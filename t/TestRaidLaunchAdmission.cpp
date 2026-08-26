/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license.
 */

#include "gtest/gtest.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <sys/stat.h>
#include <unistd.h>

#include "TestRun/DcRaidLaunchAdmission.h"

using DcRaidEngineValidation::MemberState;
using DcRaidLaunchAdmission::Coordinator;
using DcRaidLaunchAdmission::Stage;

namespace
{
    namespace fs = std::filesystem;

    class TempInbox
    {
    public:
        TempInbox()
        {
            static unsigned counter = 0;
            path = fs::temp_directory_path() /
                ("dc-raid-admission-" + std::to_string(::getpid()) + "-" + std::to_string(++counter));
            fs::create_directories(path);
        }
        ~TempInbox()
        {
            std::error_code error;
            fs::remove_all(path, error);
        }
        fs::path path;
    };

    std::string Manifest(std::string const& requestId)
    {
        std::ostringstream out;
        out << "{\"schema\":1,\"requestId\":\"" << requestId
            << "\",\"raid\":\"molten-core\",\"members\":[";
        for (std::uint32_t index = 0; index < 10; ++index)
        {
            char const* role = index == 0 ? "main_tank" : index == 1 ? "off_tank" : index < 4 ? "healer" : "dps";
            out << (index ? "," : "") << "{\"guid\":" << index + 1
                << ",\"name\":\"AdmissionBot" << index + 1 << "\",\"role\":\""
                << role << "\",\"subgroup\":" << index / 5 + 1 << '}';
        }
        return out.str() + "]}";
    }

    void WriteRequest(TempInbox const& inbox, std::string const& id, std::string const& body)
    {
        fs::path const path = inbox.path / (id + ".json");
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file << body;
        file.close();
        fs::permissions(path, fs::perms::owner_read | fs::perms::owner_write,
                        fs::perm_options::replace);
    }

    std::vector<MemberState> Snapshot(bool valid = true)
    {
        std::vector<MemberState> result;
        for (std::uint64_t guid = 1; guid <= 10; ++guid)
            result.push_back({guid, "AdmissionBot" + std::to_string(guid), 1, valid ? 60u : 59u, false, false});
        return result;
    }
}

TEST(DcRaidLaunchAdmissionTest, RunsValidationReservationAndReceiptInOrder)
{
    TempInbox inbox;
    WriteRequest(inbox, "rr-admit", Manifest("rr-admit"));
    Coordinator coordinator(inbox.path.string());

    auto const result = coordinator.Accept("rr-admit", Snapshot(), 1);

    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_EQ(result.request.members.size(), 10u);
    EXPECT_EQ(coordinator.ReservedCount(), 10u);
    EXPECT_TRUE(fs::exists(inbox.path / "accepted" / "rr-admit.accepted"));
}

TEST(DcRaidLaunchAdmissionTest, ValidationFailureDoesNotReserveOrWriteReceipt)
{
    TempInbox inbox;
    WriteRequest(inbox, "rr-invalid-engine", Manifest("rr-invalid-engine"));
    Coordinator coordinator(inbox.path.string());

    auto const result = coordinator.Accept("rr-invalid-engine", Snapshot(false), 1);

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.stage, Stage::Validation);
    EXPECT_EQ(coordinator.ReservedCount(), 0u);
    EXPECT_FALSE(fs::exists(inbox.path / "accepted" / "rr-invalid-engine.accepted"));
}

TEST(DcRaidLaunchAdmissionTest, DuplicateReceiptDoesNotReserveAgain)
{
    TempInbox inbox;
    WriteRequest(inbox, "rr-duplicate", Manifest("rr-duplicate"));
    Coordinator coordinator(inbox.path.string());

    ASSERT_TRUE(coordinator.Accept("rr-duplicate", Snapshot(), 1).ok);
    coordinator.Release({});
    auto const result = coordinator.Accept("rr-duplicate", Snapshot(), 1);

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.stage, Stage::Receipt);
    EXPECT_EQ(coordinator.ReservedCount(), 10u);
}

/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "gtest/gtest.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>

#include <sys/stat.h>
#include <unistd.h>

#include "TestRun/DcRaidRequestGateway.h"

using DcRaidRequestGateway::Coordinator;
using DcRaidRequestGateway::Error;
using DcRaidRequestGateway::Gateway;
using DcRaidRequestGateway::Result;

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
                ("dc-raid-request-" + std::to_string(::getpid()) + "-" + std::to_string(++counter));
            fs::create_directories(path);
        }

        ~TempInbox()
        {
            std::error_code error;
            fs::remove_all(path, error);
        }

        fs::path path;
    };

    std::string ValidManifest(std::string const& requestId = "rr-gateway-test")
    {
        std::ostringstream out;
        out << "{\"schema\":1,\"requestId\":\"" << requestId
            << "\",\"raid\":\"molten-core\",\"members\":[";
        for (std::uint32_t index = 0; index < 10; ++index)
        {
            char const* role = index == 0 ? "main_tank" :
                index == 1 ? "off_tank" : index < 4 ? "healer" : "dps";
            out << (index ? "," : "") << "{\"guid\":" << index + 1
                << ",\"name\":\"GatewayBot" << index + 1
                << "\",\"role\":\"" << role
                << "\",\"subgroup\":" << index / 5 + 1 << '}';
        }
        out << "]}";
        return out.str();
    }

    fs::path WriteRequest(TempInbox const& inbox, std::string const& requestId,
                          std::string const& body)
    {
        fs::path const path = inbox.path / (requestId + ".json");
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file << body;
        file.close();
        fs::permissions(path, fs::perms::owner_read | fs::perms::owner_write,
                        fs::perm_options::replace);
        return path;
    }

    void ExpectError(Result const& result, Error error)
    {
        EXPECT_FALSE(result.ok);
        EXPECT_EQ(result.error, error) << result.message;
        EXPECT_FALSE(result.message.empty());
    }
}

TEST(DcRaidRequestGatewayTest, LoadsAValidOwnerOnlyManifest)
{
    TempInbox inbox;
    WriteRequest(inbox, "rr-gateway-test", ValidManifest());

    Result const result = Gateway(inbox.path.string()).Load("rr-gateway-test");

    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_EQ(result.error, Error::None);
    EXPECT_EQ(result.request.requestId, "rr-gateway-test");
    EXPECT_EQ(result.request.raidToken, "molten-core");
    EXPECT_EQ(result.request.members.size(), 10u);
}

TEST(DcRaidRequestGatewayTest, RejectsUnsafeIdBeforeLookingAtTheFilesystem)
{
    ExpectError(Gateway("/does/not/exist").Load("../escape"), Error::InvalidRequestId);
    ExpectError(Gateway("/does/not/exist").Load("rr-é"), Error::InvalidRequestId);
}

TEST(DcRaidRequestGatewayTest, DistinguishesMissingRootAndMissingRequest)
{
    ExpectError(Gateway("/does/not/exist").Load("rr-missing"), Error::RootUnavailable);

    TempInbox inbox;
    ExpectError(Gateway(inbox.path.string()).Load("rr-missing"), Error::NotFound);
}

TEST(DcRaidRequestGatewayTest, RejectsBroadPermissionsAndOversizedFiles)
{
    TempInbox inbox;
    fs::path const broad = WriteRequest(inbox, "rr-broad", ValidManifest("rr-broad"));
    fs::permissions(broad, fs::perms::owner_all | fs::perms::group_read,
                    fs::perm_options::replace);
    ExpectError(Gateway(inbox.path.string()).Load("rr-broad"), Error::BadPermissions);

    WriteRequest(inbox, "rr-large", std::string(1025, 'x'));
    ExpectError(Gateway(inbox.path.string(), 1024).Load("rr-large"), Error::InvalidSize);
}

TEST(DcRaidRequestGatewayTest, RejectsSymlinkRootAndSymlinkRequest)
{
    TempInbox inbox;
    fs::path const realRoot = inbox.path / "real";
    fs::create_directory(realRoot);
    fs::path const linkedRoot = inbox.path / "root-link";
    fs::create_directory_symlink(realRoot, linkedRoot);
    ExpectError(Gateway(linkedRoot.string()).Load("rr-link"), Error::UnsafePath);

    fs::path const target = WriteRequest(inbox, "rr-target", ValidManifest("rr-link"));
    fs::create_symlink(target, inbox.path / "rr-link.json");
    ExpectError(Gateway(inbox.path.string()).Load("rr-link"), Error::UnsafePath);
}

TEST(DcRaidRequestGatewayTest, RejectsNonRegularAndEmptyRequests)
{
    TempInbox inbox;
    fs::path const fifo = inbox.path / "rr-fifo.json";
    ASSERT_EQ(::mkfifo(fifo.c_str(), 0600), 0);
    ExpectError(Gateway(inbox.path.string()).Load("rr-fifo"), Error::UnsafePath);

    WriteRequest(inbox, "rr-empty", "");
    ExpectError(Gateway(inbox.path.string()).Load("rr-empty"), Error::InvalidSize);
}

TEST(DcRaidRequestGatewayTest, DomainValidationFailureIsReportedWithoutARequest)
{
    TempInbox inbox;
    WriteRequest(inbox, "rr-invalid", "{\"schema\":2}");

    Result const result = Gateway(inbox.path.string()).Load("rr-invalid");
    ExpectError(result, Error::InvalidManifest);
    EXPECT_TRUE(result.request.requestId.empty());
}

TEST(DcRaidRequestCoordinatorTest, AcceptsAValidRequestOnlyOnce)
{
    TempInbox inbox;
    WriteRequest(inbox, "rr-once", ValidManifest("rr-once"));
    Coordinator coordinator(inbox.path.string());

    ASSERT_TRUE(coordinator.Accept("rr-once").ok);
    EXPECT_EQ(coordinator.AcceptedCount(), 1u);
    ExpectError(coordinator.Accept("rr-once"), Error::AlreadyAccepted);
    EXPECT_EQ(coordinator.AcceptedCount(), 1u);
}

TEST(DcRaidRequestCoordinatorTest, AcceptedRequestLeavesDurableReceipt)
{
    TempInbox inbox;
    WriteRequest(inbox, "rr-receipt", ValidManifest("rr-receipt"));
    Coordinator coordinator(inbox.path.string());

    ASSERT_TRUE(coordinator.Accept("rr-receipt").ok);
    fs::path const accepted = inbox.path / "accepted";
    fs::path const receipt = inbox.path / "accepted" / "rr-receipt.accepted";
    EXPECT_EQ(fs::status(accepted).permissions(), fs::perms::owner_all);
    ASSERT_TRUE(fs::is_regular_file(receipt));
    EXPECT_EQ(fs::status(receipt).permissions(),
              fs::perms::owner_read | fs::perms::owner_write);
    std::ifstream file(receipt);
    std::string content((std::istreambuf_iterator<char>(file)), {});
    EXPECT_NE(content.find("\"state\":\"accepted\""), std::string::npos);
    EXPECT_NE(content.find("rr-receipt"), std::string::npos);
}

TEST(DcRaidRequestCoordinatorTest, ReceiptSurvivesCoordinatorRestart)
{
    TempInbox inbox;
    WriteRequest(inbox, "rr-restart", ValidManifest("rr-restart"));
    ASSERT_TRUE(Coordinator(inbox.path.string()).Accept("rr-restart").ok);
    fs::remove(inbox.path / "rr-restart.json");

    Coordinator restarted(inbox.path.string());
    ExpectError(restarted.Accept("rr-restart"), Error::AlreadyAccepted);
    EXPECT_EQ(restarted.AcceptedCount(), 0u);
}

TEST(DcRaidRequestCoordinatorTest, InvalidRequestDoesNotCreateReceipt)
{
    TempInbox inbox;
    WriteRequest(inbox, "rr-no-receipt", "not-json");
    Coordinator coordinator(inbox.path.string());

    ExpectError(coordinator.Accept("rr-no-receipt"), Error::InvalidManifest);
    EXPECT_FALSE(fs::exists(inbox.path / "accepted" / "rr-no-receipt.accepted"));
}

TEST(DcRaidRequestCoordinatorTest, FailedRequestDoesNotReserveItsId)
{
    TempInbox inbox;
    WriteRequest(inbox, "rr-retry", "not-json");
    Coordinator coordinator(inbox.path.string());

    ExpectError(coordinator.Accept("rr-retry"), Error::InvalidManifest);
    EXPECT_EQ(coordinator.AcceptedCount(), 0u);

    WriteRequest(inbox, "rr-retry", ValidManifest("rr-retry"));
    EXPECT_TRUE(coordinator.Accept("rr-retry").ok);
    EXPECT_EQ(coordinator.AcceptedCount(), 1u);
}

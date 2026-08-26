/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license.
 */

#include "gtest/gtest.h"

#include <cstdint>
#include <string>
#include <vector>

#include "TestRun/DcRaidEngineValidation.h"

using DcRaidEngineValidation::Error;
using DcRaidEngineValidation::MemberState;
using DcRaidEngineValidation::Validate;
using DcRaidLaunchRequest::Member;
using DcRaidLaunchRequest::Request;
using DcRaidLaunchRequest::Role;

namespace
{
    Request ValidRequest()
    {
        Request request;
        request.schema = 1;
        request.requestId = "rr-engine-validation";
        request.raidToken = "molten-core";
        request.members.reserve(10);
        request.members.push_back({1, "Ironwall", Role::MainTank, 1});
        request.members.push_back({2, "Stoneguard", Role::OffTank, 1});
        request.members.push_back({3, "Lifebloom", Role::Healer, 1});
        request.members.push_back({4, "Lightwell", Role::Healer, 1});
        for (std::uint64_t guid = 5; guid <= 10; ++guid)
            request.members.push_back({guid, "Bot" + std::to_string(guid), Role::Dps, 2});
        return request;
    }

    std::vector<MemberState> ValidSnapshot()
    {
        std::vector<MemberState> snapshot;
        snapshot.reserve(10);
        snapshot.push_back({1, "Ironwall", 1, 60, false, false});
        snapshot.push_back({2, "Stoneguard", 1, 60, false, false});
        snapshot.push_back({3, "Lifebloom", 1, 60, false, false});
        snapshot.push_back({4, "Lightwell", 1, 60, false, false});
        for (std::uint64_t guid = 5; guid <= 10; ++guid)
            snapshot.push_back({guid, "Bot" + std::to_string(guid), 1, 60, false, false});
        return snapshot;
    }

    void ExpectError(Request const& request, std::vector<MemberState> const& snapshot, Error expected)
    {
        auto const result = Validate(request, snapshot, 1);
        EXPECT_FALSE(result.ok);
        EXPECT_EQ(result.error, expected) << result.message;
        EXPECT_FALSE(result.message.empty());
    }
}

TEST(DcRaidEngineValidationTest, AcceptsValidMoltenCoreSnapshot)
{
    auto const result = Validate(ValidRequest(), ValidSnapshot(), 1);

    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_EQ(result.error, Error::None);
}

TEST(DcRaidEngineValidationTest, RejectsMissingOrMismatchedIdentity)
{
    Request request = ValidRequest();
    auto snapshot = ValidSnapshot();
    snapshot.pop_back();
    ExpectError(request, snapshot, Error::MissingMember);

    snapshot = ValidSnapshot();
    snapshot.front().canonicalName = "DifferentName";
    ExpectError(request, snapshot, Error::NameMismatch);
}

TEST(DcRaidEngineValidationTest, RejectsOnlineBusyFactionAndLowLevelMembers)
{
    Request request = ValidRequest();
    auto snapshot = ValidSnapshot();
    snapshot.front().online = true;
    ExpectError(request, snapshot, Error::Online);

    snapshot = ValidSnapshot();
    snapshot.front().busy = true;
    ExpectError(request, snapshot, Error::Busy);

    snapshot = ValidSnapshot();
    snapshot.front().faction = 2;
    ExpectError(request, snapshot, Error::FactionMismatch);

    snapshot = ValidSnapshot();
    snapshot.front().level = 59;
    ExpectError(request, snapshot, Error::LevelTooLow);
}

/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "gtest/gtest.h"

#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

#include "TestRun/DcRaidLaunchRequest.h"

using DcRaidLaunchRequest::Error;
using DcRaidLaunchRequest::Parse;
using DcRaidLaunchRequest::Result;
using DcRaidLaunchRequest::Role;

namespace
{
    struct MemberJson
    {
        std::uint64_t guid;
        std::string name;
        std::string role;
        std::uint32_t subgroup;
    };

    std::vector<MemberJson> ValidMembers()
    {
        return {
            {1, "Ironwall", "main_tank", 1},
            {2, "Stoneguard", "off_tank", 1},
            {3, "Lifebloom", "healer", 1},
            {4, "Lightwell", "healer", 1},
            {5, "Ember", "dps", 1},
            {6, "Frostbite", "dps", 2},
            {7, "Backstab", "dps", 2},
            {8, "Moonfire", "dps", 2},
            {9, "Deadeye", "dps", 2},
            {10, "Stormcall", "dps", 2},
        };
    }

    std::string Manifest(std::vector<MemberJson> const& members,
                         std::string const& requestId = "rr-20260824-130000-ab12",
                         std::string const& raid = "molten-core",
                         std::uint32_t schema = 1)
    {
        std::ostringstream out;
        out << "{\"schema\":" << schema << ",\"requestId\":\"" << requestId
            << "\",\"raid\":\"" << raid << "\",\"members\":[";
        for (std::size_t index = 0; index < members.size(); ++index)
        {
            MemberJson const& member = members[index];
            out << (index ? "," : "") << "{\"guid\":" << member.guid
                << ",\"name\":\"" << member.name
                << "\",\"role\":\"" << member.role
                << "\",\"subgroup\":" << member.subgroup << '}';
        }
        out << "]}";
        return out.str();
    }

    void ExpectError(Result const& result, Error expected)
    {
        EXPECT_FALSE(result.ok);
        EXPECT_EQ(result.error, expected) << result.message;
        EXPECT_FALSE(result.message.empty());
    }
}

TEST(DcRaidLaunchRequestTest, ValidMoltenCoreManifestPreservesTypedRoster)
{
    Result const result = Parse(Manifest(ValidMembers()), "rr-20260824-130000-ab12");

    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_EQ(result.error, Error::None);
    EXPECT_EQ(result.request.schema, 1u);
    EXPECT_EQ(result.request.requestId, "rr-20260824-130000-ab12");
    EXPECT_EQ(result.request.raidToken, "molten-core");
    ASSERT_EQ(result.request.members.size(), 10u);
    EXPECT_EQ(result.request.members.front().guid, 1u);
    EXPECT_EQ(result.request.members.front().role, Role::MainTank);
    EXPECT_EQ(result.request.members.back().name, "Stormcall");
    EXPECT_EQ(result.request.members.back().subgroup, 2u);
}

TEST(DcRaidLaunchRequestTest, RejectsMalformedJsonAndUnsupportedSchema)
{
    ExpectError(Parse("not-json", "rr-20260824-130000-ab12"), Error::MalformedJson);
    ExpectError(Parse(Manifest(ValidMembers(), "rr-20260824-130000-ab12", "molten-core", 2),
                      "rr-20260824-130000-ab12"), Error::UnsupportedSchema);
}

TEST(DcRaidLaunchRequestTest, RejectsDuplicateContractFields)
{
    std::string manifest = Manifest(ValidMembers());
    manifest.replace(0, std::string("{\"schema\":1,").size(),
                     "{\"schema\":1,\"schema\":1,");
    ExpectError(Parse(manifest, "rr-20260824-130000-ab12"), Error::MalformedJson);
}

TEST(DcRaidLaunchRequestTest, RequestIdMustBeSafeAndMatchTheExpectedId)
{
    ExpectError(Parse(Manifest(ValidMembers(), "../escape"), "../escape"), Error::InvalidRequestId);
    ExpectError(Parse(Manifest(ValidMembers()), "rr-another-request"), Error::RequestIdMismatch);
}

TEST(DcRaidLaunchRequestTest, RaidMustExistAndRosterSizeMustBeAllowed)
{
    ExpectError(Parse(Manifest(ValidMembers(), "rr-20260824-130000-ab12", "naxxramas"),
                      "rr-20260824-130000-ab12"), Error::UnknownRaid);

    std::vector<MemberJson> tooSmall = ValidMembers();
    tooSmall.pop_back();
    ExpectError(Parse(Manifest(tooSmall), "rr-20260824-130000-ab12"), Error::InvalidSize);
}

TEST(DcRaidLaunchRequestTest, GuidsAndNamesMustBePresentAndUnique)
{
    std::vector<MemberJson> members = ValidMembers();
    members[1].guid = members[0].guid;
    ExpectError(Parse(Manifest(members), "rr-20260824-130000-ab12"), Error::DuplicateGuid);

    members = ValidMembers();
    members[1].name = "IRONWALL";
    ExpectError(Parse(Manifest(members), "rr-20260824-130000-ab12"), Error::DuplicateName);

    members = ValidMembers();
    members[0].guid = 0;
    ExpectError(Parse(Manifest(members), "rr-20260824-130000-ab12"), Error::InvalidMember);
}

TEST(DcRaidLaunchRequestTest, RolesMustBeKnownAndMeetRaidComposition)
{
    std::vector<MemberJson> members = ValidMembers();
    members[5].role = "support";
    ExpectError(Parse(Manifest(members), "rr-20260824-130000-ab12"), Error::InvalidRole);

    members = ValidMembers();
    members[0].role = "dps";
    ExpectError(Parse(Manifest(members), "rr-20260824-130000-ab12"), Error::InvalidComposition);

    members = ValidMembers();
    members[4].role = "main_tank";
    ExpectError(Parse(Manifest(members), "rr-20260824-130000-ab12"), Error::InvalidComposition);

    members = ValidMembers();
    members[1].role = "dps";
    ExpectError(Parse(Manifest(members), "rr-20260824-130000-ab12"), Error::InvalidComposition);
}

TEST(DcRaidLaunchRequestTest, SubgroupsAreOneToEightWithAtMostFiveMembers)
{
    std::vector<MemberJson> members = ValidMembers();
    members[0].subgroup = 0;
    ExpectError(Parse(Manifest(members), "rr-20260824-130000-ab12"), Error::InvalidSubgroup);

    members = ValidMembers();
    members[5].subgroup = 1;
    ExpectError(Parse(Manifest(members), "rr-20260824-130000-ab12"), Error::SubgroupFull);
}

TEST(DcRaidLaunchRequestTest, MissingOrNonArrayMembersAreRejected)
{
    std::string const missing =
        "{\"schema\":1,\"requestId\":\"rr-20260824-130000-ab12\",\"raid\":\"molten-core\"}";
    ExpectError(Parse(missing, "rr-20260824-130000-ab12"), Error::MissingMembers);

    std::string const object =
        "{\"schema\":1,\"requestId\":\"rr-20260824-130000-ab12\","
        "\"raid\":\"molten-core\",\"members\":{\"guid\":1}}";
    ExpectError(Parse(object, "rr-20260824-130000-ab12"), Error::MissingMembers);
}

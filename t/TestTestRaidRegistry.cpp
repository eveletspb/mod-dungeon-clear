/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "gtest/gtest.h"

#include <algorithm>
#include <set>
#include <string>

#include "TestRun/DcTestRaidRegistry.h"

using DcTestRaidRegistry::All;
using DcTestRaidRegistry::BuildJson;
using DcTestRaidRegistry::Find;
using DcTestRaidRegistry::Row;

TEST(DcTestRaidRegistryTest, EveryRowHasAConsistentLaunchContract)
{
    std::set<std::string> tokens;
    for (Row const& raid : All())
    {
        EXPECT_TRUE(tokens.insert(raid.token).second) << "duplicate token: " << raid.token;
        EXPECT_NE(raid.mapId, 0u) << raid.token;
        EXPECT_GT(raid.recommendedLevel, 0u) << raid.token;
        EXPECT_LE(raid.recommendedLevel, 80u) << raid.token;
        EXPECT_LE(raid.minMembers, raid.maxMembers) << raid.token;
        EXPECT_FALSE(raid.allowedSizes.empty()) << raid.token;
        EXPECT_NE(std::find(raid.allowedSizes.begin(), raid.allowedSizes.end(), raid.recommendedSize),
            raid.allowedSizes.end()) << raid.token;

        for (std::uint32_t size : raid.allowedSizes)
        {
            EXPECT_GE(size, raid.minMembers) << raid.token;
            EXPECT_LE(size, raid.maxMembers) << raid.token;
        }

        std::uint32_t const requiredRoles = raid.composition.mainTanks
            + raid.composition.offTanksMin + raid.composition.healersMin;
        EXPECT_LE(requiredRoles, raid.minMembers) << raid.token;
    }
}

TEST(DcTestRaidRegistryTest, MoltenCoreDefinesTheInitialRaidContract)
{
    ASSERT_EQ(All().size(), 1u);

    Row const& raid = All().front();
    EXPECT_STREQ(raid.token, "molten-core");
    EXPECT_STREQ(raid.name, "Molten Core");
    EXPECT_EQ(raid.mapId, 409u);
    EXPECT_EQ(raid.recommendedLevel, 60u);
    EXPECT_FALSE(raid.heroic);
    EXPECT_EQ(raid.minMembers, 10u);
    EXPECT_EQ(raid.maxMembers, 40u);
    EXPECT_EQ(raid.allowedSizes, (std::vector<std::uint32_t>{10, 20, 25, 40}));
    EXPECT_EQ(raid.recommendedSize, 20u);
    EXPECT_EQ(raid.composition.mainTanks, 1u);
    EXPECT_EQ(raid.composition.offTanksMin, 1u);
    EXPECT_EQ(raid.composition.healersMin, 2u);
}

TEST(DcTestRaidRegistryTest, MoltenCoreBossesAreOrderedAndUnique)
{
    Row const* raid = Find("molten-core");
    ASSERT_NE(raid, nullptr);

    std::vector<std::string> actual;
    std::set<std::string> unique;
    for (char const* boss : raid->bosses)
    {
        actual.emplace_back(boss);
        EXPECT_TRUE(unique.insert(boss).second) << "duplicate boss: " << boss;
    }

    EXPECT_EQ(actual, (std::vector<std::string>{
        "Lucifron",
        "Magmadar",
        "Gehennas",
        "Garr",
        "Baron Geddon",
        "Shazzrah",
        "Sulfuron Harbinger",
        "Golemagg the Incinerator",
        "Majordomo Executus",
        "Ragnaros",
    }));
}

TEST(DcTestRaidRegistryTest, LookupAcceptsTokenAndUnambiguousMapId)
{
    ASSERT_NE(Find("molten-core"), nullptr);
    ASSERT_NE(Find("409"), nullptr);
    EXPECT_EQ(Find(""), nullptr);
    EXPECT_EQ(Find("naxxramas"), nullptr);
    EXPECT_EQ(Find("409x"), nullptr);
}

TEST(DcTestRaidRegistryTest, JsonIsVersionedAndCarriesTheMoltenCoreContract)
{
    std::string const json = BuildJson();
    EXPECT_NE(json.find("\"schema\":1"), std::string::npos);
    EXPECT_NE(json.find("\"maxMembers\":40"), std::string::npos);
    EXPECT_NE(json.find("\"token\":\"molten-core\""), std::string::npos);
    EXPECT_NE(json.find("\"mapId\":409"), std::string::npos);
    EXPECT_NE(json.find("\"recommendedSize\":20"), std::string::npos);
    EXPECT_NE(json.find("\"Ragnaros\""), std::string::npos);
}

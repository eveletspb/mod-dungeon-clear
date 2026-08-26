/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "DcTestRaidRegistry.h"

#include <cstdlib>
#include <fstream>
#include <sstream>

#include "TestRun/DcTestRunRecord.h"

namespace DcTestRaidRegistry
{
    std::vector<Row> const& All()
    {
        static std::vector<Row> const rows = {
            {
                "molten-core",
                "Molten Core",
                409,
                60,
                false,
                10,
                40,
                {10, 20, 25, 40},
                20,
                {1, 1, 2},
                {
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
                },
            },
        };
        return rows;
    }

    Row const* Find(std::string const& tokenOrMapId)
    {
        if (tokenOrMapId.empty())
            return nullptr;

        for (Row const& row : All())
            if (tokenOrMapId == row.token)
                return &row;

        char* end = nullptr;
        unsigned long const mapId = std::strtoul(tokenOrMapId.c_str(), &end, 10);
        if (!end || *end != '\0' || mapId == 0)
            return nullptr;

        Row const* match = nullptr;
        for (Row const& row : All())
            if (row.mapId == mapId)
            {
                if (match)
                    return nullptr;
                match = &row;
            }
        return match;
    }

    std::string BuildJson()
    {
        using DcTestRunRecord::EscapeJson;

        std::ostringstream out;
        out << "{\"schema\":1,\"limits\":{\"maxActive\":1,\"maxMembers\":40},\"raids\":[";
        bool firstRaid = true;
        for (Row const& raid : All())
        {
            if (!firstRaid)
                out << ',';
            firstRaid = false;

            out << "{\"token\":\"" << EscapeJson(raid.token)
                << "\",\"name\":\"" << EscapeJson(raid.name)
                << "\",\"mapId\":" << raid.mapId
                << ",\"level\":" << raid.recommendedLevel
                << ",\"heroic\":" << (raid.heroic ? "true" : "false")
                << ",\"minMembers\":" << raid.minMembers
                << ",\"maxMembers\":" << raid.maxMembers
                << ",\"allowedSizes\":[";

            for (std::size_t index = 0; index < raid.allowedSizes.size(); ++index)
                out << (index ? "," : "") << raid.allowedSizes[index];

            out << "],\"recommendedSize\":" << raid.recommendedSize
                << ",\"composition\":{\"mainTanks\":" << raid.composition.mainTanks
                << ",\"offTanksMin\":" << raid.composition.offTanksMin
                << ",\"healersMin\":" << raid.composition.healersMin
                << "},\"bosses\":[";

            for (std::size_t index = 0; index < raid.bosses.size(); ++index)
                out << (index ? "," : "") << '"' << EscapeJson(raid.bosses[index]) << '"';

            out << "]}";
        }
        out << "]}";
        return out.str();
    }

    void WriteSidecar()
    {
        char const* path = "dc_test_raids.json";
        if (char const* configured = std::getenv("DC_TEST_RAIDS_FILE"))
            if (configured[0])
                path = configured;

        std::ofstream file(path, std::ios::out | std::ios::trunc);
        if (file.is_open())
            file << BuildJson() << '\n';
    }
}

/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DCTESTRAIDREGISTRY_H
#define _PLAYERBOT_DCTESTRAIDREGISTRY_H

#include <cstdint>
#include <string>
#include <vector>

// Hand-authored catalogue for the Raid Runner contract. It is deliberately
// separate from DcTestDungeonRegistry: raid composition, size presets and the
// ordered encounter roster are raid-specific and must not change the existing
// five-player test-run API.
namespace DcTestRaidRegistry
{
    struct Composition
    {
        std::uint32_t mainTanks = 0;
        std::uint32_t offTanksMin = 0;
        std::uint32_t healersMin = 0;
    };

    struct Row
    {
        char const* token;
        char const* name;
        std::uint32_t mapId;
        std::uint32_t recommendedLevel;
        bool heroic;
        std::uint32_t minMembers;
        std::uint32_t maxMembers;
        std::vector<std::uint32_t> allowedSizes;
        std::uint32_t recommendedSize;
        Composition composition;
        std::vector<char const*> bosses;
    };

    // Row for an exact command token or an unambiguous numeric map id.
    std::vector<Row> const& All();
    Row const* Find(std::string const& tokenOrMapId);

    // Versioned dashboard contract. WriteSidecar publishes it to
    // dc_test_raids.json (DC_TEST_RAIDS_FILE overrides the path).
    std::string BuildJson();
    void WriteSidecar();
}

#endif  // _PLAYERBOT_DCTESTRAIDREGISTRY_H

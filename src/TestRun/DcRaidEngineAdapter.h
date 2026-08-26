/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license.
 */

#ifndef _PLAYERBOT_DCRAIDENGINEADAPTER_H
#define _PLAYERBOT_DCRAIDENGINEADAPTER_H

#include <cstdint>
#include <string>
#include <unordered_set>

#include "TestRun/DcRaidEngineValidation.h"

class Player;

namespace DcRaidEngineAdapter
{
    // Read the current character cache and connected-player state, then run
    // the pure validator. Must be called on the world thread; it does not
    // reserve characters or start any asynchronous playerbot operation.
    DcRaidEngineValidation::Result Validate(
        DcRaidLaunchRequest::Request const& request,
        Player const* leader,
        std::unordered_set<std::uint64_t> const& reservedGuids);
}

#endif  // _PLAYERBOT_DCRAIDENGINEADAPTER_H

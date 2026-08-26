/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license.
 */

#ifndef _PLAYERBOT_DCRAIDENGINEVALIDATION_H
#define _PLAYERBOT_DCRAIDENGINEVALIDATION_H

#include <cstdint>
#include <string>
#include <vector>

#include "TestRun/DcRaidLaunchRequest.h"

namespace DcRaidEngineValidation
{
    enum class Error
    {
        None,
        MissingMember,
        NameMismatch,
        Online,
        Busy,
        FactionMismatch,
        LevelTooLow,
    };

    struct MemberState
    {
        std::uint64_t guid = 0;
        std::string canonicalName;
        std::uint32_t faction = 0;
        std::uint32_t level = 0;
        bool online = false;
        bool busy = false;
    };

    struct Result
    {
        bool ok = false;
        Error error = Error::MissingMember;
        std::string message;
    };

    // Validates a parsed manifest against a read-only snapshot collected by a
    // future worldserver adapter. The function performs no reservation,
    // login, grouping or other world mutation.
    Result Validate(DcRaidLaunchRequest::Request const& request,
                    std::vector<MemberState> const& members,
                    std::uint32_t leaderFaction);
}

#endif  // _PLAYERBOT_DCRAIDENGINEVALIDATION_H

/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license.
 */

#include "DcRaidEngineValidation.h"

#include <algorithm>
#include <utility>

namespace DcRaidEngineValidation
{
    namespace
    {
        Result Fail(Error error, std::string message)
        {
            return {false, error, std::move(message)};
        }
    }

    Result Validate(DcRaidLaunchRequest::Request const& request,
                    std::vector<MemberState> const& members,
                    std::uint32_t leaderFaction)
    {
        for (DcRaidLaunchRequest::Member const& requested : request.members)
        {
            auto const found = std::find_if(members.begin(), members.end(), [&](MemberState const& state)
            {
                return state.guid == requested.guid;
            });
            if (found == members.end())
                return Fail(Error::MissingMember, "raid member guid is not present in the engine snapshot");
            if (found->canonicalName != requested.name)
                return Fail(Error::NameMismatch, "raid member name does not match the requested guid");
            if (found->online)
                return Fail(Error::Online, "raid member must be offline before provisioning");
            if (found->busy)
                return Fail(Error::Busy, "raid member is already reserved by another job");
            if (found->faction != leaderFaction)
                return Fail(Error::FactionMismatch, "all raid members must belong to the leader faction");
            if (found->level < 60)
                return Fail(Error::LevelTooLow, "Molten Core members must be level 60 or higher");
        }

        return {true, Error::None, {}};
    }
}

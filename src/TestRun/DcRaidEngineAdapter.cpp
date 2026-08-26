/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license.
 */

#include "DcRaidEngineAdapter.h"

#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "CharacterCache.h"
#include "ObjectAccessor.h"
#include "ObjectGuid.h"
#include "Player.h"

namespace DcRaidEngineAdapter
{
    namespace
    {
        using DcRaidEngineValidation::Error;
        using DcRaidEngineValidation::MemberState;
        using DcRaidEngineValidation::Result;

        Result Fail(Error error, std::string message)
        {
            return {false, error, std::move(message)};
        }
    }

    Result Validate(DcRaidLaunchRequest::Request const& request,
                    Player const* leader,
                    std::unordered_set<std::uint64_t> const& reservedGuids)
    {
        if (!leader)
            return Fail(Error::FactionMismatch, "raid leader is not available on the world thread");

        std::vector<MemberState> snapshot;
        snapshot.reserve(request.members.size());
        for (DcRaidLaunchRequest::Member const& member : request.members)
        {
            if (member.guid > std::numeric_limits<std::uint32_t>::max())
                return Fail(Error::MissingMember, "raid member guid is outside the player guid range");

            ObjectGuid const guid = ObjectGuid::Create<HighGuid::Player>(static_cast<std::uint32_t>(member.guid));
            std::string canonicalName;
            if (!sCharacterCache->GetCharacterNameByGuid(guid, canonicalName))
                return Fail(Error::MissingMember, "raid member is not present in CharacterCache");

            snapshot.push_back({
                member.guid,
                std::move(canonicalName),
                sCharacterCache->GetCharacterTeamByGuid(guid),
                sCharacterCache->GetCharacterLevelByGuid(guid),
                ObjectAccessor::FindConnectedPlayer(guid) != nullptr,
                reservedGuids.find(member.guid) != reservedGuids.end(),
            });
        }

        return DcRaidEngineValidation::Validate(request, snapshot, leader->GetTeamId(true));
    }
}

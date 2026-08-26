/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license.
 */

#include "DcRaidRosterReservation.h"

#include <utility>

namespace DcRaidRosterReservation
{
    namespace
    {
        Result Fail(Error error, std::string message)
        {
            return {false, error, std::move(message)};
        }
    }

    Result Store::TryReserve(DcRaidLaunchRequest::Request const& request)
    {
        std::unordered_set<std::uint64_t> requested;
        requested.reserve(request.members.size());
        for (DcRaidLaunchRequest::Member const& member : request.members)
        {
            if (!requested.insert(member.guid).second)
                return Fail(Error::DuplicateMember, "raid roster contains a duplicate guid");
            if (_guids.find(member.guid) != _guids.end())
                return Fail(Error::AlreadyReserved, "raid roster contains a reserved guid");
        }

        _guids.insert(requested.begin(), requested.end());
        return {true, Error::None, {}};
    }

    void Store::Release(DcRaidLaunchRequest::Request const& request)
    {
        for (DcRaidLaunchRequest::Member const& member : request.members)
            _guids.erase(member.guid);
    }

    bool Store::Contains(std::uint64_t guid) const
    {
        return _guids.find(guid) != _guids.end();
    }
}

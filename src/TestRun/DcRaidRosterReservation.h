/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license.
 */

#ifndef _PLAYERBOT_DCRAIDROSTERRESERVATION_H
#define _PLAYERBOT_DCRAIDROSTERRESERVATION_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_set>

#include "TestRun/DcRaidLaunchRequest.h"

namespace DcRaidRosterReservation
{
    enum class Error
    {
        None,
        DuplicateMember,
        AlreadyReserved,
    };

    struct Result
    {
        bool ok = false;
        Error error = Error::AlreadyReserved;
        std::string message;
    };

    class Store
    {
    public:
        // World-thread only. The operation is atomic from the caller's point
        // of view: a conflict leaves the store unchanged.
        Result TryReserve(DcRaidLaunchRequest::Request const& request);
        void Release(DcRaidLaunchRequest::Request const& request);

        bool Contains(std::uint64_t guid) const;
        std::unordered_set<std::uint64_t> Snapshot() const { return _guids; }
        std::size_t Size() const { return _guids.size(); }

    private:
        std::unordered_set<std::uint64_t> _guids;
    };
}

#endif  // _PLAYERBOT_DCRAIDROSTERRESERVATION_H

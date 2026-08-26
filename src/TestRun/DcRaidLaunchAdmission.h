/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license.
 */

#ifndef _PLAYERBOT_DCRAIDLAUNCHADMISSION_H
#define _PLAYERBOT_DCRAIDLAUNCHADMISSION_H

#include <cstdint>
#include <string>
#include <vector>

#include "TestRun/DcRaidEngineValidation.h"
#include "TestRun/DcRaidRequestGateway.h"
#include "TestRun/DcRaidRosterReservation.h"

class Player;

namespace DcRaidLaunchAdmission
{
    enum class Stage
    {
        None,
        Gateway,
        Validation,
        Reservation,
        Receipt,
    };

    struct Result
    {
        bool ok = false;
        Stage stage = Stage::None;
        std::string message;
        DcRaidLaunchRequest::Request request;
    };

    class Coordinator
    {
    public:
        explicit Coordinator(std::string root,
                             std::size_t maxBytes = DcRaidRequestGateway::Gateway::kDefaultMaxBytes);

        // World-thread only. `snapshot` is collected by the engine adapter
        // immediately before this call. A successful result owns the roster
        // reservation until Release(request) is called during teardown.
        Result Accept(std::string const& requestId,
                      std::vector<DcRaidEngineValidation::MemberState> const& snapshot,
                      std::uint32_t leaderFaction);
        Result Accept(std::string const& requestId, Player const* leader);
        Result Accept(std::string const& requestId,
                      DcRaidLaunchRequest::Request const& request,
                      Player const* leader);
        void Release(DcRaidLaunchRequest::Request const& request);
        std::unordered_set<std::uint64_t> ReservedGuids() const;
        std::size_t ReservedCount() const;

    private:
        DcRaidRequestGateway::Gateway _gateway;
        DcRaidRosterReservation::Store _reservations;
    };
}

#endif  // _PLAYERBOT_DCRAIDLAUNCHADMISSION_H

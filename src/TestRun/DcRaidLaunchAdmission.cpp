/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license.
 */

#include "DcRaidLaunchAdmission.h"

#include <cstdlib>
#include <utility>

#include "TestRun/DcRaidEngineAdapter.h"
#include "Player.h"

namespace DcRaidLaunchAdmission
{
    namespace
    {
        Result GatewayFailure(DcRaidRequestGateway::Result const& result)
        {
            return {false, Stage::Gateway, result.message, {}};
        }

        Result ValidationFailure(DcRaidEngineValidation::Result const& result)
        {
            return {false, Stage::Validation, result.message, {}};
        }
    }

    Coordinator::Coordinator(std::string root, std::size_t maxBytes)
        : _gateway(std::move(root), maxBytes)
    {
    }

    Result Coordinator::Accept(std::string const& requestId,
                                std::vector<DcRaidEngineValidation::MemberState> const& snapshot,
                                std::uint32_t leaderFaction)
    {
        if (_gateway.HasReceipt(requestId))
            return {false, Stage::Receipt, "raid request was already accepted", {}};

        DcRaidRequestGateway::Result const loaded = _gateway.Load(requestId);
        if (!loaded.ok)
            return GatewayFailure(loaded);

        DcRaidEngineValidation::Result const validated =
            DcRaidEngineValidation::Validate(loaded.request, snapshot, leaderFaction);
        if (!validated.ok)
            return ValidationFailure(validated);

        DcRaidRosterReservation::Result const reserved = _reservations.TryReserve(loaded.request);
        if (!reserved.ok)
            return {false, Stage::Reservation, reserved.message, {}};

        DcRaidRequestGateway::Result const receipt = _gateway.Claim(requestId);
        if (!receipt.ok)
        {
            _reservations.Release(loaded.request);
            if (receipt.error == DcRaidRequestGateway::Error::AlreadyAccepted)
                return {false, Stage::Receipt, receipt.message, {}};
            return {false, Stage::Receipt, receipt.message, {}};
        }

        return {true, Stage::None, {}, loaded.request};
    }

    Result Coordinator::Accept(std::string const& requestId, Player const* leader)
    {
        if (_gateway.HasReceipt(requestId))
            return {false, Stage::Receipt, "raid request was already accepted", {}};

        DcRaidRequestGateway::Result const loaded = _gateway.Load(requestId);
        if (!loaded.ok)
            return GatewayFailure(loaded);

        DcRaidEngineValidation::Result const validated = DcRaidEngineAdapter::Validate(
            loaded.request, leader, _reservations.Snapshot());
        if (!validated.ok)
            return ValidationFailure(validated);

        DcRaidRosterReservation::Result const reserved = _reservations.TryReserve(loaded.request);
        if (!reserved.ok)
            return {false, Stage::Reservation, reserved.message, {}};

        DcRaidRequestGateway::Result const receipt = _gateway.Claim(requestId);
        if (!receipt.ok)
        {
            _reservations.Release(loaded.request);
            return {false, Stage::Receipt, receipt.message, {}};
        }

        return {true, Stage::None, {}, loaded.request};
    }

    Result Coordinator::Accept(std::string const& requestId,
                                DcRaidLaunchRequest::Request const& request,
                                Player const* leader)
    {
        if (_gateway.HasReceipt(requestId))
            return {false, Stage::Receipt, "raid request was already accepted", {}};

        DcRaidEngineValidation::Result const validated = DcRaidEngineAdapter::Validate(
            request, leader, _reservations.Snapshot());
        if (!validated.ok)
            return ValidationFailure(validated);

        DcRaidRosterReservation::Result const reserved = _reservations.TryReserve(request);
        if (!reserved.ok)
            return {false, Stage::Reservation, reserved.message, {}};

        DcRaidRequestGateway::Result const receipt = _gateway.Claim(requestId);
        if (!receipt.ok)
        {
            _reservations.Release(request);
            return {false, Stage::Receipt, receipt.message, {}};
        }

        return {true, Stage::None, {}, request};
    }

    void Coordinator::Release(DcRaidLaunchRequest::Request const& request)
    {
        _reservations.Release(request);
    }

    std::size_t Coordinator::ReservedCount() const
    {
        return _reservations.Size();
    }

    std::unordered_set<std::uint64_t> Coordinator::ReservedGuids() const
    {
        return _reservations.Snapshot();
    }
}

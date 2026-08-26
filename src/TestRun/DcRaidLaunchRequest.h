/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DCRAIDLAUNCHREQUEST_H
#define _PLAYERBOT_DCRAIDLAUNCHREQUEST_H

#include <cstdint>
#include <string>
#include <vector>

// Engine-free parser and validator for Raid Runner launch manifests. It proves
// the complete document is internally safe before a later layer looks up any
// character or mutates world state.
namespace DcRaidLaunchRequest
{
    enum class Role
    {
        MainTank,
        OffTank,
        Healer,
        Dps,
    };

    enum class Error
    {
        None,
        MalformedJson,
        UnsupportedSchema,
        InvalidRequestId,
        RequestIdMismatch,
        UnknownRaid,
        MissingMembers,
        InvalidSize,
        InvalidMember,
        DuplicateGuid,
        DuplicateName,
        InvalidRole,
        InvalidSubgroup,
        SubgroupFull,
        InvalidComposition,
    };

    struct Member
    {
        std::uint64_t guid = 0;
        std::string name;
        Role role = Role::Dps;
        std::uint32_t subgroup = 0;
    };

    struct Request
    {
        std::uint32_t schema = 0;
        std::string requestId;
        std::string raidToken;
        std::vector<Member> members;
    };

    struct Result
    {
        bool ok = false;
        Error error = Error::MalformedJson;
        std::string message;
        Request request;
    };

    // Safe as one filesystem stem: rr- prefix, 1..61 ASCII letters/digits/'-'
    // after it, 64 bytes total at most. Shared by the manifest parser and the
    // file gateway so no path is constructed from an unchecked request ID.
    bool IsSafeRequestId(std::string const& requestId);
    Result Parse(std::string const& json, std::string const& expectedRequestId);
}

#endif  // _PLAYERBOT_DCRAIDLAUNCHREQUEST_H

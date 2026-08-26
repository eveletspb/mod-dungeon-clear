/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "DcRaidLaunchRequest.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <sstream>
#include <unordered_set>
#include <utility>

#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>

#include "TestRun/DcTestRaidRegistry.h"

namespace DcRaidLaunchRequest
{
    namespace
    {
        using boost::property_tree::ptree;

        Result Fail(Error error, std::string message)
        {
            Result result;
            result.error = error;
            result.message = std::move(message);
            return result;
        }

        std::string LowerAscii(std::string value)
        {
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch)
            {
                return static_cast<char>(std::tolower(ch));
            });
            return value;
        }

        bool HasVisibleCharacter(std::string const& value)
        {
            return std::any_of(value.begin(), value.end(), [](unsigned char ch)
            {
                return !std::isspace(ch);
            });
        }

        bool HasDuplicate(ptree const& object, char const* key)
        {
            return std::count_if(object.begin(), object.end(), [key](auto const& field)
            {
                return field.first == key;
            }) > 1;
        }

        bool ParseRole(std::string const& value, Role* role)
        {
            if (value == "main_tank")
                *role = Role::MainTank;
            else if (value == "off_tank")
                *role = Role::OffTank;
            else if (value == "healer")
                *role = Role::Healer;
            else if (value == "dps")
                *role = Role::Dps;
            else
                return false;
            return true;
        }
    }

    bool IsSafeRequestId(std::string const& requestId)
    {
        if (requestId.size() < 4 || requestId.size() > 64 || requestId.rfind("rr-", 0) != 0)
            return false;

        return std::all_of(requestId.begin() + 3, requestId.end(), [](unsigned char ch)
        {
            return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                   (ch >= '0' && ch <= '9') || ch == '-';
        });
    }

    Result Parse(std::string const& json, std::string const& expectedRequestId)
    {
        ptree document;
        try
        {
            std::istringstream input{json};
            boost::property_tree::read_json(input, document);
        }
        catch (boost::property_tree::json_parser::json_parser_error const& error)
        {
            return Fail(Error::MalformedJson, "malformed raid launch JSON: " + error.message());
        }

        try
        {
            for (auto const& field : document)
                if (field.first.empty())
                    return Fail(Error::MalformedJson, "raid launch document must be an object");
            if (HasDuplicate(document, "schema") || HasDuplicate(document, "requestId") ||
                HasDuplicate(document, "raid") || HasDuplicate(document, "members"))
                return Fail(Error::MalformedJson,
                            "raid launch document contains a duplicate contract field");

            auto const schema = document.get_optional<std::uint32_t>("schema");
            if (!schema || *schema != 1)
                return Fail(Error::UnsupportedSchema, "raid launch schema must be 1");

            auto const requestId = document.get_optional<std::string>("requestId");
            if (!requestId || !IsSafeRequestId(*requestId) || !IsSafeRequestId(expectedRequestId))
                return Fail(Error::InvalidRequestId,
                            "requestId must start with 'rr-' and contain only letters, digits or '-'");
            if (*requestId != expectedRequestId)
                return Fail(Error::RequestIdMismatch,
                            "manifest requestId does not match the requested file id");

            auto const raidToken = document.get_optional<std::string>("raid");
            DcTestRaidRegistry::Row const* raid =
                raidToken ? DcTestRaidRegistry::Find(*raidToken) : nullptr;
            if (!raid)
                return Fail(Error::UnknownRaid, "unknown raid token in launch manifest");

            auto const membersNode = document.get_child_optional("members");
            if (!membersNode)
                return Fail(Error::MissingMembers, "launch manifest must contain a members array");
            for (auto const& entry : *membersNode)
                if (!entry.first.empty())
                    return Fail(Error::MissingMembers, "launch manifest members must be an array");

            std::size_t const memberCount = membersNode->size();
            if (std::find(raid->allowedSizes.begin(), raid->allowedSizes.end(), memberCount) ==
                raid->allowedSizes.end())
                return Fail(Error::InvalidSize, "raid roster size is not allowed for this raid");

            Request request;
            request.schema = *schema;
            request.requestId = *requestId;
            request.raidToken = raid->token;
            request.members.reserve(memberCount);

            std::unordered_set<std::uint64_t> guids;
            std::unordered_set<std::string> names;
            std::array<std::uint32_t, 8> subgroupSizes{};
            std::uint32_t mainTanks = 0;
            std::uint32_t offTanks = 0;
            std::uint32_t healers = 0;

            for (auto const& entry : *membersNode)
            {
                ptree const& source = entry.second;
                if (HasDuplicate(source, "guid") || HasDuplicate(source, "name") ||
                    HasDuplicate(source, "role") || HasDuplicate(source, "subgroup"))
                    return Fail(Error::InvalidMember,
                                "raid member contains a duplicate contract field");

                auto const guid = source.get_optional<std::uint64_t>("guid");
                auto const name = source.get_optional<std::string>("name");
                auto const roleName = source.get_optional<std::string>("role");
                auto const subgroup = source.get_optional<std::uint32_t>("subgroup");
                if (!guid || *guid == 0 || !name || name->size() > 32 ||
                    !HasVisibleCharacter(*name) || !roleName || !subgroup)
                    return Fail(Error::InvalidMember,
                                "every member needs non-zero guid, name, role and subgroup");

                if (!guids.insert(*guid).second)
                    return Fail(Error::DuplicateGuid, "raid roster contains a duplicate guid");
                if (!names.insert(LowerAscii(*name)).second)
                    return Fail(Error::DuplicateName,
                                "raid roster contains a duplicate character name");

                Role role;
                if (!ParseRole(*roleName, &role))
                    return Fail(Error::InvalidRole,
                                "member role must be main_tank, off_tank, healer or dps");
                if (*subgroup < 1 || *subgroup > subgroupSizes.size())
                    return Fail(Error::InvalidSubgroup, "member subgroup must be between 1 and 8");
                if (++subgroupSizes[*subgroup - 1] > 5)
                    return Fail(Error::SubgroupFull, "raid subgroup cannot contain more than 5 members");

                if (role == Role::MainTank)
                    ++mainTanks;
                else if (role == Role::OffTank)
                    ++offTanks;
                else if (role == Role::Healer)
                    ++healers;

                request.members.push_back({*guid, *name, role, *subgroup});
            }

            if (mainTanks != raid->composition.mainTanks ||
                offTanks < raid->composition.offTanksMin ||
                healers < raid->composition.healersMin)
                return Fail(Error::InvalidComposition,
                            "raid roster does not satisfy the configured tank/healer composition");

            Result result;
            result.ok = true;
            result.error = Error::None;
            result.request = std::move(request);
            return result;
        }
        catch (boost::property_tree::ptree_error const& error)
        {
            return Fail(Error::MalformedJson, "invalid raid launch value: " + std::string(error.what()));
        }
    }
}

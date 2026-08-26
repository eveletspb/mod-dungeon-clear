/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DCRAIDREQUESTGATEWAY_H
#define _PLAYERBOT_DCRAIDREQUESTGATEWAY_H

#include <cstddef>
#include <mutex>
#include <string>
#include <unordered_set>

#include "TestRun/DcRaidLaunchRequest.h"

namespace DcRaidRequestGateway
{
    enum class Error
    {
        None,
        InvalidRequestId,
        RootUnavailable,
        NotFound,
        UnsafePath,
        BadPermissions,
        InvalidSize,
        ReadFailed,
        InvalidManifest,
        ReceiptUnavailable,
        ReceiptWriteFailed,
        AlreadyAccepted,
    };

    struct Result
    {
        bool ok = false;
        Error error = Error::ReadFailed;
        std::string message;
        DcRaidLaunchRequest::Request request;
    };

    class Gateway
    {
    public:
        static constexpr std::size_t kDefaultMaxBytes = 64 * 1024;

        explicit Gateway(std::string root, std::size_t maxBytes = kDefaultMaxBytes);
        Result Load(std::string const& requestId) const;
        bool HasReceipt(std::string const& requestId) const;
        Result Claim(std::string const& requestId) const;

    private:
        int OpenRoot() const;
        std::string _root;
        std::size_t _maxBytes;
    };

    class Coordinator
    {
    public:
        explicit Coordinator(std::string root, std::size_t maxBytes = Gateway::kDefaultMaxBytes);
        Result Accept(std::string const& requestId);
        std::size_t AcceptedCount() const;

    private:
        Gateway _gateway;
        mutable std::mutex _mutex;
        std::unordered_set<std::string> _acceptedIds;
    };
}

#endif  // _PLAYERBOT_DCRAIDREQUESTGATEWAY_H

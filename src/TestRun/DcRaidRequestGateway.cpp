/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "DcRaidRequestGateway.h"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <utility>

namespace DcRaidRequestGateway
{
    namespace
    {
        class FileDescriptor
        {
        public:
            explicit FileDescriptor(int value) : _value(value) {}
            ~FileDescriptor()
            {
                if (_value >= 0)
                    ::close(_value);
            }

            FileDescriptor(FileDescriptor const&) = delete;
            FileDescriptor& operator=(FileDescriptor const&) = delete;

            int Get() const { return _value; }
            bool IsOpen() const { return _value >= 0; }

        private:
            int _value;
        };

        Result Fail(Error error, std::string message)
        {
            Result result;
            result.error = error;
            result.message = std::move(message);
            return result;
        }

        std::string SystemError(char const* operation, int error)
        {
            return std::string(operation) + ": " + std::strerror(error);
        }
    }

    Gateway::Gateway(std::string root, std::size_t maxBytes)
        : _root(std::move(root)), _maxBytes(maxBytes)
    {
    }

    int Gateway::OpenRoot() const
    {
        struct stat rootStatus{};
        if (::lstat(_root.c_str(), &rootStatus) != 0)
            return -1;
        if (S_ISLNK(rootStatus.st_mode))
        {
            errno = ELOOP;
            return -1;
        }
        if (!S_ISDIR(rootStatus.st_mode))
        {
            errno = ENOTDIR;
            return -1;
        }
        return ::open(_root.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    }

    Result Gateway::Load(std::string const& requestId) const
    {
        if (!DcRaidLaunchRequest::IsSafeRequestId(requestId))
            return Fail(Error::InvalidRequestId, "unsafe raid request id");

        FileDescriptor rootFd(OpenRoot());
        if (!rootFd.IsOpen())
        {
            int const error = errno;
            return Fail(error == ELOOP ? Error::UnsafePath : Error::RootUnavailable,
                        SystemError("cannot open raid request root", error));
        }

        std::string const filename = requestId + ".json";
        struct stat pathStatus{};
        if (::fstatat(rootFd.Get(), filename.c_str(), &pathStatus, AT_SYMLINK_NOFOLLOW) != 0)
        {
            int const error = errno;
            return Fail(error == ENOENT ? Error::NotFound : Error::ReadFailed,
                        SystemError("raid request unavailable", error));
        }
        if (S_ISLNK(pathStatus.st_mode))
            return Fail(Error::UnsafePath, "raid request file must not be a symlink");

        FileDescriptor fileFd(::openat(rootFd.Get(), filename.c_str(),
                                       O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK));
        if (!fileFd.IsOpen())
        {
            int const error = errno;
            Error const kind = error == ENOENT ? Error::NotFound :
                error == ELOOP ? Error::UnsafePath : Error::ReadFailed;
            return Fail(kind, SystemError("cannot open raid request", error));
        }

        struct stat fileStatus{};
        if (::fstat(fileFd.Get(), &fileStatus) != 0)
        {
            int const error = errno;
            return Fail(Error::ReadFailed, SystemError("cannot inspect raid request", error));
        }
        if (pathStatus.st_dev != fileStatus.st_dev || pathStatus.st_ino != fileStatus.st_ino)
            return Fail(Error::UnsafePath, "raid request changed while opening");
        if (!S_ISREG(fileStatus.st_mode))
            return Fail(Error::UnsafePath, "raid request must be a regular file");
        if (fileStatus.st_nlink != 1)
            return Fail(Error::UnsafePath, "raid request must not have filesystem aliases");
        if ((fileStatus.st_mode & 0777) != 0600)
            return Fail(Error::BadPermissions, "raid request permissions must be 0600");
        if (fileStatus.st_size <= 0 || static_cast<std::uint64_t>(fileStatus.st_size) > _maxBytes)
            return Fail(Error::InvalidSize, "raid request size must be between 1 and " +
                        std::to_string(_maxBytes) + " bytes");

        std::string json(static_cast<std::size_t>(fileStatus.st_size), '\0');
        std::size_t offset = 0;
        while (offset < json.size())
        {
            ssize_t const count = ::read(fileFd.Get(), json.data() + offset, json.size() - offset);
            if (count > 0)
            {
                offset += static_cast<std::size_t>(count);
                continue;
            }
            if (count < 0 && errno == EINTR)
                continue;
            if (count == 0)
                return Fail(Error::ReadFailed, "raid request changed while reading");
            int const error = errno;
            return Fail(Error::ReadFailed, SystemError("cannot read raid request", error));
        }

        struct stat finalStatus{};
        if (::fstat(fileFd.Get(), &finalStatus) != 0 ||
            finalStatus.st_size != fileStatus.st_size ||
            finalStatus.st_dev != fileStatus.st_dev || finalStatus.st_ino != fileStatus.st_ino)
            return Fail(Error::ReadFailed, "raid request changed while reading");

        DcRaidLaunchRequest::Result parsed = DcRaidLaunchRequest::Parse(json, requestId);
        if (!parsed.ok)
            return Fail(Error::InvalidManifest, parsed.message);

        Result result;
        result.ok = true;
        result.error = Error::None;
        result.request = std::move(parsed.request);
        return result;
    }

    bool Gateway::HasReceipt(std::string const& requestId) const
    {
        if (!DcRaidLaunchRequest::IsSafeRequestId(requestId))
            return false;

        FileDescriptor rootFd(OpenRoot());
        if (!rootFd.IsOpen())
            return false;
        FileDescriptor acceptedFd(::openat(rootFd.Get(), "accepted",
                                           O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
        if (!acceptedFd.IsOpen())
            return false;

        std::string const filename = requestId + ".accepted";
        struct stat status{};
        return ::fstatat(acceptedFd.Get(), filename.c_str(), &status, AT_SYMLINK_NOFOLLOW) == 0 &&
               S_ISREG(status.st_mode) && status.st_nlink == 1;
    }

    Result Gateway::Claim(std::string const& requestId) const
    {
        if (!DcRaidLaunchRequest::IsSafeRequestId(requestId))
            return Fail(Error::InvalidRequestId, "unsafe raid request id");

        FileDescriptor rootFd(OpenRoot());
        if (!rootFd.IsOpen())
        {
            int const error = errno;
            return Fail(error == ELOOP ? Error::UnsafePath : Error::ReceiptUnavailable,
                        SystemError("cannot open raid request root", error));
        }

        if (::mkdirat(rootFd.Get(), "accepted", 0700) != 0 && errno != EEXIST)
        {
            int const error = errno;
            return Fail(error == ELOOP ? Error::UnsafePath : Error::ReceiptUnavailable,
                        SystemError("cannot create raid receipt directory", error));
        }

        FileDescriptor acceptedFd(::openat(rootFd.Get(), "accepted",
                                           O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
        if (!acceptedFd.IsOpen())
        {
            int const error = errno;
            return Fail(error == ELOOP ? Error::UnsafePath : Error::ReceiptUnavailable,
                        SystemError("cannot open raid receipt directory", error));
        }

        struct stat directoryStatus{};
        if (::fstat(acceptedFd.Get(), &directoryStatus) != 0 ||
            !S_ISDIR(directoryStatus.st_mode) || (directoryStatus.st_mode & 0777) != 0700)
            return Fail(Error::ReceiptUnavailable, "raid receipt directory permissions must be 0700");

        std::string const filename = requestId + ".accepted";
        FileDescriptor receiptFd(::openat(acceptedFd.Get(), filename.c_str(),
                                          O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC,
                                          0600));
        if (!receiptFd.IsOpen())
        {
            int const error = errno;
            if (error == EEXIST)
                return Fail(Error::AlreadyAccepted, "raid request receipt already exists");
            return Fail(Error::ReceiptWriteFailed, SystemError("cannot create raid request receipt", error));
        }

        if (::fchmod(receiptFd.Get(), 0600) != 0)
        {
            int const error = errno;
            ::unlinkat(acceptedFd.Get(), filename.c_str(), 0);
            return Fail(Error::ReceiptWriteFailed, SystemError("cannot set raid receipt permissions", error));
        }

        std::string const receipt = "{\"schema\":1,\"requestId\":\"" + requestId +
            "\",\"state\":\"accepted\"}\n";
        std::size_t offset = 0;
        while (offset < receipt.size())
        {
            ssize_t const count = ::write(receiptFd.Get(), receipt.data() + offset, receipt.size() - offset);
            if (count > 0)
            {
                offset += static_cast<std::size_t>(count);
                continue;
            }
            if (count < 0 && errno == EINTR)
                continue;
            int const error = errno;
            ::unlinkat(acceptedFd.Get(), filename.c_str(), 0);
            return Fail(Error::ReceiptWriteFailed, count == 0 ?
                        "raid receipt write made no progress" :
                        SystemError("cannot write raid request receipt", error));
        }

        if (::fsync(receiptFd.Get()) != 0)
        {
            int const error = errno;
            ::unlinkat(acceptedFd.Get(), filename.c_str(), 0);
            return Fail(Error::ReceiptWriteFailed, SystemError("cannot flush raid request receipt", error));
        }

        Result result;
        result.ok = true;
        result.error = Error::None;
        return result;
    }

    Coordinator::Coordinator(std::string root, std::size_t maxBytes)
        : _gateway(std::move(root), maxBytes)
    {
    }

    Result Coordinator::Accept(std::string const& requestId)
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (_acceptedIds.count(requestId))
            return Fail(Error::AlreadyAccepted, "raid request was already accepted in this process");
        if (_gateway.HasReceipt(requestId))
            return Fail(Error::AlreadyAccepted, "raid request was already accepted");

        Result result = _gateway.Load(requestId);
        if (!result.ok)
            return result;

        Result const claimed = _gateway.Claim(requestId);
        if (!claimed.ok)
            return claimed;

        _acceptedIds.insert(requestId);
        return result;
    }

    std::size_t Coordinator::AcceptedCount() const
    {
        std::lock_guard<std::mutex> lock(_mutex);
        return _acceptedIds.size();
    }
}

#pragma once

#include <cstdint>
#include <string>

#include "Api/DungeonClearStopCause.h"
#include "ObjectGuid.h"

class Group;
class Player;

namespace DungeonClear
{
    struct StartOptions
    {
        std::string reason = "controller start";
        ObjectGuid requesterGuid;

        ObjectGuid ResolveOwnerGuid(ObjectGuid groupLeaderGuid) const;
    };

    enum class Error
    {
        None,
        InvalidGroup,
        LeaderOffline,
        NoTankBot,
        AlreadyActive,
        StartRejected,
    };

    struct Result
    {
        bool accepted = false;
        Error error = Error::None;
        std::string message;

        static Result Accepted(std::string message);
        static Result Rejected(Error error, std::string message);
        static Result FromStartAction(bool actionExecuted);
    };

    struct Snapshot
    {
        ObjectGuid groupGuid;
        ObjectGuid leaderGuid;
        std::uint32_t mapId = 0;
        bool active = false;
        bool paused = false;
        std::string phase;
        std::uint64_t stopSequence = 0;
        StopCause stopCause = StopCause::None;
        std::string stopReason;
    };

    // World-thread facade for integrations such as mod-raid-runner. The
    // implementation deliberately owns all playerbot/DungeonClear internals;
    // consumers only need Group, options and the stable result/snapshot types.
    class Controller
    {
    public:
        static Controller& Instance();

        Result StartForGroup(Group* group, StartOptions const& options = {});
        Result StopForGroup(Group* group, std::string const& reason = "controller stop");
        bool GetSnapshot(Group const* group, Snapshot& snapshot) const;
        void RecordStopped(Player* tank, StopCause cause, std::string const& reason);

    private:
        Controller() = default;
    };
}

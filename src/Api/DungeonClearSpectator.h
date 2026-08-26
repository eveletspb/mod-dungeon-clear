#pragma once

#include <string>

#include "ObjectGuid.h"

class Player;

namespace DungeonClear::Spectator
{
    struct Entrance
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float orientation = 0.0f;
    };

    struct WatchTarget
    {
        ObjectGuid playerGuid;
        Entrance entrance;
    };

    enum class Error
    {
        None,
        InvalidViewer,
        TargetUnavailable,
        CameraRejected,
        TeleportRejected,
    };

    struct Result
    {
        bool accepted = false;
        Error error = Error::None;
        std::string message;
    };

    // World-thread service shared by test and raid launchers. The caller owns
    // target selection; this service owns camera, instance bind, teleport and
    // return-position bookkeeping for the viewer.
    Result Start(Player* viewer, WatchTarget const& target);
    Result Stop(Player* viewer);
}

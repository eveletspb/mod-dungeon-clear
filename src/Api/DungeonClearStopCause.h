#pragma once

namespace DungeonClear
{
    enum class StopCause
    {
        None,
        Manual,
        Controller,
        Completed,
        Wipe,
        NoRezzer,
        ResurrectionTimeout,
        LeftInstance,
        Internal,
    };

    inline char const* StopCauseName(StopCause cause)
    {
        switch (cause)
        {
            case StopCause::None:                return "none";
            case StopCause::Manual:              return "manual";
            case StopCause::Controller:          return "controller";
            case StopCause::Completed:           return "completed";
            case StopCause::Wipe:                return "wipe";
            case StopCause::NoRezzer:             return "no-rezzer";
            case StopCause::ResurrectionTimeout: return "resurrection-timeout";
            case StopCause::LeftInstance:        return "left-instance";
            case StopCause::Internal:            return "internal";
        }

        return "unknown";
    }
}

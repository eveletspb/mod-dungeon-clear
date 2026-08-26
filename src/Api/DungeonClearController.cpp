#include "Api/DungeonClearController.h"

#include <mutex>
#include <unordered_map>
#include <utility>

#include "Event.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "Playerbots.h"
#include "Group.h"

#include "Ai/Dungeon/DungeonClear/Action/DcActionShared.h"
#include "Ai/Dungeon/DungeonClear/Util/DcLeaderSignal.h"
#include "Ai/Dungeon/DungeonClear/Util/DcRun.h"
#include "Ai/Dungeon/DungeonClear/DcValueKeys.h"

namespace DungeonClear
{
    ObjectGuid StartOptions::ResolveOwnerGuid(ObjectGuid groupLeaderGuid) const
    {
        return requesterGuid.IsEmpty() ? groupLeaderGuid : requesterGuid;
    }

    Result Result::Accepted(std::string message)
    {
        return {true, Error::None, std::move(message)};
    }

    Result Result::Rejected(Error error, std::string message)
    {
        return {false, error, std::move(message)};
    }

    Result Result::FromStartAction(bool actionExecuted)
    {
        return actionExecuted
            ? Accepted("dungeon clear start requested")
            : Rejected(Error::StartRejected, "dungeon clear start action was rejected");
    }

    Controller& Controller::Instance()
    {
        static Controller controller;
        return controller;
    }

    namespace
    {
        struct LifecycleRecord
        {
            ObjectGuid tankGuid;
            std::uint64_t stopSequence = 0;
            StopCause stopCause = StopCause::None;
            std::string stopReason;
        };

        std::mutex lifecycleMutex;
        std::unordered_map<ObjectGuid, LifecycleRecord> lifecycleByGroup;

        LifecycleRecord FindLifecycle(Group const* group)
        {
            std::lock_guard<std::mutex> lock(lifecycleMutex);
            auto const itr = lifecycleByGroup.find(group->GetGUID());
            return itr != lifecycleByGroup.end() ? itr->second : LifecycleRecord{};
        }

        void RecordStarted(Group const* group, ObjectGuid tankGuid)
        {
            std::lock_guard<std::mutex> lock(lifecycleMutex);
            LifecycleRecord& record = lifecycleByGroup[group->GetGUID()];
            record.tankGuid = tankGuid;
            record.stopCause = StopCause::None;
            record.stopReason.clear();
        }

        Player* ResolveTank(Group const* group)
        {
            if (!group)
                return nullptr;

            Player* leader = ObjectAccessor::FindConnectedPlayer(group->GetLeaderGUID());
            return leader ? DcLeaderSignal::FindLeaderTank(leader) : nullptr;
        }
    }

    Result Controller::StartForGroup(Group* group, StartOptions const& options)
    {
        if (!group)
            return Result::Rejected(Error::InvalidGroup, "group is not available");

        ObjectGuid const leaderGuid = group->GetLeaderGUID();
        Player* leader = ObjectAccessor::FindConnectedPlayer(leaderGuid);
        if (!leader)
            return Result::Rejected(Error::LeaderOffline, "raid leader is offline");

        Player* tank = DcLeaderSignal::FindLeaderTank(leader);
        if (!tank)
            return Result::Rejected(Error::NoTankBot, "no tank bot found in group");

        PlayerbotAI* botAI = GET_PLAYERBOT_AI(tank);
        if (!botAI)
            return Result::Rejected(Error::NoTankBot, "tank has no playerbot AI");

        if (DcRun::Of(botAI).enabled)
            return Result::Rejected(Error::AlreadyActive, "dungeon clear is already active");

        Player* owner = ObjectAccessor::FindConnectedPlayer(options.ResolveOwnerGuid(leaderGuid));
        if (!owner)
            return Result::Rejected(Error::LeaderOffline, "dungeon clear controller is offline");

        Result const result = Result::FromStartAction(
            botAI->DoSpecificAction("dc on", Event("controller", options.reason, owner), true));
        if (!result.accepted)
            return result;

        RecordStarted(group, tank->GetGUID());
        return result;
    }

    Result Controller::StopForGroup(Group* group, std::string const& reason)
    {
        if (!group)
            return Result::Rejected(Error::InvalidGroup, "group is not available");

        LifecycleRecord const lifecycle = FindLifecycle(group);
        Player* tank = lifecycle.tankGuid.IsEmpty()
            ? ResolveTank(group)
            : ObjectAccessor::FindConnectedPlayer(lifecycle.tankGuid);
        if (!tank)
            return Result::Rejected(Error::NoTankBot, "no tank bot found in group");

        PlayerbotAI* botAI = GET_PLAYERBOT_AI(tank);
        if (!botAI)
            return Result::Rejected(Error::NoTankBot, "tank has no playerbot AI");

        DcActionShared::DisableDungeonClear(botAI, reason, StopCause::Controller);
        return Result::Accepted("dungeon clear stop requested");
    }

    bool Controller::GetSnapshot(Group const* group, Snapshot& snapshot) const
    {
        if (!group)
            return false;

        snapshot = {};
        snapshot.groupGuid = group->GetGUID();

        LifecycleRecord const lifecycle = FindLifecycle(group);
        snapshot.stopSequence = lifecycle.stopSequence;
        snapshot.stopCause = lifecycle.stopCause;
        snapshot.stopReason = lifecycle.stopReason;

        Player* tank = lifecycle.tankGuid.IsEmpty()
            ? ResolveTank(group)
            : ObjectAccessor::FindConnectedPlayer(lifecycle.tankGuid);
        if (!tank)
            return !lifecycle.tankGuid.IsEmpty();

        PlayerbotAI* botAI = GET_PLAYERBOT_AI(tank);
        if (!botAI)
            return !lifecycle.tankGuid.IsEmpty();

        AiObjectContext* context = botAI->GetAiObjectContext();
        DcRunState const& run = DcRun::Of(context);
        snapshot.leaderGuid = tank->GetGUID();
        snapshot.mapId = tank->GetMapId();
        snapshot.active = run.enabled;
        snapshot.paused = run.paused;
        snapshot.phase = context->GetValue<std::string&>(DcKey::Phase)->Get();
        return true;
    }

    void Controller::RecordStopped(Player* tank, StopCause cause, std::string const& reason)
    {
        Group const* group = tank ? tank->GetGroup() : nullptr;
        if (!group)
            return;

        std::lock_guard<std::mutex> lock(lifecycleMutex);
        LifecycleRecord& record = lifecycleByGroup[group->GetGUID()];
        record.tankGuid = tank->GetGUID();
        ++record.stopSequence;
        record.stopCause = cause;
        record.stopReason = reason;
    }
}

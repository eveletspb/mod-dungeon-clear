#include "Api/DungeonClearSpectator.h"

#include <algorithm>
#include <unordered_map>
#include <utility>
#include <vector>

#include "InstanceSaveMgr.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "Player.h"

#include "Util/DcSpectator.h"
#include "Util/DcWatchHop.h"

namespace DungeonClear::Spectator
{
    namespace
    {
        std::unordered_map<ObjectGuid, std::vector<DcWatchHop::Bind>> watchBinds;

        Result Accepted(std::string message = {})
        {
            return {true, Error::None, std::move(message)};
        }

        Result Rejected(Error error, std::string message)
        {
            return {false, error, std::move(message)};
        }

        std::vector<DcWatchHop::Bind> HeldBinds(Player* viewer)
        {
            auto const itr = watchBinds.find(viewer->GetGUID());
            return itr == watchBinds.end()
                ? std::vector<DcWatchHop::Bind>{}
                : itr->second;
        }

        bool IsInside(Player* viewer, std::vector<DcWatchHop::Bind> const& binds)
        {
            return viewer->IsInWorld() &&
                std::any_of(binds.begin(), binds.end(), [viewer](DcWatchHop::Bind const& bind)
                {
                    return bind.mapId == viewer->GetMapId() &&
                        bind.instanceId == viewer->GetInstanceId();
                });
        }

        void ReleaseBinds(Player* viewer, std::vector<DcWatchHop::Bind> const& binds)
        {
            if (binds.empty())
                return;

            auto& held = watchBinds[viewer->GetGUID()];
            for (DcWatchHop::Bind const& bind : binds)
            {
                sInstanceSaveMgr->PlayerUnbindInstance(
                    viewer->GetGUID(), bind.mapId, Difficulty(bind.difficulty), true, viewer);
                held.erase(std::remove_if(held.begin(), held.end(),
                    [&bind](DcWatchHop::Bind const& candidate)
                    {
                        return candidate.mapId == bind.mapId &&
                            candidate.difficulty == bind.difficulty &&
                            candidate.instanceId == bind.instanceId;
                    }), held.end());
            }

            if (held.empty())
                watchBinds.erase(viewer->GetGUID());
        }

        void RestoreGmModeOnFailure(Player* viewer, bool watchOwnsGmMode)
        {
            DcSpectator::Stop(viewer);
            if (watchOwnsGmMode && viewer->IsGameMaster())
                viewer->SetGameMaster(false);
        }
    }

    Result Start(Player* viewer, WatchTarget const& requestedTarget)
    {
        if (!viewer)
            return Rejected(Error::InvalidViewer, "spectator viewer is not available");

        Player* target = ObjectAccessor::FindConnectedPlayer(requestedTarget.playerGuid);
        if (!target || !target->IsInWorld() || !target->GetMap())
            return Rejected(Error::TargetUnavailable, "spectator target is not in the world");

        Map* targetMap = target->GetMap();
        Difficulty const targetDifficulty = target->GetDifficulty(targetMap->IsRaid());
        InstanceSave* targetSave = sInstanceSaveMgr->GetInstanceSave(target->GetInstanceId());
        DcWatchHop::Bind const targetBind{
            target->GetMapId(),
            static_cast<uint8>(targetSave ? targetSave->GetDifficulty() : targetDifficulty),
            target->GetInstanceId()};

        uint32 boundInstanceId = 0;
        if (InstancePlayerBind* bind = sInstanceSaveMgr->PlayerGetBoundInstance(
                viewer->GetGUID(), targetBind.mapId, Difficulty(targetBind.difficulty)))
            if (bind->save)
                boundInstanceId = bind->save->GetInstanceId();

        std::vector<DcWatchHop::Bind> const held = HeldBinds(viewer);
        bool const ownedGmMode = DcSpectator::HiddenByWatch(viewer);
        bool const announceGmMode = !viewer->IsGameMaster();
        DcWatchHop::SessionPlan const session = DcWatchHop::DecideSession(
            {viewer->GetMapId(), viewer->GetInstanceId()}, targetBind,
            boundInstanceId, held, viewer->IsGameMaster(), ownedGmMode);

        DcSpectator::Stop(viewer);
        if (session.ownGmMode && !viewer->IsGameMaster())
            viewer->SetGameMaster(true);

        ReleaseBinds(viewer, session.hop.release);
        if (session.hop.bindToTarget && targetSave)
        {
            sInstanceSaveMgr->PlayerBindToInstance(
                viewer->GetGUID(), targetSave, !targetSave->CanReset(), viewer);
            watchBinds[viewer->GetGUID()].push_back(targetBind);
        }

        if (targetMap->IsRaid())
            viewer->SetRaidDifficulty(target->GetRaidDifficulty());
        else
            viewer->SetDungeonDifficulty(target->GetDungeonDifficulty());

        if (session.saveReturnPosition)
            viewer->SaveRecallPosition();

        if (session.hop.alreadyThere && viewer->IsInWorld())
        {
            DcSpectator::RequestFollowOnArrival(
                viewer, target->GetGUID(), session.ownGmMode);
            std::string reason;
            if (!DcSpectator::StartFollow(viewer, target, &reason))
            {
                RestoreGmModeOnFailure(viewer, session.ownGmMode);
                return Rejected(Error::CameraRejected, std::move(reason));
            }
            return Accepted(announceGmMode
                ? "GM mode enabled so the run does not see you; it will be restored when watching stops."
                : std::string{});
        }

        Entrance const& entrance = requestedTarget.entrance;
        if (!viewer->TeleportTo(target->GetMapId(), entrance.x, entrance.y, entrance.z,
                                entrance.orientation, 0, nullptr,
                                session.hop.forceNewInstance))
        {
            RestoreGmModeOnFailure(viewer, session.ownGmMode);
            if (session.hop.bindToTarget && targetSave)
                ReleaseBinds(viewer, {targetBind});
            return Rejected(Error::TeleportRejected,
                            "teleport into the watched instance was refused");
        }

        DcSpectator::RequestFollowOnArrival(
            viewer, target->GetGUID(), session.ownGmMode);
        return Accepted(announceGmMode
            ? "GM mode enabled so the run does not see you; it will be restored when watching stops."
            : std::string{});
    }

    Result Stop(Player* viewer)
    {
        if (!viewer)
            return Rejected(Error::InvalidViewer, "spectator viewer is not available");

        bool const wasActive = DcSpectator::IsActive(viewer);
        bool const ownedGmMode = DcSpectator::HiddenByWatch(viewer);
        DcSpectator::Stop(viewer);
        if (ownedGmMode && viewer->IsGameMaster())
            viewer->SetGameMaster(false);

        std::vector<DcWatchHop::Bind> const held = HeldBinds(viewer);
        bool const returnViewer = IsInside(viewer, held) && !viewer->IsBeingTeleported();
        if (returnViewer)
            viewer->TeleportTo(viewer->m_recallMap, viewer->m_recallX, viewer->m_recallY,
                               viewer->m_recallZ, viewer->m_recallO);
        ReleaseBinds(viewer, held);

        if (returnViewer)
            return Accepted("Returned to where you were before watching.");
        if (!wasActive)
            return Accepted("No spectator camera running; any pending watch request was cancelled.");
        return Accepted();
    }
}

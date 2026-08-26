/*
 * mod-dungeon-clear — DungeonClearCommand.cpp
 *
 * Slash command `.dc on|off|skip|status|bosses`. A convenience entry point that
 * works with zero config (unlike the chat keywords, which need the
 * "dungeon clear" strategy applied — see DungeonClearModule.cpp).
 *
 * Each subcommand dispatches the matching DungeonClear action ("dc on", …) to
 * the issuing player's tank bot(s) via PlayerbotAI::DoSpecificAction. The
 * actions already self-authorize (owner must be a real player in the bot's
 * group) and self-gate (e.g. `dc on` is tank-only), so we carry the issuing
 * player as the Event owner and let the existing action logic decide.
 */

#include "ScriptMgr.h"
#include "Chat.h"
#include "ChatCommand.h"
#include "Group.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "StringFormat.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <sstream>
#include <vector>

#include "PlayerbotAIConfig.h"
#include "Playerbots.h"

#include "DungeonClearDispatch.h"
#include "Api/DungeonClearSpectator.h"
#include "TestRun/DcTestDriver.h"
#include "TestRun/DcTestDungeonRegistry.h"
#include "TestRun/DcTestGearTiers.h"
#include "TestRun/DcTestPlan.h"
#include "TestRun/DcTestPlanManager.h"
#include "TestRun/DcTestRunManager.h"
#include "Util/DcSpectator.h"
#include "Ai/Dungeon/DungeonClear/Settings/DcSettings.h"
#include "Ai/Dungeon/DungeonClear/Settings/DcSettingsRegistry.h"
#include "Ai/Dungeon/DungeonClear/Util/DungeonClearUtil.h"


using namespace Acore::ChatCommands;

namespace
{
#if 0 // Moved to mod-raid-runner; kept temporarily for one source-level migration commit.
    struct DcRaidRndCandidate
    {
        ObjectGuid guid;
        uint8 classId = 0;
    };

    std::vector<DcRaidRndCandidate> LoadRndRaidCandidates(bool alliance)
    {
        std::vector<DcRaidRndCandidate> candidates;
        QueryResult accountResults = PlayerbotsDatabase.Query(
            "SELECT account_id FROM playerbots_account_type WHERE account_type = 1");
        if (!accountResults)
            return candidates;

        do
        {
            uint32 const accountId = accountResults->Fetch()[0].Get<uint32>();
            CharacterDatabasePreparedStatement* statement =
                CharacterDatabase.GetPreparedStatement(CHAR_SEL_CHARS_BY_ACCOUNT_ID);
            statement->SetData(0, accountId);
            PreparedQueryResult result = CharacterDatabase.Query(statement);
            if (!result)
                continue;

            do
            {
                Field* fields = result->Fetch();
                ObjectGuid const guid = ObjectGuid::Create<HighGuid::Player>(fields[0].Get<uint32>());
                uint8 const classId = fields[1].Get<uint8>();
                uint8 const race = fields[2].Get<uint8>();
                bool const candidateAlliance = race == 1 || race == 3 || race == 4 || race == 7 || race == 11;
                if (candidateAlliance != alliance || classId == 10 ||
                    sCharacterCache->GetCharacterLevelByGuid(guid) < 60 ||
                    ObjectAccessor::FindConnectedPlayer(guid))
                    continue;
                uint32 const guildId = sCharacterCache->GetCharacterGuildIdByGuid(guid);
                if (guildId && PlayerbotGuildMgr::instance().IsRealGuild(guildId))
                    continue;
                candidates.push_back({guid, classId});
            } while (result->NextRow());
        } while (accountResults->NextRow());

        std::mt19937 generator(std::random_device{}());
        std::shuffle(candidates.begin(), candidates.end(), generator);
        return candidates;
    }
#endif

    bool RunDcCommand(ChatHandler* handler, std::string const& action, std::string const& param = "")
    {
        Player* issuer = handler->GetSession() ? handler->GetSession()->GetPlayer() : nullptr;
        if (!issuer)
        {
            handler->SendSysMessage("This command must be used in-game.");
            return true;
        }

        if (!DungeonClearDispatch::DispatchToTankBots(issuer, action, param))
            handler->SendSysMessage("No tank bot found in your group.");

        return true;
    }

    // Format a resolved raw double per the registry type, so the printout reads
    // the way the conf line is written (true/false, ints, floats).
    std::string FormatDcValue(DcSettingDef const& d, double raw)
    {
        switch (d.type)
        {
            case DcType::Bool:
                return raw != 0.0 ? "true" : "false";
            case DcType::UInt:
            case DcType::Int:
                return Acore::StringFormat("{}", static_cast<int64>(std::lround(raw)));
            case DcType::Float:
            default:
                return Acore::StringFormat("{:.2f}", raw);
        }
    }

    // Dumps every DungeonClear tunable as the module actually reads it: the live
    // conf/default value, plus the per-run effective value when the issuer's run
    // has an addon override active. This is a pure read of sConfigMgr through the
    // DcSettings accessor, so it reflects exactly what the AI sees this tick —
    // use it to confirm whether a conf edit took effect (no `.reload config`).
    bool HandleConfig(ChatHandler* handler)
    {
        Player* issuer = handler->GetSession() ? handler->GetSession()->GetPlayer() : nullptr;
        if (!issuer)
        {
            handler->SendSysMessage("This command must be used in-game.");
            return true;
        }

        // Resolve the run owner (leader tank) so we can surface per-run overrides.
        // Empty when the issuer isn't in a DC run — then only conf/defaults show.
        Player* leader = DcLeaderSignal::FindLeaderTank(issuer);
        ObjectGuid const runOwner = leader ? leader->GetGUID() : ObjectGuid::Empty;

        handler->SendSysMessage(
            "DungeonClear config (effective values; * = addon override, H = heroic default):");
        for (DcSettingDef const& d : kDcSettings)
        {
            // confVal reads with no owner, so it never picks up the heroic layer:
            // it is the "what this would be outside the run" baseline both markers
            // are shown against. effVal is what the run is actually using.
            double const confVal = DcSettings::GetEffectiveRaw(ObjectGuid::Empty, d);
            double const effVal  = DcSettings::GetEffectiveRaw(runOwner, d);
            bool const overridden =
                !runOwner.IsEmpty() && DcSettings::HasOverride(runOwner, d.key);

            std::string line;
            if (overridden)
                line = Acore::StringFormat("  * DungeonClear.{} = {} (conf {})",
                                           d.key, FormatDcValue(d, effVal),
                                           FormatDcValue(d, confVal));
            else if (effVal != confVal)
                // Not overridden but not the conf value either: the run is heroic
                // and this row carries a heroic default. Without the marker the
                // number looks like the conf line is being ignored.
                line = Acore::StringFormat("  H DungeonClear.{} = {} (normal {})",
                                           d.key, FormatDcValue(d, effVal),
                                           FormatDcValue(d, confVal));
            else
                line = Acore::StringFormat("    DungeonClear.{} = {}",
                                           d.key, FormatDcValue(d, confVal));
            handler->SendSysMessage(line);
        }
        return true;
    }

    // Spectator camera toggle. Acts on the ISSUER directly (session plumbing,
    // not bot behavior) — it must NOT go through DispatchToTankBots or the
    // action pipeline: the issuer may not even be the tank, and the camera
    // belongs to their session alone. See Util/DcSpectator.h.
    //
    // Bare `.dc spectate` is the free-flying camera; `.dc spectate follow
    // [name]` rides a bot instead. From a live follow camera (which is what
    // `.dc test watch` leaves you in) bare `.dc spectate` steps INTO the free
    // camera at the bot you were watching rather than ending the camera — press
    // it again to stop. Neither needs a group — that gate lives only
    // in the addon's party-channel transport, which is why a GM watching from
    // outside the party has to type the command.
    bool HandleSpectate(ChatHandler* handler, Optional<std::string> param)
    {
        Player* issuer = handler->GetSession() ? handler->GetSession()->GetPlayer() : nullptr;
        if (!issuer)
        {
            handler->SendSysMessage("This command must be used in-game.");
            return true;
        }

        std::string arg = param ? *param : "";
        std::string sub;
        std::string name;
        {
            std::istringstream in(arg);
            in >> sub >> name;
        }
        std::transform(sub.begin(), sub.end(), sub.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

        std::string whyNot;

        // Seat selection. `next`/`prev` walk every bot in the instance, not just
        // tanks — one key to start watching and to move down the party.
        if (sub == "next" || sub == "n")
        {
            if (!DcSpectator::CycleFollow(issuer, +1, &whyNot))
                handler->SendSysMessage(whyNot);
            return true;
        }
        if (sub == "prev" || sub == "previous" || sub == "p")
        {
            if (!DcSpectator::CycleFollow(issuer, -1, &whyNot))
                handler->SendSysMessage(whyNot);
            return true;
        }
        if (sub == "list" || sub == "who")
        {
            handler->SendSysMessage(DcSpectator::RosterText(issuer));
            return true;
        }

        if (sub == "follow")
        {
            // Named seat: switch to it, whether or not a camera is already up.
            // Bare `follow` stays a toggle — that is how you turn the camera off.
            if (!name.empty())
            {
                Player* target = DcSpectator::FindWatchableByName(issuer, name);
                if (!target)
                {
                    handler->PSendSysMessage(
                        "No watchable bot matching '{}' here. Try `.dc spectate list`.", name);
                    return true;
                }
                if (!DcSpectator::SeatFollow(issuer, target, &whyNot))
                    handler->SendSysMessage(whyNot);
                return true;
            }

            if (!DcSpectator::ToggleFollow(issuer, nullptr, &whyNot))
                handler->SendSysMessage(whyNot);
            return true;
        }

        if (!sub.empty() && sub != "free")
        {
            handler->SendSysMessage(
                "Usage: .dc spectate [follow [name] | next | prev | list]");
            return true;
        }

        if (!DcSpectator::Toggle(issuer, &whyNot))
            handler->SendSysMessage(whyNot);
        return true;
    }

}

class dungeon_clear_command_script : public CommandScript
{
public:
    dungeon_clear_command_script() : CommandScript("dungeon_clear_command_script") {}

    ChatCommandTable GetCommands() const override
    {
        // Console::Yes across `.dc test`: a console (or dashboard screen-
        // bridge) start resolves its issuing GM to the headless driver
        // character — see DcTestDriver and ResolveTestIssuer.
        static ChatCommandTable dcTestPlanTable =
        {
            { "start",  HandleTestPlanStart,  SEC_GAMEMASTER, Console::Yes },
            { "status", HandleTestPlanStatus, SEC_GAMEMASTER, Console::Yes },
            { "stop",   HandleTestPlanStop,   SEC_GAMEMASTER, Console::Yes },
        };
    static ChatCommandTable dcTestTable =
        {
            { "start",  HandleTestStart,  SEC_GAMEMASTER, Console::Yes },
            { "status", HandleTestStatus, SEC_GAMEMASTER, Console::Yes },
            { "stop",   HandleTestStop,   SEC_GAMEMASTER, Console::Yes },
            { "list",   HandleTestList,   SEC_GAMEMASTER, Console::Yes },
            { "gear",   HandleTestGear,   SEC_GAMEMASTER, Console::Yes },
            // Console::No — a camera needs a session to attach to.
            { "watch",  HandleTestWatch,  SEC_GAMEMASTER, Console::No },
            { "plan",   dcTestPlanTable },
        };
        static ChatCommandTable dcTable =
        {
            { "on",     HandleOn,     SEC_PLAYER, Console::No },
            { "off",    HandleOff,    SEC_PLAYER, Console::No },
            { "skip",   HandleSkip,   SEC_PLAYER, Console::No },
            { "pause",  HandlePause,  SEC_PLAYER, Console::No },
            { "pull",   HandlePull,   SEC_PLAYER, Console::No },
            { "status", HandleStatus, SEC_PLAYER, Console::No },
            { "bosses", HandleBosses, SEC_PLAYER, Console::No },
            { "go",     HandleGo,     SEC_PLAYER, Console::No },
            { "config", HandleConfig, SEC_PLAYER, Console::No },
            { "spectate", HandleSpectate, SEC_PLAYER, Console::No },
            { "test",   dcTestTable },
        };
        static ChatCommandTable root = { { "dc", dcTable } };
        return root;
    }

    static bool HandleOn(ChatHandler* handler)     { return RunDcCommand(handler, "dc on"); }
    static bool HandleOff(ChatHandler* handler)    { return RunDcCommand(handler, "dc off"); }
    static bool HandleSkip(ChatHandler* handler)   { return RunDcCommand(handler, "dc skip"); }
    static bool HandlePause(ChatHandler* handler)  { return RunDcCommand(handler, "dc pause"); }
    static bool HandlePull(ChatHandler* handler, Optional<std::string> param) { return RunDcCommand(handler, "dc pull", param ? *param : ""); }
    static bool HandleStatus(ChatHandler* handler, Optional<std::string> param) { return RunDcCommand(handler, "dc status", param ? *param : ""); }
    static bool HandleBosses(ChatHandler* handler, Optional<std::string> param) { return RunDcCommand(handler, "dc bosses", param ? *param : ""); }
    static bool HandleGo(ChatHandler* handler, Tail targetBoss) { return RunDcCommand(handler, "dc go", std::string(targetBoss)); }

#if 0 // Moved to mod-raid-runner; command registration was removed in 686278c.
    static bool HandleRaidStart(ChatHandler* handler, Tail args)
    {
        Player* issuer = ResolveTestIssuer(handler);
        std::istringstream input{std::string(args)};
        std::string requestId;
        std::string partySpec;
        bool manifestMode = false;
        input >> requestId;
        std::string option;
        while (input >> option)
        {
            if (option.rfind("party=", 0) == 0)
                partySpec = option.substr(6);
            else if (option == "manifest")
                manifestMode = true;
            else
            {
                handler->SendSysMessage(
                    "Usage: .dc raid start <requestId> [party=Tank,OffTank,Heal1,Heal2,"
                    "Dps1,Dps2,Dps3,Dps4,Dps5,Dps6] [manifest]");
                return true;
            }
        }
        if (!issuer)
            return true;
        if (requestId.empty())
        {
            handler->SendSysMessage(
                "Usage: .dc raid start <requestId> [party=Tank,OffTank,Heal1,Heal2,"
                "Dps1,Dps2,Dps3,Dps4,Dps5,Dps6] [manifest]");
            return true;
        }

        static DcRaidLaunchAdmission::Coordinator coordinator(
            std::getenv("DC_RAID_INBOX") ? std::getenv("DC_RAID_INBOX") : "dc_raid_requests");
        DcRaidLaunchAdmission::Result result;
        if (manifestMode && !partySpec.empty())
        {
            handler->SendSysMessage("party= and manifest cannot be combined.");
            return true;
        }
        if (manifestMode)
            result = coordinator.Accept(requestId, issuer);
        else
        {
            std::vector<std::string> names;
            if (!partySpec.empty())
            {
                std::istringstream party{partySpec};
                std::string name;
                while (std::getline(party, name, ','))
                    if (!name.empty())
                        names.push_back(name);
            }
            else
            {
                bool const alliance = issuer->GetTeamId(true) == TEAM_ALLIANCE;
                std::unordered_set<std::uint64_t> const reserved = coordinator.ReservedGuids();
                std::array<std::vector<uint8>, 10> const roleClasses = {
                    std::vector<uint8>{1, 2, 11, 7, 6},
                    std::vector<uint8>{1, 2, 11, 7, 6},
                    std::vector<uint8>{5, 7, 2, 11},
                    std::vector<uint8>{5, 7, 2, 11},
                    std::vector<uint8>{1, 2, 3, 4, 5, 6, 7, 8, 9, 11},
                    std::vector<uint8>{1, 2, 3, 4, 5, 6, 7, 8, 9, 11},
                    std::vector<uint8>{1, 2, 3, 4, 5, 6, 7, 8, 9, 11},
                    std::vector<uint8>{1, 2, 3, 4, 5, 6, 7, 8, 9, 11},
                    std::vector<uint8>{1, 2, 3, 4, 5, 6, 7, 8, 9, 11},
                    std::vector<uint8>{1, 2, 3, 4, 5, 6, 7, 8, 9, 11},
                };
                std::unordered_set<std::uint64_t> selected;
                std::vector<DcRaidRndCandidate> const rndCandidates = LoadRndRaidCandidates(alliance);
                for (std::vector<uint8> const& classes : roleClasses)
                {
                    ObjectGuid picked;
                    for (uint8 classId : classes)
                    {
                        auto const& pool = sRandomPlayerbotMgr.addclassCache[
                            RandomPlayerbotMgr::GetTeamClassIdx(alliance, classId)];
                        for (ObjectGuid const& guid : pool)
                        {
                            std::uint64_t const value = guid.GetCounter();
                            if (reserved.count(value) || selected.count(value) ||
                                ObjectAccessor::FindConnectedPlayer(guid) ||
                                sCharacterCache->GetCharacterLevelByGuid(guid) < 60)
                                continue;
                            uint32 const guildId = sCharacterCache->GetCharacterGuildIdByGuid(guid);
                            if (guildId && PlayerbotGuildMgr::instance().IsRealGuild(guildId))
                                continue;
                            picked = guid;
                            break;
                        }
                        if (picked)
                            break;
                    }
                    if (!picked)
                    {
                        for (uint8 classId : classes)
                        {
                            for (DcRaidRndCandidate const& candidate : rndCandidates)
                                if (candidate.classId == classId &&
                                    !reserved.count(candidate.guid.GetCounter()) &&
                                    !selected.count(candidate.guid.GetCounter()))
                                {
                                    picked = candidate.guid;
                                    break;
                                }
                            if (picked)
                                break;
                        }
                    }
                    if (!picked || !sCharacterCache->GetCharacterNameByGuid(picked, partySpec))
                    {
                        handler->SendSysMessage(
                            "Not enough available addclass/RND characters for Molten Core.");
                        return true;
                    }
                    selected.insert(picked.GetCounter());
                    names.push_back(partySpec);
                }
            }

            if (names.size() != 10)
            {
                handler->SendSysMessage("Molten Core party= currently requires exactly 10 names.");
                return true;
            }

            DcRaidLaunchRequest::Request request;
            request.schema = 1;
            request.requestId = requestId;
            request.raidToken = "molten-core";
            request.members.reserve(names.size());
            for (std::size_t index = 0; index < names.size(); ++index)
            {
                normalizePlayerName(names[index]);
                ObjectGuid const guid = sCharacterCache->GetCharacterGuidByName(names[index]);
                if (!guid)
                {
                    handler->SendSysMessage("Unknown character: " + names[index]);
                    return true;
                }

                DcRaidLaunchRequest::Role role = DcRaidLaunchRequest::Role::Dps;
                if (index == 0)
                    role = DcRaidLaunchRequest::Role::MainTank;
                else if (index == 1)
                    role = DcRaidLaunchRequest::Role::OffTank;
                else if (index < 4)
                    role = DcRaidLaunchRequest::Role::Healer;

                request.members.push_back({guid.GetCounter(), names[index], role,
                                           static_cast<std::uint32_t>(index / 5 + 1)});
            }
            result = coordinator.Accept(requestId, request, issuer);
        }
        if (!result.ok)
        {
            handler->SendSysMessage(Acore::StringFormat("Raid request rejected [{}]: {}",
                                                        static_cast<int>(result.stage), result.message));
            return true;
        }

        if (!DungeonClear::RaidProvisioning::Enqueue(
                result.request, issuer->GetSession() ? issuer->GetSession()->GetAccountId() : 0,
                issuer->GetGUID()))
        {
            handler->SendSysMessage("Raid request accepted but provisioning job already exists.");
            return true;
        }

        handler->SendSysMessage(Acore::StringFormat(
            "Raid request accepted: {} ({} members). Provisioning job queued.",
            result.request.requestId, result.request.members.size()));
        return true;
    }

    static bool HandleRaidStatus(ChatHandler* handler, Tail args)
    {
        std::istringstream input{std::string(args)};
        std::string requestId;
        input >> requestId;
        if (requestId.empty())
        {
            handler->SendSysMessage("Usage: .dc raid status <requestId>");
            return true;
        }

        DungeonClear::RaidProvisioning::Job job;
        if (!DungeonClear::RaidProvisioning::Find(requestId, job))
        {
            handler->SendSysMessage("Provisioning job not found: " + requestId);
            return true;
        }

        char const* state = job.state == DungeonClear::RaidProvisioning::State::Queued ? "queued" :
            job.state == DungeonClear::RaidProvisioning::State::LoggingIn ? "logging-in" :
            job.state == DungeonClear::RaidProvisioning::State::Ready ? "ready" : "failed";
        if (job.state == DungeonClear::RaidProvisioning::State::GroupReady)
            state = "group-ready";
        else if (job.state == DungeonClear::RaidProvisioning::State::Teleporting)
            state = "teleporting";
        else if (job.state == DungeonClear::RaidProvisioning::State::AtEntrance)
            state = "at-entrance";
        handler->SendSysMessage(Acore::StringFormat(
            "Raid provisioning {}: {} ({}/{} online). {}",
            job.requestId, state, job.onlineCount, job.memberCount,
            job.message.empty() ? (job.state == DungeonClear::RaidProvisioning::State::AtEntrance ?
                "raid roster is on Molten Core map; Dungeon Clear is not started yet." :
                job.state == DungeonClear::RaidProvisioning::State::GroupReady ?
                "group created; teleport and Dungeon Clear are not started yet." :
                "group/login/teleport are not started yet.") : job.message));
        return true;
    }
#endif

    // --- `.dc test` — the automated test-run harness ------------------------
    // These act on DcTestRunManager directly (never DispatchToTankBots: the
    // whole point is that the GM is NOT in the bot party).

    // The issuing GM for a start: the in-game player when there is one, else
    // the headless driver (console / dashboard path). nullptr with a pending
    // message sent when the driver is still logging in — the caller retries.
    static Player* ResolveTestIssuer(ChatHandler* handler)
    {
        if (Player* issuer = handler->GetSession() ? handler->GetSession()->GetPlayer() : nullptr)
            return issuer;

        std::string whyPending;
        if (DcTestDriver::EnsureOnline(&whyPending))
            return DcTestDriver::Get();
        handler->SendSysMessage(whyPending);
        return nullptr;
    }

    // `.dc test start <dungeon> [heroic] [level=N] [seed=N] [ilvl=N|none]
    // [quality=rare|epic|…]` — random comp drawn from the addclass pool;
    // dungeon is a registry token (`.dc test list`) or a mapId. ilvl/quality cap
    // the gear the bots are rolled with, defaulting to the AiPlayerbot.AutoGear*
    // conf values; `.dc test gear <dungeon>` lists the ceilings worth using for
    // a given dungeon.
    //
    // `.dc test start <dungeon> party=Tank,Heal,D1,D2,D3 [heroic]` — a hand-picked
    // party of REAL player characters instead (roles positional). level=, seed=
    // and the gear options are all meaningless there: the level comes from the
    // characters, the roster is the comp, and real characters are never re-geared.
    static bool HandleTestStart(ChatHandler* handler, Tail args)
    {
        Player* issuer = ResolveTestIssuer(handler);
        if (!issuer)
            return true;

        static constexpr char const* kUsage =
            "Usage: .dc test start <dungeon> [heroic] [level=N] [seed=N] [ilvl=N|none] "
            "[quality=normal|uncommon|rare|epic|legendary]\n"
            "   or: .dc test start <dungeon> party=Tank,Heal,Dps1,Dps2,Dps3 [heroic]";

        std::string token;
        std::string party;
        uint32 level = 0;
        uint32 seed = 0;  // 0 = roll a random comp; seed=N replays a specific one
        DcTestGearTiers::Spec gear;
        bool heroic = false;
        std::istringstream in{std::string(args)};
        std::string word;
        while (in >> word)
        {
            if (word.rfind("level=", 0) == 0)
                level = static_cast<uint32>(std::strtoul(word.c_str() + 6, nullptr, 10));
            else if (word.rfind("seed=", 0) == 0)
                seed = static_cast<uint32>(std::strtoul(word.c_str() + 5, nullptr, 10));
            else if (word.rfind("ilvl=", 0) == 0)
            {
                bool ok = false;
                gear.ilvl = DcTestGearTiers::ParseIlvl(word.substr(5), &ok);
                if (!ok)
                {
                    handler->SendSysMessage("ilvl must be 1-400, or 'none' for no limit.");
                    return true;
                }
            }
            else if (word.rfind("quality=", 0) == 0)
            {
                gear.quality = DcTestGearTiers::ParseQuality(word.substr(8));
                if (gear.quality == 0)
                {
                    handler->SendSysMessage(
                        "quality must be normal|uncommon|rare|epic|legendary (or 1-5).");
                    return true;
                }
            }
            else if (word.rfind("party=", 0) == 0)
                party = word.substr(6);
            else if (word == "heroic")
                heroic = true;
            else if (token.empty())
                token = word;
            else
            {
                handler->SendSysMessage(kUsage);
                return true;
            }
        }
        if (token.empty())
        {
            handler->SendSysMessage(kUsage);
            return true;
        }

        std::string msg;
        if (!party.empty())
        {
            // Reject rather than silently ignore: somebody passing level= with a
            // roster believes it will be applied, and applying it would mean
            // relevelling their character.
            if (level || seed || !gear.IsDefault())
            {
                handler->SendSysMessage(
                    "level=, seed= and ilvl=/quality= do not apply to party= runs: the level comes "
                    "from the characters (they are never relevelled or re-geared) and the roster "
                    "is the comp.");
                return true;
            }
            DcTestRunManager::Instance().StartRoster(issuer, token, party, heroic, &msg);
        }
        else
            DcTestRunManager::Instance().Start(issuer, token, level, seed, heroic, gear, &msg);
        handler->SendSysMessage(msg);
        return true;
    }

    static bool HandleTestStatus(ChatHandler* handler)
    {
        handler->SendSysMessage(DcTestRunManager::Instance().StatusText());
        if (DcTestPlanManager::Instance().HasActivePlans())
            handler->SendSysMessage(DcTestPlanManager::Instance().StatusText());
        return true;
    }

    // `.dc test stop [selector]` — bare = the single active run (errors listing
    // runs when >1 active); "all"; an exact runId; or a dungeon token (all its
    // runs). See DcTestRunSelect. "all" also stops every active plan first —
    // otherwise the plan scheduler would relaunch the runs it just aborted.
    static bool HandleTestStop(ChatHandler* handler, Tail selector)
    {
        if (std::string(selector) == "all" && DcTestPlanManager::Instance().HasActivePlans())
        {
            DcTestPlanManager::Instance().StopAll("stopped via .dc test stop all");
            handler->SendSysMessage("stopping all test plans");
        }
        std::string msg;
        DcTestRunManager::Instance().Stop(std::string(selector), &msg);
        handler->SendSysMessage(msg);
        return true;
    }

    // `.dc test watch [selector]` — put the GM's camera on a running test.
    //
    // This is the one-command answer to "let me watch a run": the GM is NOT in
    // the bot party (by design — see DcTestRunManager), so watching used to
    // mean hand-running `.appear <botname>` then `.dc spectate`, and the addon
    // button refuses outright because its transport is the party channel.
    //
    // Target selection and entrance lookup stay here. The generic spectator
    // service hides the GM, owns bind/teleport/return bookkeeping and arms the
    // follow camera after the asynchronous teleport. `.dc test watch off`
    // delegates the complete teardown to that same service.
    //
    // Watching a SECOND run is the same command again, with no manual cleanup
    // in between: DcWatchHop works out which binds have to be released and
    // whether the teleport must be forced far, because the bind made for the
    // previous run would otherwise decide where this teleport lands. See
    // Util/DcWatchHop.h for why each of those is load-bearing.
    //
    // `.dc test watch next` hops to the run after the one being watched and
    // wraps, so a plan's worth of concurrent runs can be toured on one command
    // without reading runIds off `.dc test status`. It refuses (and stays put)
    // when there is nowhere to go — see DcTestRunManager::NextWatchTarget.
    static bool HandleTestWatch(ChatHandler* handler, Tail selectorArg)
    {
        Player* gm = handler->GetSession() ? handler->GetSession()->GetPlayer() : nullptr;
        if (!gm)
        {
            handler->SendSysMessage("This command must be used in-game.");
            return true;
        }

        std::string selector(selectorArg);
        if (selector == "off" || selector == "stop")
        {
            DungeonClear::Spectator::Result const result =
                DungeonClear::Spectator::Stop(gm);
            if (!result.message.empty())
                handler->SendSysMessage(result.message);
            return true;
        }

        ObjectGuid tankGuid;
        std::string msg;
        std::string dungeonToken;
        // "next" tours the live runs instead of naming one: the run after the
        // instance the watcher is standing in, wrapping. It resolves to a
        // target like any other selector, so the hop below is unchanged.
        bool const resolved =
            selector == "next"
                ? DcTestRunManager::Instance().NextWatchTarget(gm, &tankGuid, &msg, &dungeonToken)
                : DcTestRunManager::Instance().WatchTarget(selector, &tankGuid, &msg, &dungeonToken);
        if (!resolved)
        {
            handler->SendSysMessage(msg);
            return true;
        }

        Player* tank = ObjectAccessor::FindConnectedPlayer(tankGuid);
        if (!tank || !tank->IsInWorld())
        {
            handler->SendSysMessage("That run's tank isn't in the world yet — try again in a moment.");
            return true;
        }

        // Land at the instance ENTRANCE, not on the tank. Farsight renders from
        // the seer's position, so the camera looks the same either way — but
        // dropping the GM's body into the middle of a live pull puts it in
        // collision range of the party (and of anything the pull picks up), and
        // leaves it standing there once the camera stops. The entrance is the
        // quiet, already-cleared end of the instance.
        //
        // Preference order: the run's own registry row (per-WING for split maps
        // like Dire Maul, where a bare map lookup can't tell the wings apart),
        // then the map's entrance areatrigger, then the tank as a last resort.
        DungeonClear::Spectator::Entrance entrance{
            tank->GetPositionX(), tank->GetPositionY(),
            tank->GetPositionZ(), tank->GetOrientation()};
        if (DcTestDungeonRegistry::Row const* row = DcTestDungeonRegistry::Find(dungeonToken);
            row && row->mapId == tank->GetMapId())
        {
            entrance = {row->x, row->y, row->z, row->o};
        }
        else if (AreaTriggerTeleport const* at = sObjectMgr->GetMapEntranceTrigger(tank->GetMapId()))
        {
            entrance = {at->target_X, at->target_Y, at->target_Z, at->target_Orientation};
        }

        DungeonClear::Spectator::Result const result =
            DungeonClear::Spectator::Start(gm, {tankGuid, entrance});
        if (!result.accepted)
        {
            handler->SendSysMessage(result.message);
            return true;
        }

        handler->PSendSysMessage("Watching {}", msg);
        if (!result.message.empty())
            handler->SendSysMessage(result.message);
        return true;
    }

    // --- `.dc test plan` — batched campaigns (N runs, capped concurrency) ----

    // Unlike `.dc test start`, a plan does NOT need its issuer up front: the
    // scheduler re-resolves one per launch and waits out an in-flight driver
    // login. That matters because the very first console/dashboard start is
    // the click that kicks that login off — requiring an issuer here rejected
    // exactly the request that caused the driver to come online.
    static bool HandleTestPlanStart(ChatHandler* handler, Tail args)
    {
        Player* issuer = handler->GetSession() ? handler->GetSession()->GetPlayer() : nullptr;
        if (!issuer)
        {
            std::string why;
            DcTestDriver::Readiness const ready = DcTestDriver::Ensure(&why);
            if (ready == DcTestDriver::Readiness::Unavailable)
            {
                handler->SendSysMessage("Test plan not started: " + why);
                return true;
            }
            issuer = DcTestDriver::Get();  // nullptr while the login is in flight
        }

        DcTestPlan::ParseResult const parsed = DcTestPlan::ParseStartArgs(std::string(args));
        if (!parsed.ok)
        {
            handler->SendSysMessage(parsed.err);
            return true;
        }

        std::string msg;
        DcTestPlanManager::Instance().Start(parsed.spec, issuer, &msg);
        handler->SendSysMessage(msg);
        return true;
    }

    static bool HandleTestPlanStatus(ChatHandler* handler)
    {
        handler->SendSysMessage(DcTestPlanManager::Instance().StatusText());
        return true;
    }

    // `.dc test plan stop [planId|all]` — bare = the single active plan.
    static bool HandleTestPlanStop(ChatHandler* handler, Tail selector)
    {
        std::string msg;
        DcTestPlanManager::Instance().Stop(std::string(selector), &msg);
        handler->SendSysMessage(msg);
        return true;
    }

    static bool HandleTestList(ChatHandler* handler)
    {
        handler->SendSysMessage("Supported test dungeons (.dc test start <token> [heroic]):");
        for (DcTestDungeonRegistry::Row const& row : DcTestDungeonRegistry::All())
            handler->SendSysMessage(Acore::StringFormat(
                "  {:<16} {} (map {}, level {}{})", row.token, row.name, row.mapId,
                row.recommendedLevel,
                row.heroicLevel ? Acore::StringFormat(", heroic {}", row.heroicLevel)
                                : std::string()));
        return true;
    }

    // `.dc test gear <dungeon> [heroic]` — the item-level ceilings worth running
    // that dungeon at, i.e. the same list the dashboard's start form offers.
    // Named raid tiers at the level cap, three steps around the dungeon's own
    // gear below it (see DcTestGearTiers).
    static bool HandleTestGear(ChatHandler* handler, Tail args)
    {
        std::string token;
        bool heroic = false;
        std::istringstream in{std::string(args)};
        std::string word;
        while (in >> word)
        {
            if (word == "heroic")
                heroic = true;
            else if (token.empty())
                token = word;
        }

        DcTestDungeonRegistry::Row const* row = DcTestDungeonRegistry::Find(token);
        if (!row)
        {
            handler->SendSysMessage("Usage: .dc test gear <dungeon> [heroic] — see .dc test list");
            return true;
        }
        if (heroic && row->heroicLevel == 0)
        {
            handler->SendSysMessage(Acore::StringFormat("'{}' has no heroic mode.", row->token));
            return true;
        }

        uint32 const level = heroic ? row->heroicLevel : row->recommendedLevel;
        handler->SendSysMessage(Acore::StringFormat(
            "{}{} at level {} — ilvl= choices (server default is {}):", row->name,
            heroic ? " heroic" : "", level,
            sPlayerbotAIConfig.autoGearScoreLimit > 0
                ? std::to_string(sPlayerbotAIConfig.autoGearScoreLimit)
                : std::string("unlimited")));
        for (DcTestGearTiers::Choice const& choice : DcTestGearTiers::Ladder(row->mapId, level))
            handler->SendSysMessage(Acore::StringFormat("  ilvl={:<4} {}", choice.ilvl, choice.label));
        handler->SendSysMessage("  ilvl=none  no limit");
        return true;
    }
};

void AddSC_dungeon_clear_command()
{
    new dungeon_clear_command_script();
}

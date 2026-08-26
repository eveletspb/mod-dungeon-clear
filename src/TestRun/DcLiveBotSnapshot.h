#pragma once

#include <string>
#include <utility>

#include "TestRun/DcTestRunLiveJson.h"
#include "Player.h"

namespace DcTestRunLive
{
    inline BotPos CaptureBot(Player* player, std::string role, uint8 classId)
    {
        BotPos bot;
        bot.role = std::move(role);
        bot.name = player->GetName();
        bot.classId = classId;
        bot.x = player->GetPositionX();
        bot.y = player->GetPositionY();
        bot.z = player->GetPositionZ();
        bot.alive = player->IsAlive();
        bot.hp = static_cast<std::uint8_t>(bot.alive ? player->GetHealthPct() : 0.f);
        if (bot.alive && player->GetMaxPower(POWER_MANA) > 0)
            bot.mp = static_cast<std::int16_t>(player->GetPowerPct(POWER_MANA));
        bot.inCombat = player->IsInCombat();
        return bot;
    }
}

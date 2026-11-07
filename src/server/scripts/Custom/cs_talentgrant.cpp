/*
 * .talents show|grant|take|set: talent points given to one character, on top of the ones from levels.
 *
 * The Classic client works out its own talent point total from TraitCurrencySource (currency 3820), so a
 * server-side counter would never show up there. Instead each bonus point is a hidden marker quest: the
 * custom SQL adds a source "1 point for quest N" for each of them, and raises the currency's cap. Granting
 * a point completes the next marker quest, which the client sees through the quest-completed bits. Completed
 * quests are saved with the character, so the points survive level-ups, relogs and talent resets.
 */

#include "Chat.h"
#include "ChatCommand.h"
#include "MapUtils.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "TraitMgr.h"
#include "TraitPacketsCommon.h"
#include "UpdateFields.h"
#include <map>

using namespace Trinity::ChatCommands;

namespace
{
// Must match custom-sql/024_talent_grant.sql and 025_talent_grant_hotfix.sql.
constexpr uint32 FIRST_MARKER_QUEST = 9100001;
constexpr uint32 MARKER_QUEST_COUNT = 249;  // 51 points from levels + 249 = the cap of 300
constexpr int32 CLASSIC_TALENT_CURRENCY = 3820;

uint32 GetGranted(Player const* player)
{
    uint32 granted = 0;
    for (uint32 i = 0; i < MARKER_QUEST_COUNT; ++i)
        if (player->IsQuestRewarded(FIRST_MARKER_QUEST + i))
            ++granted;
    return granted;
}

// Talent points the character owns and has spent, from its active talent configuration.
void GetTalentPoints(Player const* player, int32& owned, int32& spent)
{
    owned = 0;
    spent = 0;
    UF::TraitConfig const* config = player->GetTraitConfig(player->m_activePlayerData->ActiveCombatTraitConfigID);
    if (!config)
        return;

    WorldPackets::Traits::TraitConfig traitConfig(*config);
    std::map<int32, int32> ownedCurrencies;
    TraitMgr::FillOwnedCurrenciesMap(traitConfig, player, ownedCurrencies);
    std::map<int32, TraitMgr::SpentCurrency> spentCurrencies;
    TraitMgr::FillSpentCurrenciesMap(traitConfig.Entries, spentCurrencies);

    if (int32 const* amount = Trinity::Containers::MapGetValuePtr(ownedCurrencies, CLASSIC_TALENT_CURRENCY))
        owned = *amount;
    if (TraitMgr::SpentCurrency const* amount = Trinity::Containers::MapGetValuePtr(spentCurrencies, CLASSIC_TALENT_CURRENCY))
        spent = amount->Total;
}

void ReportPoints(ChatHandler* handler, Player const* player)
{
    int32 owned, spent;
    GetTalentPoints(player, owned, spent);
    handler->PSendSysMessage("%s has %u granted talent points (of %u possible). Talent points: %d total, %d spent, %d unspent.",
        player->GetName().c_str(), GetGranted(player), MARKER_QUEST_COUNT, owned, spent, owned - spent);
}

Player* GetTarget(ChatHandler* handler, Optional<PlayerIdentifier>& player)
{
    if (!player)
        player = PlayerIdentifier::FromTargetOrSelf(handler);
    if (!player)
        return nullptr;

    if (!player->IsConnected())
    {
        handler->PSendSysMessage("%s is not online. Talent points can only be granted to an online character.", player->GetName().c_str());
        handler->SetSentErrorMessage(true);
        return nullptr;
    }
    return player->GetConnectedPlayer();
}

// Moves the character to exactly `target` granted points, adding from the lowest marker quest and
// removing from the highest. Refuses to take away points that are already spent.
bool SetGranted(ChatHandler* handler, Player* player, uint32 target)
{
    if (target > MARKER_QUEST_COUNT)
    {
        handler->PSendSysMessage("At most %u talent points can be granted.", MARKER_QUEST_COUNT);
        handler->SetSentErrorMessage(true);
        return false;
    }

    uint32 granted = GetGranted(player);
    if (target < granted)
    {
        int32 owned, spent;
        GetTalentPoints(player, owned, spent);
        int32 removed = int32(granted - target);
        if (owned - removed < spent)
        {
            handler->PSendSysMessage("%s has spent %d of %d talent points, so %d granted points cannot be taken away. Reset their talents first (.reset talents).",
                player->GetName().c_str(), spent, owned, removed);
            handler->SetSentErrorMessage(true);
            return false;
        }

        for (uint32 i = MARKER_QUEST_COUNT; i-- > 0 && granted > target;)
        {
            if (!player->IsQuestRewarded(FIRST_MARKER_QUEST + i))
                continue;
            player->RemoveRewardedQuest(FIRST_MARKER_QUEST + i);
            --granted;
        }
    }
    else
    {
        for (uint32 i = 0; i < MARKER_QUEST_COUNT && granted < target; ++i)
        {
            if (player->IsQuestRewarded(FIRST_MARKER_QUEST + i))
                continue;
            player->SetRewardedQuest(FIRST_MARKER_QUEST + i);
            ++granted;
        }
    }

    player->SaveToDB();
    ReportPoints(handler, player);
    return true;
}
}

class talentgrant_commandscript : public CommandScript
{
public:
    talentgrant_commandscript() : CommandScript("talentgrant_commandscript") { }

    std::span<ChatCommandBuilder const> GetCommands() const override
    {
        static ChatCommandTable talentsCommandTable =
        {
            { "show",  HandleShow,  rbac::RBAC_PERM_COMMAND_QUEST_REWARD, Console::Yes },
            { "grant", HandleGrant, rbac::RBAC_PERM_COMMAND_QUEST_REWARD, Console::Yes },
            { "take",  HandleTake,  rbac::RBAC_PERM_COMMAND_QUEST_REWARD, Console::Yes },
            { "set",   HandleSet,   rbac::RBAC_PERM_COMMAND_QUEST_REWARD, Console::Yes },
        };
        static ChatCommandTable commandTable =
        {
            { "talents", talentsCommandTable },
        };
        return commandTable;
    }

    // The count comes first because PlayerIdentifier would read a bare number as a character GUID.
    static bool HandleShow(ChatHandler* handler, Optional<PlayerIdentifier> player)
    {
        Player* target = GetTarget(handler, player);
        if (!target)
            return false;
        ReportPoints(handler, target);
        return true;
    }

    static bool HandleGrant(ChatHandler* handler, uint32 count, Optional<PlayerIdentifier> player)
    {
        Player* target = GetTarget(handler, player);
        return target && SetGranted(handler, target, GetGranted(target) + count);
    }

    static bool HandleTake(ChatHandler* handler, uint32 count, Optional<PlayerIdentifier> player)
    {
        Player* target = GetTarget(handler, player);
        if (!target)
            return false;
        uint32 granted = GetGranted(target);
        return SetGranted(handler, target, count > granted ? 0 : granted - count);
    }

    static bool HandleSet(ChatHandler* handler, uint32 count, Optional<PlayerIdentifier> player)
    {
        Player* target = GetTarget(handler, player);
        return target && SetGranted(handler, target, count);
    }
};

void AddSC_talentgrant_commandscript()
{
    new talentgrant_commandscript();
}

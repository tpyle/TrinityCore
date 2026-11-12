/*
 * .bonusstats show|grant|take|set: permanent stats given to one character, on top of gear, buffs and levels.
 *
 * Bonuses are kept in characters.character_bonus_stat (custom-sql/026_bonus_stats.sql) and applied at every
 * login the way gear applies its stats: primary stats, armor, health and mana as base values (white in the
 * character pane, and scaled by percentage buffs), ratings through the combat rating system (in this client
 * 10 hit rating = 1% hit, 14 crit rating = 1% crit, 10 haste rating = 1% haste, at every level). Level-ups
 * never reset these modifiers, so the bonuses last until changed. Works on offline characters too: their
 * row is changed and the bonus applies at their next login.
 *
 * spell_bonus_tome_stat: the stat Tomes (custom-sql 066 and 067, dropped by the core's BonusLoot) each add 1 to one stat.
 */

#include "Chat.h"
#include "ChatCommand.h"
#include "DatabaseEnv.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "SpellScript.h"
#include <array>
#include <span>

using namespace Trinity::ChatCommands;

namespace
{
enum class BonusKind
{
    PrimaryStat,    // Stats value
    Rating,         // one or more CombatRating values
    UnitMod,        // UnitMods value, as a base value
    AttackPower,
    SpellPower
};

struct BonusStat
{
    uint8 Key;          // stored in character_bonus_stat.stat; never renumber
    char const* Name;
    BonusKind Kind;
    std::array<int32, 3> Values; // Stats, CombatRatings or UnitMods; -1 = unused
};

constexpr BonusStat BONUS_STATS[] =
{
    {  1, "strength",    BonusKind::PrimaryStat, { STAT_STRENGTH,  -1, -1 } },
    {  2, "agility",     BonusKind::PrimaryStat, { STAT_AGILITY,   -1, -1 } },
    {  3, "stamina",     BonusKind::PrimaryStat, { STAT_STAMINA,   -1, -1 } },
    {  4, "intellect",   BonusKind::PrimaryStat, { STAT_INTELLECT, -1, -1 } },
    {  5, "spirit",      BonusKind::PrimaryStat, { STAT_SPIRIT,    -1, -1 } },
    { 10, "hit",         BonusKind::Rating,      { CR_HIT_MELEE,   CR_HIT_RANGED,   CR_HIT_SPELL } },
    { 11, "crit",        BonusKind::Rating,      { CR_CRIT_MELEE,  CR_CRIT_RANGED,  CR_CRIT_SPELL } },
    { 12, "haste",       BonusKind::Rating,      { CR_HASTE_MELEE, CR_HASTE_RANGED, CR_HASTE_SPELL } },
    { 13, "expertise",   BonusKind::Rating,      { CR_EXPERTISE,   -1, -1 } },
    { 14, "dodge",       BonusKind::Rating,      { CR_DODGE,       -1, -1 } },
    { 15, "parry",       BonusKind::Rating,      { CR_PARRY,       -1, -1 } },
    { 16, "block",       BonusKind::Rating,      { CR_BLOCK,       -1, -1 } },
    { 20, "attackpower", BonusKind::AttackPower, { -1, -1, -1 } },
    { 21, "spellpower",  BonusKind::SpellPower,  { -1, -1, -1 } },
    { 22, "armor",       BonusKind::UnitMod,     { UNIT_MOD_ARMOR,  -1, -1 } },
    { 23, "health",      BonusKind::UnitMod,     { UNIT_MOD_HEALTH, -1, -1 } },
    { 24, "mana",        BonusKind::UnitMod,     { UNIT_MOD_MANA,   -1, -1 } },
};

constexpr uint32 MAX_BONUS = 100000;

// The stat Tomes' spells (custom-sql 066, tools/bonus_tomes.py): 8104002 + n adds 1 to BONUS_TOMES[n]
constexpr uint32 FIRST_STAT_TOME_SPELL = 8104002;
constexpr uint8 BONUS_TOMES[] = { 1, 2, 3, 4, 5, 10, 11, 12, 13, 14, 15, 16, 20, 21, 22, 23, 24 };
static_assert(std::size(BONUS_TOMES) == std::size(BONUS_STATS));

BonusStat const* FindStat(std::string_view name)
{
    for (BonusStat const& stat : BONUS_STATS)
        if (StringEqualI(name, stat.Name))
            return &stat;
    return nullptr;
}

BonusStat const* FindStat(uint8 key)
{
    for (BonusStat const& stat : BONUS_STATS)
        if (stat.Key == key)
            return &stat;
    return nullptr;
}

void ApplyBonus(Player* player, BonusStat const& stat, int32 amount, bool apply)
{
    if (!amount)
        return;

    switch (stat.Kind)
    {
        case BonusKind::PrimaryStat:
            player->HandleStatFlatModifier(UnitMods(UNIT_MOD_STAT_START + stat.Values[0]), BASE_VALUE, float(amount), apply);
            break;
        case BonusKind::Rating:
            for (int32 rating : stat.Values)
                if (rating >= 0)
                    player->ApplyRatingMod(CombatRating(rating), amount, apply);
            break;
        case BonusKind::UnitMod:
            player->HandleStatFlatModifier(UnitMods(stat.Values[0]), BASE_VALUE, float(amount), apply);
            break;
        case BonusKind::AttackPower:
            player->HandleAttackPowerModifier(AttackPowerModIndex::Melee, AttackPowerModType::FlatPositive, float(amount), apply);
            player->HandleAttackPowerModifier(AttackPowerModIndex::Ranged, AttackPowerModType::FlatPositive, float(amount), apply);
            break;
        case BonusKind::SpellPower:
            player->ApplySpellPowerBonus(amount, apply);
            break;
    }
}

uint32 GetBonus(ObjectGuid guid, BonusStat const& stat)
{
    if (QueryResult result = CharacterDatabase.PQuery("SELECT amount FROM character_bonus_stat WHERE guid = {} AND stat = {}", guid.GetCounter(), stat.Key))
        return (*result)[0].GetUInt32();
    return 0;
}

void StoreBonus(ObjectGuid guid, BonusStat const& stat, uint32 amount)
{
    if (amount)
        CharacterDatabase.DirectPExecute("REPLACE INTO character_bonus_stat (guid, stat, amount) VALUES ({}, {}, {})", guid.GetCounter(), stat.Key, amount);
    else
        CharacterDatabase.DirectPExecute("DELETE FROM character_bonus_stat WHERE guid = {} AND stat = {}", guid.GetCounter(), stat.Key);
}

std::string ListStatNames()
{
    std::string names;
    for (BonusStat const& stat : BONUS_STATS)
    {
        if (!names.empty())
            names += ", ";
        names += stat.Name;
    }
    return names;
}

bool Report(ChatHandler* handler, PlayerIdentifier const& player)
{
    std::string line;
    if (QueryResult result = CharacterDatabase.PQuery("SELECT stat, amount FROM character_bonus_stat WHERE guid = {} ORDER BY stat", player.GetGUID().GetCounter()))
    {
        do
        {
            Field* fields = result->Fetch();
            if (BonusStat const* stat = FindStat(fields[0].GetUInt8()))
            {
                if (!line.empty())
                    line += ", ";
                line += Trinity::StringFormat("{} +{}", stat->Name, fields[1].GetUInt32());
            }
        } while (result->NextRow());
    }

    handler->PSendSysMessage("%s bonus stats: %s.", player.GetName().c_str(), line.empty() ? "none" : line.c_str());
    return true;
}

Optional<PlayerIdentifier> GetTarget(ChatHandler* handler, Optional<PlayerIdentifier> player)
{
    if (!player)
        player = PlayerIdentifier::FromTargetOrSelf(handler);
    if (!player)
    {
        handler->SendSysMessage("No character selected. Name one, or target a player.");
        handler->SetSentErrorMessage(true);
    }
    return player;
}

// Sets the character's bonus in `stat` to `newAmount`, applying the difference live if they are online.
bool SetBonus(ChatHandler* handler, PlayerIdentifier const& player, BonusStat const& stat, uint32 newAmount)
{
    if (newAmount > MAX_BONUS)
    {
        handler->PSendSysMessage("A bonus can be at most %u.", MAX_BONUS);
        handler->SetSentErrorMessage(true);
        return false;
    }

    uint32 oldAmount = GetBonus(player.GetGUID(), stat);
    StoreBonus(player.GetGUID(), stat, newAmount);

    if (Player* target = player.GetConnectedPlayer())
    {
        ApplyBonus(target, stat, int32(oldAmount), false);
        ApplyBonus(target, stat, int32(newAmount), true);
    }
    else
        handler->PSendSysMessage("%s is offline; the change applies at their next login.", player.GetName().c_str());

    return Report(handler, player);
}

BonusStat const* ParseStat(ChatHandler* handler, std::string_view name)
{
    BonusStat const* stat = FindStat(name);
    if (!stat)
    {
        handler->PSendSysMessage("Unknown stat '%s'. Stats: %s.", std::string(name).c_str(), ListStatNames().c_str());
        handler->SetSentErrorMessage(true);
    }
    return stat;
}
}

class bonusstats_commandscript : public CommandScript
{
public:
    bonusstats_commandscript() : CommandScript("bonusstats_commandscript") { }

    std::span<ChatCommandBuilder const> GetCommands() const override
    {
        static ChatCommandTable bonusStatsCommandTable =
        {
            { "show",  HandleShow,  rbac::RBAC_PERM_COMMAND_QUEST_REWARD, Console::Yes },
            { "grant", HandleGrant, rbac::RBAC_PERM_COMMAND_QUEST_REWARD, Console::Yes },
            { "take",  HandleTake,  rbac::RBAC_PERM_COMMAND_QUEST_REWARD, Console::Yes },
            { "set",   HandleSet,   rbac::RBAC_PERM_COMMAND_QUEST_REWARD, Console::Yes },
        };
        static ChatCommandTable commandTable =
        {
            { "bonusstats", bonusStatsCommandTable },
        };
        return commandTable;
    }

    static bool HandleShow(ChatHandler* handler, Optional<PlayerIdentifier> player)
    {
        Optional<PlayerIdentifier> target = GetTarget(handler, player);
        return target && Report(handler, *target);
    }

    static bool HandleGrant(ChatHandler* handler, std::string_view statName, uint32 amount, Optional<PlayerIdentifier> player)
    {
        BonusStat const* stat = ParseStat(handler, statName);
        if (!stat)
            return false;
        Optional<PlayerIdentifier> target = GetTarget(handler, player);
        if (!target)
            return false;
        uint64 newAmount = uint64(GetBonus(target->GetGUID(), *stat)) + amount;
        return SetBonus(handler, *target, *stat, uint32(std::min<uint64>(newAmount, MAX_BONUS + 1)));
    }

    static bool HandleTake(ChatHandler* handler, std::string_view statName, uint32 amount, Optional<PlayerIdentifier> player)
    {
        BonusStat const* stat = ParseStat(handler, statName);
        if (!stat)
            return false;
        Optional<PlayerIdentifier> target = GetTarget(handler, player);
        if (!target)
            return false;
        uint32 current = GetBonus(target->GetGUID(), *stat);
        return SetBonus(handler, *target, *stat, amount > current ? 0 : current - amount);
    }

    static bool HandleSet(ChatHandler* handler, std::string_view statName, uint32 amount, Optional<PlayerIdentifier> player)
    {
        BonusStat const* stat = ParseStat(handler, statName);
        if (!stat)
            return false;
        Optional<PlayerIdentifier> target = GetTarget(handler, player);
        return target && SetBonus(handler, *target, *stat, amount);
    }
};

class bonusstats_playerscript : public PlayerScript
{
public:
    bonusstats_playerscript() : PlayerScript("bonusstats_playerscript") { }

    void OnLogin(Player* player, bool /*firstLogin*/) override
    {
        QueryResult result = CharacterDatabase.PQuery("SELECT stat, amount FROM character_bonus_stat WHERE guid = {}", player->GetGUID().GetCounter());
        if (!result)
            return;

        do
        {
            Field* fields = result->Fetch();
            if (BonusStat const* stat = FindStat(fields[0].GetUInt8()))
                ApplyBonus(player, *stat, int32(fields[1].GetUInt32()), true);
        } while (result->NextRow());
    }

    void OnDelete(ObjectGuid guid, uint32 /*accountId*/) override
    {
        CharacterDatabase.PExecute("DELETE FROM character_bonus_stat WHERE guid = {}", guid.GetCounter());
    }
};

// 8104002-8104018 - Tome of <stat>: +1 to the Tome's stat. Refused (and the Tome kept) at the cap of MAX_BONUS.
class spell_bonus_tome_stat : public SpellScript
{
    BonusStat const* GetTomeStat() const
    {
        uint32 index = GetSpellInfo()->Id - FIRST_STAT_TOME_SPELL;
        return index < std::size(BONUS_TOMES) ? FindStat(BONUS_TOMES[index]) : nullptr;
    }

    bool Load() override
    {
        return GetTomeStat() != nullptr;
    }

    SpellCastResult CheckCast()
    {
        Player* player = GetCaster()->ToPlayer();
        if (!player)
            return SPELL_FAILED_BAD_TARGETS;

        BonusStat const* stat = GetTomeStat();
        if (GetBonus(player->GetGUID(), *stat) >= MAX_BONUS)
        {
            ChatHandler(player->GetSession()).PSendSysMessage("Your %s bonus is already at its limit of %u.", stat->Name, MAX_BONUS);
            return SPELL_FAILED_DONT_REPORT;
        }
        return SPELL_CAST_OK;
    }

    void HandleDummy(SpellEffIndex /*effIndex*/)
    {
        Player* player = GetHitPlayer();
        if (!player)
            return;

        BonusStat const* stat = GetTomeStat();
        uint32 amount = GetBonus(player->GetGUID(), *stat) + 1;
        StoreBonus(player->GetGUID(), *stat, amount);
        ApplyBonus(player, *stat, 1, true);
        ChatHandler(player->GetSession()).PSendSysMessage("You gain 1 %s (bonus now +%u).", stat->Name, amount);
    }

    // The bonus is written at once; save the character after the Tome is used up (Spell::TakeCastItem runs after the
    // effects), so a crash cannot leave the bonus with the Tome still in the bags
    void SaveCharacter()
    {
        if (Player* player = GetCaster()->ToPlayer())
            player->SaveToDB();
    }

    void Register() override
    {
        OnCheckCast += SpellCheckCastFn(spell_bonus_tome_stat::CheckCast);
        OnEffectHitTarget += SpellEffectFn(spell_bonus_tome_stat::HandleDummy, EFFECT_0, SPELL_EFFECT_DUMMY);
        AfterCast += SpellCastFn(spell_bonus_tome_stat::SaveCharacter);
    }
};

void AddSC_bonusstats_commandscript()
{
    new bonusstats_commandscript();
    new bonusstats_playerscript();
    RegisterSpellScript(spell_bonus_tome_stat);
}

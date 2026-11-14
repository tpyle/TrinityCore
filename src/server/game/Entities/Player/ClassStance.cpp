/*
 * Class stances: see ClassStance.h.
 */

#include "ClassStance.h"
#include "ChatPackets.h"
#include "Config.h"
#include "DatabaseEnv.h"
#include "DB2Stores.h"
#include "Log.h"
#include "Player.h"
#include "SharedDefines.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "StringConvert.h"
#include "TraitMgr.h"
#include "TraitPacketsCommon.h"
#include "UpdateFields.h"
#include "Util.h"
#include "WorldSession.h"

namespace ClassStance
{
    namespace
    {
        bool enabled = true;
        constexpr int32 ClassicTalentCurrency = 3820;   // the Classic talent points (TraitMgr)
        bool talentsAlwaysActive = true;
        bool allowInCombat = false;
        bool sharedTalentPoints = false;

        bool IsStanceSkillCategory(uint32 skill)
        {
            SkillLineEntry const* line = sSkillLineStore.LookupEntry(skill);
            return line && (line->CategoryID == SKILL_CATEGORY_CLASS || line->CategoryID == SKILL_CATEGORY_ARMOR
                || line->CategoryID == SKILL_CATEGORY_WEAPON);
        }

        // a SkillRaceClassInfo entry of exactly this class (not one for every class: the real class has those already)
        bool IsClassEntry(SkillRaceClassInfoEntry const* rcInfo, uint8 classId)
        {
            return rcInfo->ClassMask != 0 && rcInfo->ClassMask != -1 && (rcInfo->ClassMask & (1 << (classId - 1)));
        }
    }

    bool IsEnabled()
    {
        return enabled;
    }

    void LoadConfig()
    {
        enabled = sConfigMgr->GetBoolDefault("ClassStance.Enable", true);
        talentsAlwaysActive = sConfigMgr->GetBoolDefault("ClassStance.TalentsAlwaysActive", true);
        allowInCombat = sConfigMgr->GetBoolDefault("ClassStance.AllowInCombat", false);
        sharedTalentPoints = sConfigMgr->GetBoolDefault("ClassStance.SharedTalentPoints", false);
        TC_LOG_INFO("server.loading", "Class stances: {} (spells {}-{}), talents {}, {} talent points, {}", enabled ? "enabled" : "disabled",
            FirstSpell, LastSpell, talentsAlwaysActive ? "always active" : "active in their stance", sharedTalentPoints ? "shared" : "separate",
            allowInCombat ? "changed in combat too" : "changed out of combat only");
    }

    uint8 GetClassForSpell(uint32 spellId)
    {
        if (spellId < FirstSpell || spellId > LastSpell)
            return 0;
        return Classes[spellId - FirstSpell];
    }

    uint32 GetSpellForClass(uint8 classId)
    {
        for (uint32 i = 0; i < std::size(Classes); ++i)
            if (Classes[i] == classId)
                return FirstSpell + i;
        return 0;
    }

    uint32 GetLearnedClassMask(Player const* player)
    {
        if (!enabled)
            return 0;

        uint32 mask = 0;
        for (uint32 i = 0; i < std::size(Classes); ++i)
            if (Classes[i] != player->GetClass() && player->HasSpell(FirstSpell + i))
                mask |= 1u << (Classes[i] - 1);
        return mask;
    }

    bool KnowsClass(Player const* player, uint8 classId)
    {
        return player->GetClass() == classId || (GetLearnedClassMask(player) & (1u << (classId - 1)));
    }

    SkillRaceClassInfoEntry const* GetSkillRaceClassInfo(Player const* player, uint32 skill, uint8 race, bool anyStance)
    {
        if (SkillRaceClassInfoEntry const* rcInfo = sDB2Manager.GetSkillRaceClassInfo(skill, race, player->GetClass()))
            return rcInfo;

        if (!enabled)
            return nullptr;

        uint32 const classMask = anyStance ? 0xFFFFFFFFu : GetLearnedClassMask(player);
        if (!classMask)
            return nullptr;

        for (uint8 classId : Classes)
        {
            if (classId == player->GetClass() || !(classMask & (1u << (classId - 1))))
                continue;
            if (SkillRaceClassInfoEntry const* rcInfo = sDB2Manager.GetSkillRaceClassInfo(skill, race, classId))
                return rcInfo;
            if (IsStanceSkillCategory(skill))
                for (SkillRaceClassInfoEntry const* rcInfo : sDB2Manager.GetSkillRaceClassInfo(skill))
                    if (IsClassEntry(rcInfo, classId))
                        return rcInfo;
        }
        return nullptr;
    }

    uint8 GetClassOfSpell(SpellInfo const* spellInfo)
    {
        if (!spellInfo->SpellFamilyName)
            return 0;
        for (uint8 classId : Classes)
            if (ChrClassesEntry const* entry = sChrClassesStore.LookupEntry(classId))
                if (entry->SpellClassSet == spellInfo->SpellFamilyName)
                    return classId;
        return 0;
    }

    SpellCastResult CheckClassSpell(Player const* player, SpellInfo const* spellInfo, bool notify)
    {
        uint32 const learned = GetLearnedClassMask(player);
        if (!learned)
            return SPELL_CAST_OK;

        uint8 const spellClass = GetClassOfSpell(spellInfo);
        if (!spellClass || spellClass == player->GetEffectiveClass())
            return SPELL_CAST_OK;

        uint8 stanceClass;
        SpellCastResult result;
        if (spellClass == player->GetClass())
        {
            stanceClass = player->GetEffectiveClass();      // a class stance is active
            result = SPELL_FAILED_NOT_SHAPESHIFT;
        }
        else if (learned & (1u << (spellClass - 1)))
        {
            stanceClass = spellClass;                       // the spell's class stance is not active
            result = SPELL_FAILED_ONLY_SHAPESHIFT;
        }
        else
            return SPELL_CAST_OK;

        if (!notify)
            return result;

        SpellInfo const* stance = sSpellMgr->GetSpellInfo(GetSpellForClass(stanceClass), DIFFICULTY_NONE);
        char const* stanceName = stance ? stance->SpellName->Str[player->GetSession()->GetSessionDbcLocale()] : "a class stance";
        if (result == SPELL_FAILED_NOT_SHAPESHIFT)
            player->GetSession()->SendNotification("Leave %s first.", stanceName);
        else
            player->GetSession()->SendNotification("Requires %s.", stanceName);
        return SPELL_FAILED_DONT_REPORT;
    }

    void LearnClassSkills(Player* player, uint8 classId)
    {
        for (SkillRaceClassInfoEntry const* rcInfo : sSkillRaceClassInfoStore)
        {
            if (rcInfo->Availability != 1 || !IsClassEntry(rcInfo, classId))
                continue;
            if (!rcInfo->RaceMask.IsEmpty() && !rcInfo->RaceMask.HasRace(player->GetRace()) && !IsStanceSkillCategory(rcInfo->SkillID))
                continue;
            if (rcInfo->MinLevel > player->GetLevel() || player->HasSkill(rcInfo->SkillID))
                continue;

            player->LearnDefaultSkill(rcInfo);
        }
    }

    namespace
    {
        // TalentTab IDs of each class's three trees (client TalentTab.db2; tools/class_stance_addon_data.py writes the same)
        uint8 GetTabClass(uint32 tabId)
        {
            switch (tabId)
            {
                case 161: case 163: case 164: return 1;     // warrior
                case 381: case 382: case 383: return 2;     // paladin
                case 361: case 362: case 363: return 3;     // hunter
                case 181: case 182: case 183: return 4;     // rogue
                case 201: case 202: case 203: return 5;     // priest
                case 261: case 262: case 263: return 7;     // shaman
                case 41: case 61: case 81:    return 8;     // mage
                case 301: case 302: case 303: return 9;     // warlock
                case 281: case 282: case 283: return 11;    // druid
                default: return 0;
            }
        }

        uint8 GetMaxRank(TalentEntry const* talent)
        {
            uint8 ranks = 0;
            for (uint32 spell : talent->SpellRank)
                if (spell)
                    ++ranks;
            return ranks;
        }

        // the trait definition of a talent's rank 1 spell, as the real class's talents learn it
        uint32 GetTraitDefinition(uint32 spellId)
        {
            static std::unordered_map<uint32, uint32> const definitions = []
            {
                std::unordered_map<uint32, uint32> map;
                for (TraitDefinitionEntry const* definition : sTraitDefinitionStore)
                    if (definition->SpellID)
                        map.emplace(definition->SpellID, definition->ID);
                return map;
            }();
            auto itr = definitions.find(spellId);
            return itr != definitions.end() ? itr->second : 0;
        }

        bool IsTalentActive(Player const* player, uint8 classId)
        {
            return enabled && (GetLearnedClassMask(player) & (1u << (classId - 1)))
                && (talentsAlwaysActive || player->GetClassStance() == classId);
        }

        void ApplyTalent(Player* player, TalentEntry const* talent, uint8 rank, bool apply)
        {
            uint32 const firstSpell = talent->SpellRank[0];
            if (uint32 definition = GetTraitDefinition(firstSpell))
            {
                if (apply)
                    player->LearnSpell(firstSpell, true, 0, false, PlayerSpellTrait{ .DefinitionId = int32(definition), .Rank = rank });
                else
                    player->RemoveSpell(firstSpell, false, false);
                return;
            }

            // no trait for it: the Classic rank spells, the highest one only
            for (uint8 i = 0; i < GetMaxRank(talent); ++i)
                if (player->HasSpell(talent->SpellRank[i]))
                    player->RemoveSpell(talent->SpellRank[i], false, false);
            if (apply && rank)
                player->LearnSpell(talent->SpellRank[rank - 1], true);
        }

        void ApplyClassTalents(Player* player, uint8 classId, bool apply)
        {
            for (auto const& [talentId, rank] : player->GetClassStanceTalents())
                if (TalentEntry const* talent = sTalentStore.LookupEntry(talentId))
                    if (GetTabClass(talent->TabID) == classId)
                        ApplyTalent(player, talent, rank, apply);
        }

        uint32 GetSpentInTab(Player const* player, uint32 tabId)
        {
            uint32 spent = 0;
            for (auto const& [talentId, rank] : player->GetClassStanceTalents())
                if (TalentEntry const* talent = sTalentStore.LookupEntry(talentId))
                    if (talent->TabID == tabId)
                        spent += rank;
            return spent;
        }

        void SendAddon(Player* player, std::string const& text)
        {
            WorldPackets::Chat::Chat chat;
            chat.Initialize(CHAT_MSG_WHISPER, LANG_ADDON, player, player, text, 0, "", LOCALE_enUS, AddonPrefix);
            player->SendDirectMessage(chat.Write());
        }

        // entries joined by sep into messages of at most 250 bytes, each starting with head
        void SendList(Player* player, std::string const& head, std::vector<std::string> const& entries, char sep)
        {
            std::string message = head;
            bool empty = true;
            for (std::string const& entry : entries)
            {
                if (!empty && message.size() + 1 + entry.size() > 250)
                {
                    SendAddon(player, message);
                    message = head;
                    empty = true;
                }
                if (!empty)
                    message += sep;
                message += entry;
                empty = false;
            }
            if (!empty)
                SendAddon(player, message);
        }
    }

    bool TalentsAlwaysActive()
    {
        return talentsAlwaysActive;
    }

    namespace
    {
        // the real class's talent points (its talent currency: levels, the Talented perk, Rate.Talent, granted points and Tomes)
        // and what its talent window has spent
        void GetRealTalentPoints(Player const* player, uint32& owned, uint32& spent)
        {
            owned = spent = 0;
            if (UF::TraitConfig const* config = player->GetTraitConfig(player->m_activePlayerData->ActiveCombatTraitConfigID))
            {
                WorldPackets::Traits::TraitConfig traitConfig(*config);
                std::map<int32, int32> ownedCurrencies;
                TraitMgr::FillOwnedCurrenciesMap(traitConfig, player, ownedCurrencies);
                if (auto itr = ownedCurrencies.find(ClassicTalentCurrency); itr != ownedCurrencies.end())
                    owned = uint32(std::max(itr->second, 0));
                std::map<int32, TraitMgr::SpentCurrency> spentCurrencies;
                TraitMgr::FillSpentCurrenciesMap(traitConfig.Entries, spentCurrencies);
                if (auto itr = spentCurrencies.find(ClassicTalentCurrency); itr != spentCurrencies.end())
                    spent = uint32(std::max(itr->second.Total, 0));
                return;
            }

            // no talent configuration yet: the points from levels
            if (player->GetLevel() >= 10)
                owned = uint32(std::min<uint32>(player->GetLevel() - 9, 51) * sConfigMgr->GetFloatDefault("Rate.Talent", 1.0f));
        }
    }

    bool AllowInCombat()
    {
        return allowInCombat;
    }

    bool SharedTalentPoints()
    {
        return sharedTalentPoints;
    }

    uint32 GetTalentPoints(Player const* player, uint8 classId)
    {
        // as many as the real class owns
        uint32 owned, realSpent;
        GetRealTalentPoints(player, owned, realSpent);
        if (!sharedTalentPoints)
            return owned;

        // shared: what the real class and the other stance classes have not spent (never below what this class has spent)
        uint32 const own = GetSpentTalentPoints(player, classId);
        uint32 const others = realSpent + GetAllStanceSpentTalentPoints(player) - own;
        return std::max(owned > others ? owned - others : 0u, own);
    }

    uint32 GetAllStanceSpentTalentPoints(Player const* player)
    {
        uint32 spent = 0;
        for (auto const& [talentId, rank] : player->GetClassStanceTalents())
            if (TalentEntry const* talent = sTalentStore.LookupEntry(talentId))
                if (GetTabClass(talent->TabID))
                    spent += rank;
        return spent;
    }

    std::string CheckRealTalentCommit(Player const* player, std::vector<int32> const& oldAndNewSpent)
    {
        if (!enabled || !sharedTalentPoints || oldAndNewSpent.size() != 2)
            return "";
        int32 const oldSpent = oldAndNewSpent[0], newSpent = oldAndNewSpent[1];
        if (newSpent <= oldSpent)
            return "";      // taking talents back is always allowed

        uint32 owned, realSpent;
        GetRealTalentPoints(player, owned, realSpent);
        uint32 const stances = GetAllStanceSpentTalentPoints(player);
        if (uint32(std::max(newSpent, 0)) + stances <= owned)
            return "";
        return fmt::format("Your talent points are shared with your class stances, which have spent {} of them: {} of {} are left for your own talents.",
            stances, owned > stances ? owned - stances : 0, owned);
    }

    uint32 GetSpentTalentPoints(Player const* player, uint8 classId)
    {
        uint32 spent = 0;
        for (auto const& [talentId, rank] : player->GetClassStanceTalents())
            if (TalentEntry const* talent = sTalentStore.LookupEntry(talentId))
                if (GetTabClass(talent->TabID) == classId)
                    spent += rank;
        return spent;
    }

    std::string LearnTalent(Player* player, uint8 classId, uint32 talentId)
    {
        if (!enabled || !(GetLearnedClassMask(player) & (1u << (classId - 1))))
            return "You have not learned that class's stance.";
        if (player->IsInCombat())
            return "Not in combat.";

        TalentEntry const* talent = sTalentStore.LookupEntry(talentId);
        if (!talent || GetTabClass(talent->TabID) != classId || !GetMaxRank(talent))
            return "Unknown talent.";

        auto& talents = player->GetClassStanceTalents();
        uint8 const rank = talents.contains(talentId) ? talents[talentId] : 0;
        if (rank >= GetMaxRank(talent))
            return "That talent is at its highest rank.";
        if (GetSpentTalentPoints(player, classId) >= GetTalentPoints(player, classId))
            return "You have no talent points left for that class.";
        if (GetSpentInTab(player, talent->TabID) < uint32(talent->TierID) * 5)
            return "Spend more points in that tree first.";
        for (uint8 i = 0; i < talent->PrereqTalent.size(); ++i)
        {
            if (!talent->PrereqTalent[i])
                continue;
            auto itr = talents.find(talent->PrereqTalent[i]);
            if (itr == talents.end() || itr->second < talent->PrereqRank[i] + 1)
                return "That talent needs another talent first.";
        }

        talents[talentId] = rank + 1;
        CharacterDatabase.DirectPExecute("REPLACE INTO character_class_stance_talent (guid, class, talent, `rank`) VALUES ({}, {}, {}, {})",
            player->GetGUID().GetCounter(), classId, talentId, rank + 1);

        if (IsTalentActive(player, classId))
        {
            if (rank)
                ApplyTalent(player, talent, rank, false);
            ApplyTalent(player, talent, rank + 1, true);
        }
        return "";
    }

    void ResetTalents(Player* player, uint8 classId)
    {
        if (IsTalentActive(player, classId))
            ApplyClassTalents(player, classId, false);

        auto& talents = player->GetClassStanceTalents();
        std::erase_if(talents, [classId](auto const& pair)
        {
            TalentEntry const* talent = sTalentStore.LookupEntry(pair.first);
            return !talent || GetTabClass(talent->TabID) == classId;
        });
        CharacterDatabase.DirectPExecute("DELETE FROM character_class_stance_talent WHERE guid = {} AND class = {}", player->GetGUID().GetCounter(), classId);
    }

    void LoadTalents(Player* player)
    {
        auto& talents = player->GetClassStanceTalents();
        talents.clear();
        if (QueryResult result = CharacterDatabase.PQuery("SELECT talent, `rank` FROM character_class_stance_talent WHERE guid = {}", player->GetGUID().GetCounter()))
        {
            do
            {
                Field* fields = result->Fetch();
                talents[fields[0].GetUInt32()] = fields[1].GetUInt8();
            } while (result->NextRow());
        }

        for (uint8 classId : Classes)
            if (IsTalentActive(player, classId))
                ApplyClassTalents(player, classId, true);
    }

    void DeleteTalents(ObjectGuid guid)
    {
        CharacterDatabase.PExecute("DELETE FROM character_class_stance_talent WHERE guid = {}", guid.GetCounter());
    }

    void OnStanceChanged(Player* player, uint8 oldClass)
    {
        if (!player->IsInWorld())
            return;

        if (!talentsAlwaysActive)
        {
            if (oldClass)
                ApplyClassTalents(player, oldClass, false);
            if (player->GetClassStance())
                ApplyClassTalents(player, player->GetClassStance(), true);
        }
        SendState(player);
    }

    void SendState(Player* player)
    {
        // STATE <real class> <learned class mask> <active stance class> <talents always active> <class:points:spent;...>
        std::string message = fmt::format("STATE {} {} {} {} ", player->GetClass(), GetLearnedClassMask(player), player->GetClassStance(),
            talentsAlwaysActive ? 1 : 0);
        bool first = true;
        for (uint8 classId : Classes)
        {
            if (!(GetLearnedClassMask(player) & (1u << (classId - 1))))
                continue;
            message += fmt::format("{}{}:{}:{}", first ? "" : ";", classId, GetTalentPoints(player, classId), GetSpentTalentPoints(player, classId));
            first = false;
        }
        SendAddon(player, message);
    }

    void SendTalents(Player* player, uint8 classId)
    {
        // TALB <class>, then TAL <class> <talent:rank,...> as needed, then TALE <class> <points> <spent>
        SendAddon(player, fmt::format("TALB {}", classId));
        std::vector<std::string> entries;
        for (auto const& [talentId, rank] : player->GetClassStanceTalents())
            if (TalentEntry const* talent = sTalentStore.LookupEntry(talentId))
                if (GetTabClass(talent->TabID) == classId)
                    entries.push_back(fmt::format("{}:{}", talentId, rank));
        SendList(player, fmt::format("TAL {} ", classId), entries, ',');
        SendAddon(player, fmt::format("TALE {} {} {}", classId, GetTalentPoints(player, classId), GetSpentTalentPoints(player, classId)));
    }

    void SendSkills(Player* player)
    {
        // SKLB, then SKL <category:value:max:name;...>, then SKLE: the class, armor and weapon skills (the client hides those its
        // data gives to other classes)
        SendAddon(player, "SKLB");
        std::vector<std::string> entries;
        for (SkillLineEntry const* line : sSkillLineStore)
        {
            if (!IsStanceSkillCategory(line->ID) || !player->HasSkill(line->ID))
                continue;
            entries.push_back(fmt::format("{}:{}:{}:{}", line->CategoryID, player->GetSkillValue(line->ID), player->GetMaxSkillValue(line->ID),
                line->DisplayName[LOCALE_enUS]));
        }
        SendList(player, "SKL ", entries, ';');
        SendAddon(player, "SKLE");
    }

    void SendSpells(Player* player, uint8 classId)
    {
        // SPLB <class>, SPLS <class> <skill:name;...>, SPL <class> <spell:skill,...>, SPLE <class>: the spellbook spells (active,
        // not disabled) of the class's spell family, each with the class skill line it is in (0 for none)
        SendAddon(player, fmt::format("SPLB {}", classId));
        std::vector<std::string> spells;
        std::map<uint32, std::string> skills;
        for (auto const& [spellId, spell] : player->GetSpellMap())
        {
            if (spell.state == PLAYERSPELL_REMOVED || spell.disabled || !spell.active)
                continue;
            SpellInfo const* info = sSpellMgr->GetSpellInfo(spellId, DIFFICULTY_NONE);
            if (!info || GetClassOfSpell(info) != classId)
                continue;

            uint32 skillId = 0;
            SkillLineAbilityMapBounds bounds = sSpellMgr->GetSkillLineAbilityMapBounds(spellId);
            for (auto itr = bounds.first; itr != bounds.second && !skillId; ++itr)
                if (SkillLineEntry const* line = sSkillLineStore.LookupEntry(itr->second->SkillLine))
                    if (line->CategoryID == SKILL_CATEGORY_CLASS)
                        skillId = line->ID;
            if (skillId && !skills.contains(skillId))
                skills[skillId] = sSkillLineStore.AssertEntry(skillId)->DisplayName[LOCALE_enUS];
            spells.push_back(fmt::format("{}:{}", spellId, skillId));
        }

        std::vector<std::string> skillEntries;
        for (auto const& [skillId, name] : skills)
            skillEntries.push_back(fmt::format("{}:{}", skillId, name));
        SendList(player, fmt::format("SPLS {} ", classId), skillEntries, ';');
        SendList(player, fmt::format("SPL {} ", classId), spells, ',');
        SendAddon(player, fmt::format("SPLE {}", classId));
    }

    void HandleAddonMessage(Player* player, std::string const& text)
    {
        std::vector<std::string_view> args = Trinity::Tokenize(text, ' ', false);
        if (args.empty())
            return;

        auto number = [&args](size_t i) { return i < args.size() ? Trinity::StringTo<uint32>(args[i]).value_or(0) : 0u; };
        std::string_view const command = args[0];
        if (command == "PING")
            SendAddon(player, "PONG");
        else if (command == "HELLO")
        {
            SendState(player);
            SendSkills(player);
        }
        else if (command == "TALENTS")
            SendTalents(player, uint8(number(1)));
        else if (command == "LEARN")
        {
            uint8 const classId = uint8(number(1));
            std::string error = LearnTalent(player, classId, number(2));
            if (!error.empty())
                SendAddon(player, "ERR " + error);
            SendTalents(player, classId);
            SendState(player);
        }
        else if (command == "RESET")
        {
            uint8 const classId = uint8(number(1));
            if (player->IsInCombat())
                SendAddon(player, "ERR Not in combat.");
            else if (GetLearnedClassMask(player) & (1u << (classId - 1)))
                ResetTalents(player, classId);
            SendTalents(player, classId);
            SendState(player);
        }
        else if (command == "SKILLS")
            SendSkills(player);
        else if (command == "SPELLS")
        {
            uint8 const classId = uint8(number(1));
            if (GetLearnedClassMask(player) & (1u << (classId - 1)))
                SendSpells(player, classId);
        }
    }
}

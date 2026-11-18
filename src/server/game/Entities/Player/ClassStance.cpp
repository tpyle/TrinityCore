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
#include <unordered_set>

namespace ClassStance
{
    namespace
    {
        bool enabled = true;
        constexpr int32 ClassicTalentCurrency = 3820;   // the Classic talent points (TraitMgr)
        bool talentsAlwaysActive = true;
        bool allowInCombat = false;
        bool sharedTalentPoints = false;
        std::unordered_set<uint32> anyStanceSpells;    // ClassStance.AnyStanceSpells: cast in any stance or none

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
        anyStanceSpells.clear();
        std::string const anyStanceList = sConfigMgr->GetStringDefault("ClassStance.AnyStanceSpells", "75");
        for (std::string_view token : Trinity::Tokenize(anyStanceList, ' ', false))
            if (Optional<uint32> spellId = Trinity::StringTo<uint32>(token))
                anyStanceSpells.insert(*spellId);
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

    // the class of a spell: its spell family, or for a spell of no family (Rockbiter Weapon, Flametongue Weapon, Water Breathing)
    // the class skill line it is in (SkillLineAbility), when that line belongs to one class only (SkillRaceClassInfo)
    uint8 GetClassOfSpell(SpellInfo const* spellInfo)
    {
        if (spellInfo->SpellFamilyName)
        {
            for (uint8 classId : Classes)
                if (ChrClassesEntry const* entry = sChrClassesStore.LookupEntry(classId))
                    if (entry->SpellClassSet == spellInfo->SpellFamilyName)
                        return classId;
            return 0;
        }

        static std::unordered_map<uint32, uint8> const lineClasses = []
        {
            std::unordered_map<uint32, uint32> masks;
            for (SkillRaceClassInfoEntry const* rcInfo : sSkillRaceClassInfoStore)
                if (SkillLineEntry const* line = sSkillLineStore.LookupEntry(rcInfo->SkillID); line && line->CategoryID == SKILL_CATEGORY_CLASS)
                    masks[rcInfo->SkillID] |= uint32(rcInfo->ClassMask);
            std::unordered_map<uint32, uint8> out;
            for (auto const& [skillId, mask] : masks)
                for (uint8 classId : Classes)
                    if (mask == 1u << (classId - 1))
                        out[skillId] = classId;
            return out;
        }();

        SkillLineAbilityMapBounds bounds = sSpellMgr->GetSkillLineAbilityMapBounds(spellInfo->Id);
        for (auto itr = bounds.first; itr != bounds.second; ++itr)
            if (auto line = lineClasses.find(itr->second->SkillLine); line != lineClasses.end())
                return line->second;
        return 0;
    }

    SpellCastResult CheckClassSpell(Player const* player, SpellInfo const* spellInfo, bool notify)
    {
        uint32 const learned = GetLearnedClassMask(player);
        if (!learned)
            return SPELL_CAST_OK;

        uint8 const spellClass = GetClassOfSpell(spellInfo);
        if (!spellClass || spellClass == player->GetEffectiveClass() || anyStanceSpells.contains(spellInfo->Id))
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
        // the stance talent trees: WoW Forever's trait trees as the client's talent window shows them (world.class_stance_talent,
        // custom-sql 075, written by tools/class_stance_addon_data.py with the addon's data), by trait node entry
        struct TalentNode
        {
            uint8 ClassId = 0;
            uint32 Definition = 0;      // TraitDefinition: learned as Spell with the rank as a trait
            uint32 Spell = 0;
            uint8 Tab = 0;
            uint8 Need = 0;             // points in the tab its row asks
            uint8 MaxRanks = 0;
            std::array<uint32, 3> Prereqs = { };   // entries that must be at their full rank
        };
        std::unordered_map<uint32, TalentNode> talentNodes;

        TalentNode const* GetNode(uint32 entry)
        {
            auto itr = talentNodes.find(entry);
            return itr != talentNodes.end() ? &itr->second : nullptr;
        }

        bool IsTalentActive(Player const* player, uint8 classId)
        {
            return enabled && (GetLearnedClassMask(player) & (1u << (classId - 1)))
                && (talentsAlwaysActive || player->GetClassStance() == classId);
        }

        // quiet: no "You have learned/unlearned" message (talents put back at login or on a stance change, not newly picked)
        void ApplyTalent(Player* player, TalentNode const& node, uint8 rank, bool apply, bool quiet)
        {
            if (!node.Spell)
                return;
            if (apply && rank)
                player->LearnSpell(node.Spell, true, 0, quiet, PlayerSpellTrait{ .DefinitionId = int32(node.Definition), .Rank = rank });
            else
                player->RemoveSpell(node.Spell, false, false, quiet);
        }

        void ApplyClassTalents(Player* player, uint8 classId, bool apply, bool quiet)
        {
            for (auto const& [entry, rank] : player->GetClassStanceTalents())
                if (TalentNode const* node = GetNode(entry); node && node->ClassId == classId)
                    ApplyTalent(player, *node, rank, apply, quiet);
        }

        uint32 GetSpentInTab(Player const* player, uint8 classId, uint8 tab)
        {
            uint32 spent = 0;
            for (auto const& [entry, rank] : player->GetClassStanceTalents())
                if (TalentNode const* node = GetNode(entry); node && node->ClassId == classId && node->Tab == tab)
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

    void LoadTalentNodes()
    {
        talentNodes.clear();
        QueryResult result = WorldDatabase.Query("SELECT entry, class, definition, spell, tab, need, max_ranks, prereq1, prereq2, prereq3 FROM class_stance_talent");
        if (!result)
        {
            TC_LOG_ERROR("server.loading", "Class stances: no talents in world.class_stance_talent (custom-sql 075)");
            return;
        }
        do
        {
            Field* fields = result->Fetch();
            TalentNode& node = talentNodes[fields[0].GetUInt32()];
            node.ClassId = fields[1].GetUInt8();
            node.Definition = fields[2].GetUInt32();
            node.Spell = fields[3].GetUInt32();
            node.Tab = fields[4].GetUInt8();
            node.Need = fields[5].GetUInt8();
            node.MaxRanks = fields[6].GetUInt8();
            node.Prereqs = { fields[7].GetUInt32(), fields[8].GetUInt32(), fields[9].GetUInt32() };
            if (!sSpellMgr->GetSpellInfo(node.Spell, DIFFICULTY_NONE))
                TC_LOG_ERROR("server.loading", "Class stances: talent {} learns spell {}, which does not exist", fields[0].GetUInt32(), node.Spell);
        } while (result->NextRow());
        TC_LOG_INFO("server.loading", ">> Loaded {} class stance talents", talentNodes.size());
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
        for (auto const& [entry, rank] : player->GetClassStanceTalents())
            if (GetNode(entry))
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
        for (auto const& [entry, rank] : player->GetClassStanceTalents())
            if (TalentNode const* node = GetNode(entry); node && node->ClassId == classId)
                spent += rank;
        return spent;
    }

    std::string LearnTalent(Player* player, uint8 classId, uint32 talentId)
    {
        if (!enabled || !(GetLearnedClassMask(player) & (1u << (classId - 1))))
            return "You have not learned that class's stance.";
        if (player->IsInCombat())
            return "Not in combat.";

        TalentNode const* node = GetNode(talentId);
        if (!node || node->ClassId != classId || !node->MaxRanks)
            return "Unknown talent.";

        auto& talents = player->GetClassStanceTalents();
        uint8 const rank = talents.contains(talentId) ? talents[talentId] : 0;
        if (rank >= node->MaxRanks)
            return "That talent is at its highest rank.";
        if (GetSpentTalentPoints(player, classId) >= GetTalentPoints(player, classId))
            return "You have no talent points left for that class.";
        if (GetSpentInTab(player, classId, node->Tab) < node->Need)
            return fmt::format("Requires {} points in that tree.", node->Need);
        for (uint32 prereq : node->Prereqs)
        {
            if (!prereq)
                continue;
            TalentNode const* parent = GetNode(prereq);
            auto itr = talents.find(prereq);
            if (parent && (itr == talents.end() || itr->second < parent->MaxRanks))
                return "That talent needs another talent at its highest rank first.";
        }

        talents[talentId] = rank + 1;
        CharacterDatabase.DirectPExecute("REPLACE INTO character_class_stance_talent (guid, class, talent, `rank`) VALUES ({}, {}, {}, {})",
            player->GetGUID().GetCounter(), classId, talentId, rank + 1);

        if (IsTalentActive(player, classId))
        {
            // off and on again: learning a known spell with a new rank only relinks the trait, and a passive aura (Divine Intellect)
            // kept its first rank's amount
            if (rank)
                ApplyTalent(player, *node, rank, false, true);
            ApplyTalent(player, *node, rank + 1, true, false);
        }
        return "";
    }

    void ResetTalents(Player* player, uint8 classId)
    {
        if (IsTalentActive(player, classId))
            ApplyClassTalents(player, classId, false, false);

        auto& talents = player->GetClassStanceTalents();
        std::erase_if(talents, [classId](auto const& pair)
        {
            TalentNode const* node = GetNode(pair.first);
            return !node || node->ClassId == classId;
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

        // a pick of a talent no longer in the trees, or above its ranks (the trees changed): dropped or lowered, the points back
        for (auto itr = talents.begin(); itr != talents.end();)
        {
            TalentNode const* node = GetNode(itr->first);
            if (!node)
            {
                CharacterDatabase.PExecute("DELETE FROM character_class_stance_talent WHERE guid = {} AND talent = {}",
                    player->GetGUID().GetCounter(), itr->first);
                itr = talents.erase(itr);
                continue;
            }
            if (itr->second > node->MaxRanks)
            {
                itr->second = node->MaxRanks;
                CharacterDatabase.PExecute("UPDATE character_class_stance_talent SET `rank` = {} WHERE guid = {} AND talent = {}",
                    itr->second, player->GetGUID().GetCounter(), itr->first);
            }
            ++itr;
        }

        for (uint8 classId : Classes)
            if (IsTalentActive(player, classId))
                ApplyClassTalents(player, classId, true, true);
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
                ApplyClassTalents(player, oldClass, false, true);
            if (player->GetClassStance())
                ApplyClassTalents(player, player->GetClassStance(), true, true);
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
        for (auto const& [entry, rank] : player->GetClassStanceTalents())
            if (TalentNode const* node = GetNode(entry); node && node->ClassId == classId)
                entries.push_back(fmt::format("{}:{}", entry, rank));
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

/*
 * Class stances: see ClassStance.h.
 */

#include "ClassStance.h"
#include "Config.h"
#include "DB2Stores.h"
#include "Log.h"
#include "Player.h"
#include "SharedDefines.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "WorldSession.h"

namespace ClassStance
{
    namespace
    {
        bool enabled = true;

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
        TC_LOG_INFO("server.loading", "Class stances: {} (spells {}-{})", enabled ? "enabled" : "disabled", FirstSpell, LastSpell);
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
}

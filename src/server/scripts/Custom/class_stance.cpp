/*
 * Class stances (game/Entities/Player/ClassStance.h): the stance bar auras 8104019-8104027 (custom-sql 071, scripts bound in
 * 072). Casting one makes the player fight as its class until it is cancelled or another is cast; casting the active one again
 * cancels it. Out of combat only, never the player's own class.
 */

#include "ScriptMgr.h"
#include "ClassStance.h"
#include "Player.h"
#include "SpellAuraEffects.h"
#include "SpellScript.h"

class spell_class_stance : public SpellScript
{
    SpellCastResult CheckCast()
    {
        Player* player = GetCaster()->ToPlayer();
        uint8 const stanceClass = ClassStance::GetClassForSpell(GetSpellInfo()->Id);
        if (!player || !stanceClass || !ClassStance::IsEnabled() || stanceClass == player->GetClass())
            return SPELL_FAILED_DONT_REPORT;

        // the stance bar button of the active stance turns it off
        if (player->HasAura(GetSpellInfo()->Id))
        {
            player->RemoveAurasDueToSpell(GetSpellInfo()->Id);
            return SPELL_FAILED_DONT_REPORT;
        }

        if (player->IsInCombat())
            return SPELL_FAILED_AFFECTING_COMBAT;

        return SPELL_CAST_OK;
    }

    void Register() override
    {
        OnCheckCast += SpellCheckCastFn(spell_class_stance::CheckCast);
    }
};

class spell_class_stance_aura : public AuraScript
{
    void HandleApply(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        Player* player = GetTarget()->ToPlayer();
        uint8 const stanceClass = ClassStance::GetClassForSpell(GetId());
        if (!player || !stanceClass || !ClassStance::IsEnabled() || stanceClass == player->GetClass())
            return;

        // one class stance at a time
        for (uint32 spellId = ClassStance::FirstSpell; spellId <= ClassStance::LastSpell; ++spellId)
            if (spellId != GetId())
                player->RemoveAurasDueToSpell(spellId);

        player->SetClassStance(stanceClass);
    }

    void HandleRemove(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        Player* player = GetTarget()->ToPlayer();
        if (player && player->GetClassStance() == ClassStance::GetClassForSpell(GetId()))
            player->SetClassStance(0);
    }

    void Register() override
    {
        OnEffectApply += AuraEffectApplyFn(spell_class_stance_aura::HandleApply, EFFECT_0, SPELL_AURA_DUMMY, AURA_EFFECT_HANDLE_REAL);
        OnEffectRemove += AuraEffectRemoveFn(spell_class_stance_aura::HandleRemove, EFFECT_0, SPELL_AURA_DUMMY, AURA_EFFECT_HANDLE_REAL);
    }
};

void AddSC_class_stance()
{
    RegisterSpellAndAuraScriptPair(spell_class_stance, spell_class_stance_aura);
}

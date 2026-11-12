/*
 * Bonus loot (Classic 1.60 custom), see BonusLoot.h.
 */

#include "BonusLoot.h"
#include "Config.h"
#include "Creature.h"
#include "Formulas.h"
#include "Log.h"
#include "Player.h"
#include "Random.h"
#include <algorithm>

namespace
{
    BonusLoot::Config config;

    float Chance(char const* name, float defaultValue)
    {
        return std::clamp(sConfigMgr->GetFloatDefault(name, defaultValue), 0.0f, 100.0f);
    }

    float StatTomeChance(Creature const* creature, bool encounterBoss)
    {
        CreatureClassifications classification = creature->GetCreatureClassification();
        if (encounterBoss || creature->IsDungeonBoss() || creature->isWorldBoss() || classification == CreatureClassifications::Obsolete)
            return config.BossChance;

        switch (classification)
        {
            case CreatureClassifications::Elite:
                return config.EliteChance;
            case CreatureClassifications::RareElite:
                return config.RareEliteChance;
            case CreatureClassifications::Rare:
                return config.RareChance;
            default:
                return config.NormalChance;
        }
    }
}

namespace BonusLoot
{
    Config const& GetConfig()
    {
        return config;
    }

    void LoadConfig()
    {
        config.Enable          = sConfigMgr->GetBoolDefault("BonusLoot.Enable", true);
        config.NormalChance    = Chance("BonusLoot.StatTome.Normal", 1.0f);
        config.EliteChance     = Chance("BonusLoot.StatTome.Elite", 2.0f);
        config.RareChance      = Chance("BonusLoot.StatTome.Rare", 2.0f);
        config.RareEliteChance = Chance("BonusLoot.StatTome.RareElite", 3.0f);
        config.BossChance      = Chance("BonusLoot.StatTome.Boss", 10.0f);
        config.TalentChance    = Chance("BonusLoot.TalentTome", 0.1f);

        TC_LOG_INFO("server.loading", "Bonus loot: {} (stat Tome normal {}%, elite {}%, rare {}%, rare elite {}%, boss {}%; Tome of Talent {}%)",
            config.Enable ? "enabled" : "disabled", config.NormalChance, config.EliteChance, config.RareChance, config.RareEliteChance,
            config.BossChance, config.TalentChance);
    }

    std::vector<uint32> Roll(Creature const* creature, std::vector<Player*> const& tappers, bool encounterBoss)
    {
        std::vector<uint32> tomes;
        if (!config.Enable || !creature || tappers.empty())
            return tomes;

        if (creature->IsPet() || creature->IsSummon() || creature->IsTotem() || creature->IsCritter() || creature->IsTrigger() || !creature->CanHaveLoot())
            return tomes;

        CreatureClassifications classification = creature->GetCreatureClassification();
        if (classification == CreatureClassifications::MinusMob || classification == CreatureClassifications::Trivial)
            return tomes;

        // gray to any tapper: no farming low creatures, and no carrying low characters through them
        for (Player const* tapper : tappers)
            if (Trinity::XP::GetColorCode(tapper->GetLevel(), creature->GetLevelForTarget(tapper)) == XP_GRAY)
                return tomes;

        if (roll_chance(StatTomeChance(creature, encounterBoss)))
            tomes.push_back(FirstStatTome + urand(0, StatTomeCount - 1));
        if (roll_chance(config.TalentChance))
            tomes.push_back(TalentTome);
        return tomes;
    }
}

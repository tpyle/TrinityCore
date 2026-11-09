/*
 * World scaling (Classic 1.60), see WorldScale.h.
 */

#include "WorldScale.h"
#include "Config.h"
#include "Creature.h"
#include "Log.h"
#include "Map.h"
#include "Player.h"
#include <algorithm>
#include <cmath>

namespace
{
    WorldScale::Config config;
}

namespace WorldScale
{
    Config const& GetConfig()
    {
        return config;
    }

    void LoadConfig()
    {
        config.Enable            = sConfigMgr->GetBoolDefault("WorldScale.Enable", true);
        config.Dungeons          = sConfigMgr->GetBoolDefault("WorldScale.Dungeons", true);
        config.Raids             = sConfigMgr->GetBoolDefault("WorldScale.Raids", true);
        config.AffectElites      = sConfigMgr->GetBoolDefault("WorldScale.AffectElites", true);
        config.AffectWorldBosses = sConfigMgr->GetBoolDefault("WorldScale.AffectWorldBosses", false);
        config.MinMultiplier     = sConfigMgr->GetFloatDefault("WorldScale.MinMultiplier", 0.05f);
        config.MaxMultiplier     = sConfigMgr->GetFloatDefault("WorldScale.MaxMultiplier", 20.0f);
        config.KillXPFraction    = std::clamp(sConfigMgr->GetFloatDefault("WorldScale.KillXPFraction", 0.5f), 0.0f, 1.0f);
        config.AggroLevelsBelow  = sConfigMgr->GetIntDefault("WorldScale.Aggro.LevelsBelow", 5);

        if (config.MinMultiplier <= 0.0f)
            config.MinMultiplier = 0.05f;
        if (config.MaxMultiplier < config.MinMultiplier)
            config.MaxMultiplier = config.MinMultiplier;

        TC_LOG_INFO("server.loading", "World scaling: {} (dungeons {}, raids {}, elites {}, bosses {}, kill xp {}, aggro -{})",
            config.Enable ? "enabled" : "disabled", config.Dungeons, config.Raids, config.AffectElites, config.AffectWorldBosses,
            config.KillXPFraction, config.AggroLevelsBelow);
    }

    bool CanScale(Creature const* creature, uint8 level)
    {
        if (!config.Enable || !creature || level < 1 || level >= 60)
            return false;

        // anything a player summons or owns fights on the player's side; critters and triggers are not opponents
        if (creature->IsPet() || creature->IsTotem() || creature->IsSummon() || creature->IsCritter() || creature->IsTrigger())
            return false;

        if (creature->isWorldBoss() && !config.AffectWorldBosses)
            return false;

        if (creature->IsElite() && !config.AffectElites)
            return false;

        Map const* map = creature->FindMap();
        if (!map || map->IsBattlegroundOrArena())
            return false;

        // a raid is also a dungeon to Map, so it is asked first
        if (map->IsRaid())
            return config.Raids;
        if (map->IsDungeon())
            return config.Dungeons;

        return true;
    }

    bool IsPlayerSide(WorldObject const* target)
    {
        Unit const* unit = target ? target->ToUnit() : nullptr;
        return unit && unit->GetCharmerOrOwnerPlayerOrPlayerItself();
    }

    float Ratio(float currentValue, float targetValue)
    {
        if (currentValue <= 0.0f || targetValue <= 0.0f)
            return 1.0f;

        return std::clamp(targetValue / currentValue, config.MinMultiplier, config.MaxMultiplier);
    }

    uint32 BlendXP(uint32 original, uint32 scaled, float fraction)
    {
        if (scaled <= original)
            return original;

        return original + uint32(std::lround(double(scaled - original) * double(std::clamp(fraction, 0.0f, 1.0f))));
    }

    double ExactDamageTaken(Creature const* creature, Unit const* attacker, uint32 damage)
    {
        // the small addition keeps a hit that should take a whole number of points (9.9999...) from losing one to rounding
        return double(damage) / double(creature->GetHealthMultiplierForTarget(attacker)) + double(creature->GetWorldScaleDamageCarry()) + 1e-4;
    }

    int64 DamageBeyondHealth(Unit const* victim, Unit const* attacker, uint32 damage, uint64 health)
    {
        Creature const* creature = victim->ToCreature();
        if (!attacker || !creature || !creature->IsWorldScaled())
            return int64(damage) - int64(health);

        float const multiplier = victim->GetHealthMultiplierForTarget(attacker);
        double const taken = ExactDamageTaken(creature, attacker, damage);
        if (taken < double(health))
            return -1;

        return std::llround((taken - double(health)) * multiplier);
    }
}

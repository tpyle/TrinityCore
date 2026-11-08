/*
 * World scaling (Classic 1.60, port of the realm's AzerothCore mod-worldscale): a creature below a player's level fights,
 * looks and rewards as though it were the player's level, per player, using the engine's own level scaling.
 *
 * A vanilla creature that may scale is given the ContentTuning row ContentTuningBase + its level (hotfixes, levels
 * "its level" to 60). The client then shows each player the creature at their own level, clamped to that range, and the
 * server's per-target level (Creature::GetLevelForTarget) drives hit and avoidance rolls, aggro range, the damage, health
 * and armour multipliers and kill experience. Unlike retail, the creature keeps its own vanilla level and stats; the
 * multipliers come from the vanilla class/level table (creature_classic_classlevelstats), which is also the curve the
 * client uses to display scaled health.
 */

#ifndef TRINITY_WORLDSCALE_H
#define TRINITY_WORLDSCALE_H

#include "Define.h"

class Creature;
class Unit;
class WorldObject;

namespace WorldScale
{
    struct Config
    {
        bool Enable = true;
        bool Dungeons = true;           // scale in 5 player dungeons
        bool Raids = true;              // ...and raids (battlegrounds and arenas never)
        bool AffectElites = true;
        bool AffectWorldBosses = false; // creatures flagged as boss mobs (world and dungeon bosses)
        float MinMultiplier = 0.05f;
        float MaxMultiplier = 20.0f;
        float KillXPFraction = 0.5f;    // how far from the creature's at-its-own-level kill experience towards the player's to pay
        uint32 AggroLevelsBelow = 5;    // a scaled-up creature aggroes as if this many levels below the player
    };

    // ContentTuning rows ContentTuningBase + N (N = 1..59): levels N to 60 (custom-sql 055)
    constexpr int32 ContentTuningBase = 8100000;

    Config const& GetConfig();
    void LoadConfig();

    // Whether a vanilla creature at this level may scale; decided when it spawns (or respawns)
    bool CanScale(Creature const* creature, uint8 level);

    // Whether target is a player or something a player controls (pets, guardians): only they see scaled creatures
    bool IsPlayerSide(WorldObject const* target);

    // targetValue / currentValue, clamped to the configured multipliers; 1 when it cannot be formed
    float Ratio(float currentValue, float targetValue);

    // original + fraction of the way to scaled (never less than original)
    uint32 BlendXP(uint32 original, uint32 scaled, float fraction);

    // Combat log overkill of a hit not yet dealt: damage minus the victim's health, in the attacker's points. On a scaled victim
    // the attacker sees health x multiplier and the hit takes damage / multiplier (plus the carried fraction, see Unit::DealDamage),
    // so the plain damage - health would report overkill on hits it survives and the client would empty its health bar.
    int64 DamageBeyondHealth(Unit const* victim, Unit const* attacker, uint32 damage, uint64 health);
}

#endif // TRINITY_WORLDSCALE_H

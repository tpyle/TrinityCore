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
class Player;
class Quest;
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
        // a scaled creature fights and shows as this many levels below the player (never below its own level); 0: the player's level
        uint32 LevelsBelow = 0;
        float KillXPFraction = 0.5f;    // how far from the creature's at-its-own-level kill experience towards the player's to pay
        uint32 AggroLevelsBelow = 5;    // a scaled-up creature aggroes as if this many levels below the player
        bool Quests = true;             // quest experience below the player's level (custom-sql 057 makes the client show the level)
        float QuestXPFraction = 0.5f;   // how far from the quest's at-its-own-level experience towards the player's to pay
        bool GroupXPSplit = true;       // share a kill's experience across the group (false: each member earns what they would alone)
    };

    // ContentTuning rows ContentTuningBase + N (N = 1..59): levels N to 60 (custom-sql 055)
    constexpr int32 ContentTuningBase = 8100000;

    // Whether a quest is on one of those rows (custom-sql 057: vanilla quests of level N). The client shows such a quest at the
    // player's level, never below its own; the server keeps the quest's fixed level for its rewards.
    bool IsScaledQuest(Quest const* quest);

    // The quest level the client shows player: theirs for a scaled quest below them, else the quest's own
    int32 GetShownQuestLevel(Player const* player, Quest const* quest);

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
    // The health points a hit of damage takes from a scaled creature, with the fraction carried from earlier hits (Unit::DealDamage)
    double ExactDamageTaken(Creature const* creature, Unit const* attacker, uint32 damage);

    int64 DamageBeyondHealth(Unit const* victim, Unit const* attacker, uint32 damage, uint64 health);
}

#endif // TRINITY_WORLDSCALE_H

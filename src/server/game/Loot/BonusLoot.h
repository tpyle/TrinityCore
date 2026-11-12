/*
 * Bonus loot (Classic 1.60 custom): Tomes that grant bonus stats or a talent point drop from enemies everywhere.
 *
 * Rolled once per kill in Unit::Kill, on top of the creature's own loot table (a creature without one can still drop
 * them): a stat Tome (one of the 17, picked evenly) at a chance by the creature's kind, and the Tome of Talent at its own
 * chance, independently. The Tomes go in the corpse's loot: the group's shared loot where there is one, else the personal
 * loot of one tapper picked at random. Nothing drops from pets, summons, totems, critters, triggers, minus mobs, trivial
 * creatures, creatures that have no loot, or a creature that is gray to any of its tappers.
 *
 * The Tomes are custom-sql 066 (hotfixes, tools/bonus_tomes.py) and their spells' scripts are in scripts/Custom.
 */

#ifndef TRINITY_BONUSLOOT_H
#define TRINITY_BONUSLOOT_H

#include "Define.h"
#include <vector>

class Creature;
class Player;

namespace BonusLoot
{
    struct Config
    {
        bool Enable = true;
        // percent chances of a stat Tome
        float NormalChance = 1.0f;
        float EliteChance = 2.0f;
        float RareChance = 2.0f;
        float RareEliteChance = 3.0f;
        float BossChance = 10.0f;       // dungeon encounters and creatures flagged as dungeon or world bosses
        // percent chance of the Tome of Talent, from any of them
        float TalentChance = 0.1f;
    };

    // Must match custom-sql 066 (tools/bonus_tomes.py)
    constexpr uint32 TalentTome = 8104001;
    constexpr uint32 FirstStatTome = 8104002;
    constexpr uint32 StatTomeCount = 17;

    Config const& GetConfig();
    void LoadConfig();

    // The Tomes this kill drops, rolled once per kill: none, a stat Tome, the Tome of Talent, or both
    std::vector<uint32> Roll(Creature const* creature, std::vector<Player*> const& tappers, bool encounterBoss);
}

#endif // TRINITY_BONUSLOOT_H

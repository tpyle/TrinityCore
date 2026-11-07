/*
 * Teaches primary professions directly from the server.
 *
 * The client's trainer window stops a player at two primary professions, whatever
 * MaxPrimaryTradeSkill says. A gossip menu avoids that window and still respects the
 * configured limit.
 */

#include "Chat.h"
#include "Player.h"
#include "ScriptedCreature.h"
#include "ScriptedGossip.h"
#include "ScriptMgr.h"
#include <array>

namespace
{
struct TutorProfession
{
    char const* Name;
    uint32 SkillId;
    uint32 SpellId;  // Apprentice rank
    uint8 MinLevel;  // Same requirement as the normal trainers
};

constexpr std::array<TutorProfession, 9> TutorProfessions =
{ {
    { "Alchemy",        171, 2259, 5 },
    { "Blacksmithing",  164, 2018, 5 },
    { "Enchanting",     333, 7411, 5 },
    { "Engineering",    202, 4036, 5 },
    { "Herbalism",      182, 2366, 0 },
    { "Leatherworking", 165, 2108, 5 },
    { "Mining",         186, 2575, 0 },
    { "Skinning",       393, 8613, 0 },
    { "Tailoring",      197, 3908, 5 },
} };

constexpr uint32 NPC_TEXT_GREETING = 68;

bool CanLearn(Player const* player, TutorProfession const& profession)
{
    return !player->HasSkill(profession.SkillId)
        && player->GetLevel() >= profession.MinLevel
        && player->GetFreePrimaryProfessionPoints() > 0;
}
}

struct npc_profession_tutor : public ScriptedAI
{
    using ScriptedAI::ScriptedAI;

    bool OnGossipHello(Player* player) override
    {
        InitGossipMenuFor(player, 0);

        for (TutorProfession const& profession : TutorProfessions)
            if (CanLearn(player, profession))
                AddGossipItemFor(player, GossipOptionNpc::Trainer, std::string("Learn ") + profession.Name, GOSSIP_SENDER_MAIN, profession.SpellId);

        SendGossipMenuFor(player, NPC_TEXT_GREETING, me->GetGUID());
        return true;
    }

    bool OnGossipSelect(Player* player, uint32 /*menuId*/, uint32 gossipListId) override
    {
        uint32 const spellId = GetGossipActionFor(player, gossipListId);
        ClearGossipMenuFor(player);

        for (TutorProfession const& profession : TutorProfessions)
        {
            if (profession.SpellId != spellId)
                continue;

            // Checked again because the menu could be stale by the time it is clicked
            if (CanLearn(player, profession))
            {
                player->LearnSpell(profession.SpellId, false);
                ChatHandler(player->GetSession()).PSendSysMessage("You have learned %s.", profession.Name);
            }
            break;
        }

        return OnGossipHello(player);
    }
};

void AddSC_npc_profession_tutor()
{
    RegisterCreatureAI(npc_profession_tutor);
}

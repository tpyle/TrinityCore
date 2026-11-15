/*
 * Crafting from the bank (Crafting.ReagentsFromBank, Spell::CheckItems): the server counts the bank's reagents for a recipe,
 * but the client checks the reagents itself before it sends a cast ("Missing reagent") and counts only the bags. The
 * ForeverProfessions addon then asks for the craft with an addon whisper to the player themselves, prefix FPROF, and this
 * casts the recipe for them; the server's own checks (spell known, reagents with the bank, tools, spell focus) still apply.
 *
 *   CRAFT <recipe spell>              a recipe that makes an item
 *   ENCH <recipe spell> <item guid>   an enchant on an item the player carries
 *
 * One cast per message: the addon sends the next one of a "Create All" when the last succeeded.
 */

#include "ScriptMgr.h"
#include "Config.h"
#include "Item.h"
#include "ObjectGuid.h"
#include "Player.h"
#include "SharedDefines.h"
#include "Spell.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "StringConvert.h"
#include "Util.h"

namespace
{
    constexpr char AddonPrefix[] = "FPROF";

    void Craft(Player* player, std::string const& text)
    {
        if (!sConfigMgr->GetBoolDefault("Crafting.ReagentsFromBank", true))
            return;

        std::vector<std::string_view> args = Trinity::Tokenize(text, ' ', false);
        if (args.size() < 2)
            return;
        bool const enchant = args[0] == "ENCH";
        if (!enchant && args[0] != "CRAFT")
            return;

        Optional<uint32> spellId = Trinity::StringTo<uint32>(args[1]);
        if (!spellId || !player->HasSpell(*spellId))
            return;
        SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(*spellId, DIFFICULTY_NONE);
        if (!spellInfo || !spellInfo->HasAttribute(SPELL_ATTR0_IS_TRADESKILL))
            return;
        if (player->IsNonMeleeSpellCast(false))
            return;

        SpellCastTargets targets;
        if (enchant)
        {
            if (args.size() < 3)
                return;
            ObjectGuid const itemGuid = ObjectGuid::FromString(args[2]);
            Item* item = itemGuid.IsEmpty() ? nullptr : player->GetItemByGuid(itemGuid);
            if (!item)
                return;
            targets.SetItemTarget(item);
        }
        else
            targets.SetUnitTarget(player);

        player->CastSpell(std::move(targets), *spellId, CastSpellExtraArgs(TRIGGERED_NONE));
    }
}

class bank_crafting_playerscript : public PlayerScript
{
public:
    bank_crafting_playerscript() : PlayerScript("bank_crafting_playerscript") { }

    // a whisper to themselves; nothing reaches another player
    void OnAddonMessageSent(Player* player, uint32 type, std::string const& prefix, std::string const& text, Player* receiver) override
    {
        if (type == CHAT_MSG_WHISPER && receiver == player && prefix == AddonPrefix)
            Craft(player, text);
    }
};

void AddSC_bank_crafting()
{
    new bank_crafting_playerscript();
}

/*
 * Class stances (Classic 1.60 realm custom): a player buys another class's stance from that class's trainer, then learns
 * that class's spells there as their own class does. The stance (a stance bar aura, hotfix spells 8104019-8104027, custom-sql
 * 071) makes the player fight as that class: its resource (every class has a slot for mana, rage, energy and combo points,
 * 071), its attack power, crit, dodge, parry and regeneration formulas. The real class stays: talents, specialization, the
 * talent window and what other players see. A learned stance also lets the player hold that class's skill lines and wear
 * what that class wears, whether the stance is active or not.
 */

#ifndef TRINITY_CLASSSTANCE_H
#define TRINITY_CLASSSTANCE_H

#include "Define.h"
#include "ObjectGuid.h"
#include <iterator>
#include <string>
#include <vector>

class Player;
class SpellInfo;
struct SkillRaceClassInfoEntry;
enum SpellCastResult : int32;

namespace ClassStance
{
    // the stance spells, one per class; custom-sql 071 and tools/class_stances.py write them in this order
    constexpr uint32 FirstSpell = 8104019;
    constexpr uint8 Classes[] = { 1, 2, 3, 4, 5, 7, 8, 9, 11 };    // warrior paladin hunter rogue priest shaman mage warlock druid
    constexpr uint32 LastSpell = FirstSpell + std::size(Classes) - 1;

    bool IsEnabled();
    void LoadConfig();

    // the class a stance spell stands for, 0 for any other spell
    uint8 GetClassForSpell(uint32 spellId);
    // the stance spell of a class, 0 for a class without one
    uint32 GetSpellForClass(uint8 classId);

    // the class mask of every class whose stance the player knows (the real class not included)
    uint32 GetLearnedClassMask(Player const* player);
    // the player is of this class or has learned its stance (taming, demons, stable masters, class quests)
    bool KnowsClass(Player const* player, uint8 classId);

    // the SkillRaceClassInfo entry that lets the player hold a skill: the real class's (race and class), else that of a learned
    // stance's class. For a stance class a class, armor or weapon skill line ignores the race: a human may learn the shaman's
    // lines without being a race the shaman's data names. Every stance class counts when anyStance (skills load before spells).
    SkillRaceClassInfoEntry const* GetSkillRaceClassInfo(Player const* player, uint32 skill, uint8 race, bool anyStance = false);

    // the class a spell belongs to by its spell family (ChrClasses.SpellClassSet), 0 for a spell of no class
    uint8 GetClassOfSpell(SpellInfo const* spellInfo);

    // a player who has learned a class stance casts a class's abilities only while fighting as that class: the real class's
    // with no class stance active, a stance class's in its stance. Spells of classes the player has no stance for (item and
    // proc spells) are not checked. With notify, a refusal tells the player which stance to take or leave (the client has no
    // error text for a class stance) and is not reported further.
    SpellCastResult CheckClassSpell(Player const* player, SpellInfo const* spellInfo, bool notify);

    // the stance class's skill lines and its armor and weapon skills, as a new character of that class gets them
    void LearnClassSkills(Player* player, uint8 classId);

    // Stance talents (WoW Forever's trait trees, custom-sql 075; the ForeverStances addon draws them, 073 keeps the picks by trait
    // node entry): every learned stance class has its own pool of talent points, as many as the real class owns (levels,
    // Rate.Talent, granted points and Tomes of Talent), spent by the trees' rules (the points in the tab each row asks,
    // prerequisites at full rank). A talent is its trait's spell with the rank as a trait, as the real class's talents are. They
    // are active at all times, or with ClassStance.TalentsAlwaysActive = 0 only while their class's stance is.
    bool TalentsAlwaysActive();
    // the stance talent trees, world.class_stance_talent (custom-sql 075); at startup
    void LoadTalentNodes();
    // ClassStance.AllowInCombat: stances may be changed in combat (the addon's bars, keys and panel still change only out of
    // combat: the client does not let an addon change them in combat)
    bool AllowInCombat();
    // ClassStance.SharedTalentPoints: one pool for the real class and every stance class instead of a full pool each. The
    // client's own talent window cannot know what the stances spent: it shows those points as unspent, and the server refuses
    // a commit that spends them (CheckRealTalentCommit).
    bool SharedTalentPoints();
    // what every stance class has spent
    uint32 GetAllStanceSpentTalentPoints(Player const* player);
    // a commit of the real class's talents (the client's window): "" when allowed, else why not (only with shared points)
    std::string CheckRealTalentCommit(Player const* player, std::vector<int32> const& oldAndNewSpent);
    uint32 GetTalentPoints(Player const* player, uint8 classId);
    uint32 GetSpentTalentPoints(Player const* player, uint8 classId);
    // "" when learned, else why not
    std::string LearnTalent(Player* player, uint8 classId, uint32 talentId);
    void ResetTalents(Player* player, uint8 classId);
    // login: read the picks and apply those that are active
    void LoadTalents(Player* player);
    void DeleteTalents(ObjectGuid guid);
    // a class stance began or ended (Player::SetClassStance)
    void OnStanceChanged(Player* player, uint8 oldClass);

    // the addon's messages (prefix AddonPrefix, whispers the player sends to themselves) and the server's answers
    constexpr char AddonPrefix[] = "FSTANCE";
    void HandleAddonMessage(Player* player, std::string const& text);
    void SendState(Player* player);
    void SendTalents(Player* player, uint8 classId);
    void SendSkills(Player* player);
    // the stance class's spells the player knows, by skill line (the addon's stance spellbook)
    void SendSpells(Player* player, uint8 classId);
}

#endif

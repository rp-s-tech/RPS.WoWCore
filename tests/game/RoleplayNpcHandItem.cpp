/*
 * Custom NPC hand copy: only items that survive ObjectMgr::LoadEquipmentTemplates after a restart.
 */

#include "RolePlay.h"

#include "catch2/catch_test_macros.hpp"

TEST_CASE("Custom NPC hand copy keeps weapons, shields and held items", "[roleplay][customnpc]")
{
    REQUIRE(Roleplay::IsNpcHandItem(INVTYPE_WEAPON));
    REQUIRE(Roleplay::IsNpcHandItem(INVTYPE_2HWEAPON));
    REQUIRE(Roleplay::IsNpcHandItem(INVTYPE_WEAPONMAINHAND));
    REQUIRE(Roleplay::IsNpcHandItem(INVTYPE_WEAPONOFFHAND));
    REQUIRE(Roleplay::IsNpcHandItem(INVTYPE_SHIELD));
    REQUIRE(Roleplay::IsNpcHandItem(INVTYPE_HOLDABLE));
    REQUIRE(Roleplay::IsNpcHandItem(INVTYPE_RANGED));
    REQUIRE(Roleplay::IsNpcHandItem(INVTYPE_RANGEDRIGHT));
}

TEST_CASE("Custom NPC hand copy rejects armor shown in a hand by .disp", "[roleplay][customnpc]")
{
    // .disp mainhand <helm>, a Hidden item or ammo: the core would empty this hand on the next startup
    for (InventoryType notHandItem : { INVTYPE_NON_EQUIP, INVTYPE_HEAD, INVTYPE_SHOULDERS, INVTYPE_BODY, INVTYPE_CHEST, INVTYPE_ROBE,
        INVTYPE_WAIST, INVTYPE_LEGS, INVTYPE_FEET, INVTYPE_WRISTS, INVTYPE_HANDS, INVTYPE_CLOAK, INVTYPE_TABARD, INVTYPE_AMMO })
        REQUIRE_FALSE(Roleplay::IsNpcHandItem(notHandItem));
}

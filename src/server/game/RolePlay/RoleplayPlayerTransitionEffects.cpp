#include "RoleplayPlayerTransitionEffects.h"

#include "Corpse.h"
#include "GossipDef.h"
#include "Map.h"
#include "ObjectGuid.h"
#include "Player.h"

namespace RoleplayPlayerTransitionEffects
{
void Cleanup(Player* player)
{
    if (!player || !player->IsInWorld())
        return;

    player->InterruptNonMeleeSpells(true);
    player->CombatStopWithPets(true);
    player->ClearInCombat();
    player->TradeCancel(true);
    player->DuelComplete(DUEL_INTERRUPTED);
    player->PlayerTalkClass->SendCloseGossip();
    player->PlayerTalkClass->ClearMenus();
    player->SendLootReleaseAll();
    player->SetLootGUID(ObjectGuid::Empty);
    player->SetSelection(ObjectGuid::Empty);
}

void RefreshVisibility(Player* player)
{
    if (!player || !player->IsInWorld())
        return;

    Map* map = player->GetMap();
    if (!map)
        return;

    player->UpdateObjectVisibility(true);
    for (Unit* controlled : player->m_Controlled)
        if (controlled->IsInWorld())
            controlled->UpdateObjectVisibility(true);
    if (Corpse* corpse = player->GetCorpse(); corpse && corpse->IsInWorld())
        corpse->UpdateObjectVisibility(true);

    map->SendUpdateTransportVisibility(player);
    map->DoOnPlayers([player](Player* nearby)
    {
        if (nearby != player && player->IsWithinDist(nearby, player->GetVisibilityRange()))
            nearby->UpdateObjectVisibility(true);
    });
}

void Apply(Player* player, uint64 previousPhaseId, uint64 currentPhaseId,
    void (*handler)(Player* player, uint64 previousPhaseId, uint64 currentPhaseId) /*= nullptr*/)
{
    if (!player)
        return;

    if (previousPhaseId != currentPhaseId)
        Cleanup(player);

    RefreshVisibility(player);

    if (handler)
        handler(player, previousPhaseId, currentPhaseId);
}
}

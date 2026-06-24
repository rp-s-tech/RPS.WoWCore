/*
 * NobleNext — companion pet commands.
 */

#include "noble_next_staff.h"

#include "Chat.h"
#include "ChatCommand.h"
#include "ChatCommandTags.h"
#include "Creature.h"
#include "MotionMaster.h"
#include "Player.h"
#include "RBAC.h"
#include "SharedDefines.h"
#include "SpellAuras.h"

namespace RoleplayCore::NobleNext
{
    using namespace Trinity::ChatCommands;

    constexpr uint32 PET_BLOCK_AURA = 91072;

    static Creature* GetControlledPet(Player* player, ChatHandler* handler)
    {
        Unit* sel = handler->getSelectedUnit();
        if (!sel || !sel->IsCreature())
        {
            handler->SendSysMessage("Выберите спутника.");
            return nullptr;
        }
        Creature* c = sel->ToCreature();
        if (c->GetCharmerOrOwnerGUID() != player->GetGUID())
        {
            handler->SendSysMessage("Вы не можете управлять этим существом.");
            return nullptr;
        }
        if (c->HasAura(PET_BLOCK_AURA))
        {
            handler->SendSysMessage("Спутник заблокирован.");
            return nullptr;
        }
        return c;
    }

    class PetCommands : public CommandScript
    {
    public:
        PetCommands() : CommandScript("noble_next_pet_commands") { }

        std::span<ChatCommandBuilder const> GetCommands() const override
        {
            static ChatCommandTable petTable =
            {
                { "",       HandlePetHelp,   rbac::RBAC_PERM_COMMAND_GM_INGAME, Console::No },
                { "say",    HandlePetSay,    rbac::RBAC_PERM_COMMAND_GM_INGAME, Console::No },
                { "emote",  HandlePetEmote,  rbac::RBAC_PERM_COMMAND_GM_INGAME, Console::No },
                { "stay",   HandlePetStay,   rbac::RBAC_PERM_COMMAND_GM_INGAME, Console::No },
                { "follow", HandlePetFollow, rbac::RBAC_PERM_COMMAND_GM_INGAME, Console::No },
                { "play",   HandlePetPlay,   rbac::RBAC_PERM_COMMAND_GM_INGAME, Console::No },
                { "pos",    HandlePetPos,    rbac::RBAC_PERM_COMMAND_GM_INGAME, Console::No },
                { "tele",   HandlePetTele,   rbac::RBAC_PERM_COMMAND_GM_INGAME, Console::No },
            };

            static ChatCommandTable commandTable =
            {
                { "pet", petTable },
            };
            return commandTable;
        }

        static bool HandlePetHelp(ChatHandler* handler)
        {
            handler->SendSysMessage(".pet say|emote|stay|follow|play|pos|tele — управление спутником в цели.");
            return true;
        }

        static bool HandlePetSay(ChatHandler* handler, Tail text)
        {
            Player* player = handler->GetPlayer();
            Creature* pet = GetControlledPet(player, handler);
            if (!pet) return false;
            pet->Say(std::string(text), LANG_UNIVERSAL);
            return true;
        }

        static bool HandlePetEmote(ChatHandler* handler, Tail text)
        {
            Player* player = handler->GetPlayer();
            Creature* pet = GetControlledPet(player, handler);
            if (!pet) return false;
            pet->TextEmote(std::string(text), player);
            return true;
        }

        static bool HandlePetStay(ChatHandler* handler)
        {
            Player* player = handler->GetPlayer();
            Creature* pet = GetControlledPet(player, handler);
            if (!pet) return false;
            pet->GetMotionMaster()->Clear();
            pet->StopMoving();
            return true;
        }

        static bool HandlePetFollow(ChatHandler* handler, Optional<float> dist, Optional<float> /*angle*/)
        {
            Player* player = handler->GetPlayer();
            Creature* pet = GetControlledPet(player, handler);
            if (!pet) return false;
            float followDist = dist.value_or(3.f);
            pet->GetMotionMaster()->MoveFollow(player, followDist, pet->GetFollowAngle());
            return true;
        }

        static bool HandlePetPlay(ChatHandler* handler, uint32 emoteId, Optional<uint8> repeat)
        {
            Player* player = handler->GetPlayer();
            Creature* pet = GetControlledPet(player, handler);
            if (!pet) return false;
            if (repeat)
                pet->SetEmoteState(static_cast<Emote>(emoteId));
            else
                pet->HandleEmoteCommand(static_cast<Emote>(emoteId));
            return true;
        }

        static bool HandlePetPos(ChatHandler* handler, uint8 pos)
        {
            Player* player = handler->GetPlayer();
            Creature* pet = GetControlledPet(player, handler);
            if (!pet) return false;
            pet->SetStandState(static_cast<UnitStandStateType>(pos));
            return true;
        }

        static bool HandlePetTele(ChatHandler* handler)
        {
            Player* player = handler->GetPlayer();
            Creature* pet = GetControlledPet(player, handler);
            if (!pet) return false;
            pet->NearTeleportTo(player->GetPositionX(), player->GetPositionY(), player->GetPositionZ(), player->GetOrientation());
            return true;
        }
    };
}

void AddSC_NobleNextPetCommands()
{
    new RoleplayCore::NobleNext::PetCommands();
}

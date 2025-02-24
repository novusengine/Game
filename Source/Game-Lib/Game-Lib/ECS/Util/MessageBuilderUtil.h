#pragma once
#include "Game-Lib/ECS/Components/MovementInfo.h"
#include "Game-Lib/Gameplay/Database/Item.h"

#include <Base/Types.h>
#include <Base/Memory/Bytebuffer.h>

#include "Gameplay/GameDefine.h"
#include "Gameplay/Network/Opcode.h"

#include <entt/fwd.hpp>

#include <functional>
#include <limits>
#include <memory>

namespace ClientDB
{
    struct Data;
}

namespace ECS
{
    namespace Components
    {
        struct UnitStatsComponent;
        struct Transform;
    }

    namespace Util::MessageBuilder
    {
        u32 AddHeader(std::shared_ptr<Bytebuffer>& buffer, Network::GameOpcode opcode, Network::MessageHeader::Flags flags = {}, u16 size = 0);
        bool ValidatePacket(const std::shared_ptr<Bytebuffer>& buffer, u32 headerPos);
        bool CreatePacket(std::shared_ptr<Bytebuffer>& buffer, Network::GameOpcode opcode, std::function<void()> func);
        bool CreatePing(std::shared_ptr<Bytebuffer>& buffer, std::function<void()> func);

        namespace Authentication
        {
            bool BuildConnectMessage(std::shared_ptr<Bytebuffer>& buffer, const std::string& charName);
        }

        namespace Heartbeat
        {
            bool BuildPingMessage(std::shared_ptr<Bytebuffer>& buffer, u16 ping);
        }

        namespace Entity
        {
            bool BuildMoveMessage(std::shared_ptr<Bytebuffer>& buffer, const vec3& position, const quat& rotation, const Components::MovementFlags& movementFlags, f32 verticalVelocity);
            bool BuildTargetUpdateMessage(std::shared_ptr<Bytebuffer>& buffer, GameDefine::ObjectGuid targetGuid);
        }

        namespace Container
        {
            bool BuildRequestSwapSlots(std::shared_ptr<Bytebuffer>& buffer, u8 srcContainerIndex, u8 destContainerIndex, u8 srcSlotIndex, u8 destSlotIndex);
        }

        namespace Spell
        {
            bool BuildLocalRequestSpellCast(std::shared_ptr<Bytebuffer>& buffer);
        }

        namespace CombatLog
        {
        }

        namespace Cheat
        {
            bool BuildCheatDamage(std::shared_ptr<Bytebuffer>& buffer, u32 damage);
            bool BuildCheatKill(std::shared_ptr<Bytebuffer>& buffer);
            bool BuildCheatHeal(std::shared_ptr<Bytebuffer>& buffer, u32 heal);
            bool BuildCheatResurrect(std::shared_ptr<Bytebuffer>& buffer);
            bool BuildCheatMorph(std::shared_ptr<Bytebuffer>& buffer, u32 displayID);
            bool BuildCheatDemorph(std::shared_ptr<Bytebuffer>& buffer);
            bool BuildCheatTeleport(std::shared_ptr<Bytebuffer>& buffer, u32 mapID, const vec3& position);
            bool BuildCheatCreateChar(std::shared_ptr<Bytebuffer>& buffer, const std::string& name);
            bool BuildCheatDeleteChar(std::shared_ptr<Bytebuffer>& buffer, const std::string& name);
            bool BuildCheatSetRace(std::shared_ptr<Bytebuffer>& buffer, GameDefine::UnitRace race);
            bool BuildCheatSetGender(std::shared_ptr<Bytebuffer>& buffer, GameDefine::Gender gender);
            bool BuildCheatSetClass(std::shared_ptr<Bytebuffer>& buffer, GameDefine::UnitClass unitClass);
            bool BuildCheatSetLevel(std::shared_ptr<Bytebuffer>& buffer, u16 level);
            bool BuildCheatSetItemTemplate(std::shared_ptr<Bytebuffer>& buffer, ClientDB::Data* itemStorage, u32 itemID, const Database::Item::Item& item);
            bool BuildCheatSetItemStatTemplate(std::shared_ptr<Bytebuffer>& buffer, ClientDB::Data* statTemplateStorage, u32 statTemplateID, const Database::Item::ItemStatTemplate& statTemplate);
            bool BuildCheatSetItemArmorTemplate(std::shared_ptr<Bytebuffer>& buffer, ClientDB::Data* armorTemplateStorage, u32 armorTemplateID, const Database::Item::ItemArmorTemplate& armorTemplate);
            bool BuildCheatSetItemWeaponTemplate(std::shared_ptr<Bytebuffer>& buffer, ClientDB::Data* weaponTemplateStorage, u32 weaponTemplateID, const Database::Item::ItemWeaponTemplate& weaponTemplate);
            bool BuildCheatSetItemShieldTemplate(std::shared_ptr<Bytebuffer>& buffer, ClientDB::Data* shieldTemplateStorage, u32 shieldTemplateID, const Database::Item::ItemShieldTemplate& shieldTemplate);
            bool BuildCheatAddItem(std::shared_ptr<Bytebuffer>& buffer, u32 itemID, u32 itemCount);
            bool BuildCheatRemoveItem(std::shared_ptr<Bytebuffer>& buffer, u32 itemID, u32 itemCount);
        }
    }
}

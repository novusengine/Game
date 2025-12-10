#pragma once
#include "Game-Lib/ECS/Components/MovementInfo.h"
#include "Game-Lib/Gameplay/Database/Item.h"

#include <Base/Types.h>
#include <Base/Memory/Bytebuffer.h>

#include <Gameplay/GameDefine.h>

#include <MetaGen/PacketList.h>
#include <MetaGen/Game/Command/Command.h>
#include <MetaGen/Shared/ClientDB/ClientDB.h>

#include <Network/Define.h>

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
        u32 AddHeader(std::shared_ptr<Bytebuffer>& buffer, ::Network::OpcodeType opcode, u16 size = 0);
        bool ValidatePacket(const std::shared_ptr<Bytebuffer>& buffer, u32 headerPos);
        bool CreatePacket(std::shared_ptr<Bytebuffer>& buffer, ::Network::OpcodeType opcode, std::function<void()> func);

        namespace Authentication
        {
            bool BuildConnectMessage(std::shared_ptr<Bytebuffer>& buffer, const std::string& charName);
        }

        namespace Heartbeat
        {
            bool BuildPingMessage(std::shared_ptr<Bytebuffer>& buffer, u16 ping);
        }

        namespace Unit
        {
            bool BuildUnitMoveMessage(std::shared_ptr<Bytebuffer>& buffer, const vec3& position, const vec2& pitchYaw, const Components::MovementFlags& movementFlags, f32 verticalVelocity);
            bool BuildUnitTargetUpdateMessage(std::shared_ptr<Bytebuffer>& buffer, ObjectGUID targetGUID);
        }

        namespace Container
        {
            bool BuildRequestSwapSlots(std::shared_ptr<Bytebuffer>& buffer, u16 srcContainerIndex, u16 destContainerIndex, u16 srcSlotIndex, u16 destSlotIndex);
        }

        namespace Spell
        {
            bool BuildSpellCast(std::shared_ptr<Bytebuffer>& buffer, u32 spellID);
        }

        namespace Chat
        {
            bool BuildChatMessage(std::shared_ptr<Bytebuffer>& buffer, const std::string& message);
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
            bool BuildCheatUnitMorph(std::shared_ptr<Bytebuffer>& buffer, u32 displayID);
            bool BuildCheatUnitDemorph(std::shared_ptr<Bytebuffer>& buffer);
            bool BuildCheatTeleport(std::shared_ptr<Bytebuffer>& buffer, u32 mapID, const vec3& position);
            bool BuildCheatCharacterAdd(std::shared_ptr<Bytebuffer>& buffer, const std::string& name);
            bool BuildCheatCharacterRemove(std::shared_ptr<Bytebuffer>& buffer, const std::string& name);
            bool BuildCheatUnitSetRace(std::shared_ptr<Bytebuffer>& buffer, GameDefine::UnitRace race);
            bool BuildCheatUnitSetGender(std::shared_ptr<Bytebuffer>& buffer, GameDefine::UnitGender gender);
            bool BuildCheatUnitSetClass(std::shared_ptr<Bytebuffer>& buffer, GameDefine::UnitClass unitClass);
            bool BuildCheatUnitSetLevel(std::shared_ptr<Bytebuffer>& buffer, u16 level);
            bool BuildCheatItemSetTemplate(std::shared_ptr<Bytebuffer>& buffer, ClientDB::Data* itemStorage, u32 itemID, const MetaGen::Shared::ClientDB::ItemRecord& item);
            bool BuildCheatItemSetStatTemplate(std::shared_ptr<Bytebuffer>& buffer, ClientDB::Data* statTemplateStorage, u32 statTemplateID, const MetaGen::Shared::ClientDB::ItemStatTemplateRecord& statTemplate);
            bool BuildCheatItemSetArmorTemplate(std::shared_ptr<Bytebuffer>& buffer, ClientDB::Data* armorTemplateStorage, u32 armorTemplateID, const MetaGen::Shared::ClientDB::ItemArmorTemplateRecord& armorTemplate);
            bool BuildCheatItemSetWeaponTemplate(std::shared_ptr<Bytebuffer>& buffer, ClientDB::Data* weaponTemplateStorage, u32 weaponTemplateID, const MetaGen::Shared::ClientDB::ItemWeaponTemplateRecord& weaponTemplate);
            bool BuildCheatItemSetShieldTemplate(std::shared_ptr<Bytebuffer>& buffer, ClientDB::Data* shieldTemplateStorage, u32 shieldTemplateID, const MetaGen::Shared::ClientDB::ItemShieldTemplateRecord& shieldTemplate);
            bool BuildCheatItemAdd(std::shared_ptr<Bytebuffer>& buffer, u32 itemID, u32 itemCount);
            bool BuildCheatItemRemove(std::shared_ptr<Bytebuffer>& buffer, u32 itemID, u32 itemCount);
            bool BuildCheatCreatureAdd(std::shared_ptr<Bytebuffer>& buffer, u32 creatureTemplateID);
            bool BuildCheatCreatureRemove(std::shared_ptr<Bytebuffer>& buffer, ObjectGUID guid);
            bool BuildCheatCreatureInfo(std::shared_ptr<Bytebuffer>& buffer, ObjectGUID guid);
            bool BuildCheatMapAdd(std::shared_ptr<Bytebuffer>& buffer, ClientDB::Data* mapStorage, u32 mapID, const MetaGen::Shared::ClientDB::MapRecord& map);
            bool BuildCheatGotoAdd(std::shared_ptr<Bytebuffer>& buffer, const MetaGen::Game::Command::GotoAddCommand& command);
            bool BuildCheatGotoAddHere(std::shared_ptr<Bytebuffer>& buffer, const MetaGen::Game::Command::GotoAddHereCommand& command);
            bool BuildCheatGotoRemove(std::shared_ptr<Bytebuffer>& buffer, const MetaGen::Game::Command::GotoRemoveCommand& command);
            bool BuildCheatGotoMap(std::shared_ptr<Bytebuffer>& buffer, const MetaGen::Game::Command::GotoMapCommand& command);
            bool BuildCheatGotoLocation(std::shared_ptr<Bytebuffer>& buffer, const MetaGen::Game::Command::GotoLocationCommand& command);
            bool BuildCheatGotoXYZ(std::shared_ptr<Bytebuffer>& buffer, const MetaGen::Game::Command::GotoXYZCommand& command);
            bool BuildCheatTriggerAdd(std::shared_ptr<Bytebuffer>& buffer, const std::string& name, u16 flags, u16 mapID, const vec3& position, const vec3& extents);
            bool BuildCheatTriggerRemove(std::shared_ptr<Bytebuffer>& buffer, u32 triggerID);
            bool BuildCheatSpellSet(std::shared_ptr<Bytebuffer>& buffer, ClientDB::Data* spellStorage, u32 spellID, const MetaGen::Shared::ClientDB::SpellRecord& spell);
            bool BuildCheatSpellEffectSet(std::shared_ptr<Bytebuffer>& buffer, ClientDB::Data* spellEffectsStorage, u32 spellEffectsID, const MetaGen::Shared::ClientDB::SpellEffectsRecord& spellEffect);
            bool BuildCheatSpellProcDataSet(std::shared_ptr<Bytebuffer>& buffer, ClientDB::Data* spellProcDataStorage, u32 spellProcDataID, const MetaGen::Shared::ClientDB::SpellProcDataRecord& spellProcData);
            bool BuildCheatSpellProcLinkSet(std::shared_ptr<Bytebuffer>& buffer, ClientDB::Data* spellProcLinkStorage, u32 spellProcLinkID, const MetaGen::Shared::ClientDB::SpellProcLinkRecord& spellProcLink);
            bool BuildCreatureAddScript(std::shared_ptr<Bytebuffer>& buffer, const std::string& scriptName);
            bool BuildCreatureRemoveScript(std::shared_ptr<Bytebuffer>& buffer);
        }
    }
}

#pragma once
#include "Game-Lib/ECS/Components/MovementInfo.h"
#include "Game-Lib/Gameplay/Database/Item.h"

#include <Base/Types.h>
#include <Base/Memory/Bytebuffer.h>

#include <Gameplay/GameDefine.h>

#include <MetaGen/PacketList.h>
#include <MetaGen/Game/Command/Command.h>
#include <MetaGen/Shared/ClientDB/ClientDB.h>
#include <MetaGen/Shared/Spell/Spell.h>

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
            bool BuildSpellCast(std::shared_ptr<Bytebuffer>& buffer, u32 spellID, ObjectGUID targetGUID, const vec3& targetPosition);
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
            bool BuildCheatFactionReaction(std::shared_ptr<Bytebuffer>& buffer, ObjectGUID observerGUID, ObjectGUID targetGUID);
            bool BuildCheatUnitSetFaction(std::shared_ptr<Bytebuffer>& buffer, u16 factionID);
            bool BuildCheatFactionReputationInfo(std::shared_ptr<Bytebuffer>& buffer, ObjectGUID characterGUID, u16 factionID);
            bool BuildCheatFactionReputationSet(std::shared_ptr<Bytebuffer>& buffer, ObjectGUID characterGUID, u16 factionID, i32 value);
            bool BuildCheatFactionReputationModify(std::shared_ptr<Bytebuffer>& buffer, ObjectGUID characterGUID, u16 factionID, i32 delta);
            bool BuildCheatFactionReputationRemove(std::shared_ptr<Bytebuffer>& buffer, ObjectGUID characterGUID, u16 factionID);
            bool BuildCheatFactionReputationSetFlags(std::shared_ptr<Bytebuffer>& buffer, ObjectGUID characterGUID, u16 factionID, u16 flags);
            bool BuildCheatFactionReputationLock(std::shared_ptr<Bytebuffer>& buffer, ObjectGUID characterGUID, u16 factionID, bool locked);
            bool BuildCheatMapAdd(std::shared_ptr<Bytebuffer>& buffer, ClientDB::Data* mapStorage, u32 mapID, const MetaGen::Shared::ClientDB::MapRecord& map);
            bool BuildCheatGotoAdd(std::shared_ptr<Bytebuffer>& buffer, const MetaGen::Game::Command::GotoAddCommand& command);
            bool BuildCheatGotoAddHere(std::shared_ptr<Bytebuffer>& buffer, const MetaGen::Game::Command::GotoAddHereCommand& command);
            bool BuildCheatGotoRemove(std::shared_ptr<Bytebuffer>& buffer, const MetaGen::Game::Command::GotoRemoveCommand& command);
            bool BuildCheatGotoMap(std::shared_ptr<Bytebuffer>& buffer, const MetaGen::Game::Command::GotoMapCommand& command);
            bool BuildCheatGotoLocation(std::shared_ptr<Bytebuffer>& buffer, const MetaGen::Game::Command::GotoLocationCommand& command);
            bool BuildCheatGotoXYZ(std::shared_ptr<Bytebuffer>& buffer, const MetaGen::Game::Command::GotoXYZCommand& command);
            bool BuildCheatTriggerAdd(std::shared_ptr<Bytebuffer>& buffer, const std::string& name, u16 flags, u16 mapID, const vec3& position, const vec3& extents);
            bool BuildCheatTriggerRemove(std::shared_ptr<Bytebuffer>& buffer, u32 triggerID);
            bool BuildCheatSpellSync(std::shared_ptr<Bytebuffer>& buffer, u32 requestID, MetaGen::Shared::Spell::SpellEditorMutationTypeEnum mutationType, const u8* bytes, u32 size);
            bool BuildCheatSpellDelete(std::shared_ptr<Bytebuffer>& buffer, u32 requestID, u32 spellID);
            bool BuildCheatSpellEditorSnapshotRequest(std::shared_ptr<Bytebuffer>& buffer, u32 requestID);
            bool BuildCheatSpellAuraConstraintGroupSet(std::shared_ptr<Bytebuffer>& buffer, u32 requestID, MetaGen::Shared::Spell::SpellEditorMutationTypeEnum mutationType, u32 groupID, std::string_view name, u8 defaultScope, u16 defaultMaximumApplications, u8 defaultOverflowBehavior);
            bool BuildCheatSpellAuraConstraintGroupDelete(std::shared_ptr<Bytebuffer>& buffer, u32 requestID, u32 groupID);
            bool BuildCheatSpellProcDataSet(std::shared_ptr<Bytebuffer>& buffer, u32 requestID, MetaGen::Shared::Spell::SpellEditorMutationTypeEnum mutationType, const GameDefine::Database::SpellProcData& value, u32 ownerSpellID, std::string_view name);
            bool BuildCheatSpellProcDataDelete(std::shared_ptr<Bytebuffer>& buffer, u32 requestID, u32 procDataID);
            bool BuildCreatureAddScript(std::shared_ptr<Bytebuffer>& buffer, const std::string& scriptName);
            bool BuildCreatureRemoveScript(std::shared_ptr<Bytebuffer>& buffer);
            bool BuildCreatureMove(std::shared_ptr<Bytebuffer>& buffer);
            bool BuildCreatureFollow(std::shared_ptr<Bytebuffer>& buffer);
            bool BuildCreatureWander(std::shared_ptr<Bytebuffer>& buffer);
            bool BuildCreatureStop(std::shared_ptr<Bytebuffer>& buffer);
        }
    }
}

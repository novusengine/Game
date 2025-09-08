#include "MessageBuilderUtil.h"
#include "Game-Lib/ECS/Components/UnitStatsComponent.h"
#include "Game-Lib/ECS/Singletons/Database/ClientDBSingleton.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <FileFormat/Novus/ClientDB/ClientDB.h>

#include <Gameplay/GameDefine.h>
#include <Gameplay/Network/Define.h>

#include <Meta/Generated/Shared/NetworkEnum.h>
#include <Meta/Generated/Shared/NetworkPacket.h>

#include <entt/entt.hpp>

namespace ECS::Util::MessageBuilder
{
    u32 AddHeader(std::shared_ptr<Bytebuffer>& buffer, ::Network::OpcodeType opcode, u16 size)
    {
        Network::MessageHeader header =
        {
            .opcode = static_cast<Network::OpcodeType>(opcode),
            .size = size
        };

        if (buffer->GetSpace() < sizeof(Network::MessageHeader))
            return std::numeric_limits<u32>().max();

        u32 headerPos = static_cast<u32>(buffer->writtenData);
        buffer->Put(header);

        return headerPos;
    }

    bool ValidatePacket(const std::shared_ptr<Bytebuffer>& buffer, u32 headerPos)
    {
        if (buffer->writtenData < headerPos + sizeof(Network::MessageHeader))
            return false;

        Network::MessageHeader* header = reinterpret_cast<Network::MessageHeader*>(buffer->GetDataPointer() + headerPos);

        u32 headerSize = static_cast<u32>(buffer->writtenData - headerPos) - sizeof(Network::MessageHeader);
        if (headerSize > std::numeric_limits<u16>().max())
            return false;

        header->size = headerSize;
        return true;
    }

    bool CreatePacket(std::shared_ptr<Bytebuffer>& buffer, ::Network::OpcodeType opcode, std::function<void()> func)
    {
        if (!buffer)
            return false;

        u32 headerPos = AddHeader(buffer, opcode);

        if (func)
            func();

        if (!ValidatePacket(buffer, headerPos))
            return false;

        return true;
    }

    namespace Authentication
    {
        bool BuildConnectMessage(std::shared_ptr<Bytebuffer>& buffer, const std::string& charName)
        {
            bool createPacketResult = CreatePacket(buffer, Generated::ConnectPacket::PACKET_ID, [&]()
            {
                buffer->PutString(charName);
            });

            return createPacketResult;
        }
    }

    namespace Heartbeat
    {
        bool BuildPingMessage(std::shared_ptr<Bytebuffer>& buffer, u16 ping)
        {
            bool result = CreatePacket(buffer, Generated::PingPacket::PACKET_ID, [&buffer, ping]()
            {
                buffer->PutU16(ping);
            });

            return result;
        }
    }

    namespace Unit
    {
        bool BuildUnitMoveMessage(std::shared_ptr<Bytebuffer>& buffer, const vec3& position, const vec2& pitchYaw, const Components::MovementFlags& movementFlags, f32 verticalVelocity)
        {
            bool result = CreatePacket(buffer, Generated::ClientUnitMovePacket::PACKET_ID, [&]()
            {
                buffer->Put(movementFlags);
                buffer->Put(position);
                buffer->Put(pitchYaw);
                buffer->Put(verticalVelocity);
            });

            return result;
        }
        
        bool BuildUnitTargetUpdateMessage(std::shared_ptr<Bytebuffer>& buffer, ObjectGUID targetGUID)
        {
            bool result = CreatePacket(buffer, Generated::ClientUnitTargetUpdatePacket::PACKET_ID, [&, targetGUID]()
            {
                buffer->Serialize(targetGUID);
            });

            return result;
        }
    }

    namespace Container
    {
        bool BuildRequestSwapSlots(std::shared_ptr<Bytebuffer>& buffer, u16 srcContainerIndex, u16 destContainerIndex, u16 srcSlotIndex, u16 destSlotIndex)
        {
            bool result = CreatePacket(buffer, Generated::ClientContainerSwapSlotsPacket::PACKET_ID, [&buffer, srcContainerIndex, destContainerIndex, srcSlotIndex, destSlotIndex]()
            {
                buffer->PutU16(srcContainerIndex);
                buffer->PutU16(destContainerIndex);
                buffer->PutU16(srcSlotIndex);
                buffer->PutU16(destSlotIndex);
            });

            return result;
        }
    }

    namespace Spell
    {
        bool BuildSpellCast(std::shared_ptr<Bytebuffer>& buffer, u32 spellID)
        {
            bool result = CreatePacket(buffer, Generated::ClientSpellCastPacket::PACKET_ID, [&, spellID]()
            {
                buffer->PutU32(spellID);
            });

            return result;
        }
    }

    namespace Chat
    {
        bool BuildChatMessage(std::shared_ptr<Bytebuffer>& buffer, const std::string& message)
        {
            bool result = CreatePacket(buffer, Generated::ClientSendChatMessagePacket::PACKET_ID, [&]()
            {
                buffer->PutString(message);
            });

            return result;
        }
    }

    namespace CombatLog
    {
    }

    namespace Cheat
    {
        bool BuildCheatDamage(std::shared_ptr<Bytebuffer>& buffer, u32 damage)
        {
            bool result = CreatePacket(buffer, Generated::SendCheatCommandPacket::PACKET_ID, [&]()
            {
                buffer->Put(Generated::CheatCommandEnum::Damage);
                buffer->PutU32(damage);
            });

            return result;
        }
        bool BuildCheatKill(std::shared_ptr<Bytebuffer>& buffer)
        {
            bool result = CreatePacket(buffer, Generated::SendCheatCommandPacket::PACKET_ID, [&]()
            {
                buffer->Put(Generated::CheatCommandEnum::Kill);
            });

            return result;
        }
        bool BuildCheatHeal(std::shared_ptr<Bytebuffer>& buffer, u32 heal)
        {
            bool result = CreatePacket(buffer, Generated::SendCheatCommandPacket::PACKET_ID, [&]()
            {
                buffer->Put(Generated::CheatCommandEnum::Heal);
                buffer->PutU32(heal);
            });

            return result;
        }
        bool BuildCheatResurrect(std::shared_ptr<Bytebuffer>& buffer)
        {
            bool result = CreatePacket(buffer, Generated::SendCheatCommandPacket::PACKET_ID, [&]()
            {
                buffer->Put(Generated::CheatCommandEnum::Resurrect);
            });

            return result;
        }
        bool BuildCheatUnitMorph(std::shared_ptr<Bytebuffer>& buffer, u32 displayID)
        {
            bool result = CreatePacket(buffer, Generated::SendCheatCommandPacket::PACKET_ID, [&]()
            {
                buffer->Put(Generated::CheatCommandEnum::UnitMorph);
                buffer->PutU32(displayID);
            });

            return result;
        }
        bool BuildCheatUnitDemorph(std::shared_ptr<Bytebuffer>& buffer)
        {
            bool result = CreatePacket(buffer, Generated::SendCheatCommandPacket::PACKET_ID, [&]()
            {
                buffer->Put(Generated::CheatCommandEnum::UnitDemorph);
            });

            return result;
        }
        bool BuildCheatTeleport(std::shared_ptr<Bytebuffer>& buffer, u32 mapID, const vec3& position)
        {
            bool result = CreatePacket(buffer, Generated::SendCheatCommandPacket::PACKET_ID, [&]()
            {
                buffer->Put(Generated::CheatCommandEnum::Teleport);
                buffer->PutU32(mapID);
                buffer->Put(position);
            });

            return result;
        }
        bool BuildCheatCharacterAdd(std::shared_ptr<Bytebuffer>& buffer, const std::string& name)
        {
            bool result = CreatePacket(buffer, Generated::SendCheatCommandPacket::PACKET_ID, [&]()
            {
                buffer->Put(Generated::CheatCommandEnum::CharacterAdd);
                buffer->PutString(name);
            });

            return result;
        }
        bool BuildCheatCharacterRemove(std::shared_ptr<Bytebuffer>& buffer, const std::string& name)
        {
            bool result = CreatePacket(buffer, Generated::SendCheatCommandPacket::PACKET_ID, [&]()
            {
                buffer->Put(Generated::CheatCommandEnum::CharacterRemove);
                buffer->PutString(name);
            });

            return result;
        }
        bool BuildCheatUnitSetRace(std::shared_ptr<Bytebuffer>& buffer, GameDefine::UnitRace race)
        {
            bool result = CreatePacket(buffer, Generated::SendCheatCommandPacket::PACKET_ID, [&]()
            {
                buffer->Put(Generated::CheatCommandEnum::UnitSetRace);
                buffer->Put(race);
            });

            return result;
        }
        bool BuildCheatUnitSetGender(std::shared_ptr<Bytebuffer>& buffer, GameDefine::UnitGender gender)
        {
            bool result = CreatePacket(buffer, Generated::SendCheatCommandPacket::PACKET_ID, [&]()
            {
                buffer->Put(Generated::CheatCommandEnum::UnitSetGender);
                buffer->Put(gender);
            });

            return result;
        }
        bool BuildCheatUnitSetClass(std::shared_ptr<Bytebuffer>& buffer, GameDefine::UnitClass unitClass)
        {
            bool result = CreatePacket(buffer, Generated::SendCheatCommandPacket::PACKET_ID, [&]()
            {
                buffer->Put(Generated::CheatCommandEnum::UnitSetClass);
                buffer->Put(unitClass);
            });

            return result;
        }
        bool BuildCheatUnitSetLevel(std::shared_ptr<Bytebuffer>& buffer, u16 level)
        {
            bool result = CreatePacket(buffer, Generated::SendCheatCommandPacket::PACKET_ID, [&]()
            {
                buffer->Put(Generated::CheatCommandEnum::UnitSetLevel);
                buffer->PutU16(level);
            });

            return result;
        }
        bool BuildCheatItemSetTemplate(std::shared_ptr<Bytebuffer>& buffer, ClientDB::Data* itemStorage, u32 itemID, const Generated::ItemRecord& item)
        {
            bool result = CreatePacket(buffer, Generated::SendCheatCommandPacket::PACKET_ID, [&, itemID]()
            {
                GameDefine::Database::ItemTemplate itemTemplate =
                {
                    .id = itemID,
                    .displayID = item.displayID,
                    .bind = item.bind,
                    .rarity = item.rarity,
                    .category = item.category,
                    .type = item.categoryType,
                    .virtualLevel = item.virtualLevel,
                    .requiredLevel = item.requiredLevel,
                    .durability = item.durability,
                    .iconID = item.iconID,

                    .name = itemStorage->GetString(item.name),
                    .description = itemStorage->GetString(item.description),

                    .armor = item.armor,
                    .statTemplateID = item.statTemplateID,
                    .armorTemplateID = item.armorTemplateID,
                    .weaponTemplateID = item.weaponTemplateID,
                    .shieldTemplateID = item.shieldTemplateID
                };

                buffer->Put(Generated::CheatCommandEnum::ItemSetTemplate);
                GameDefine::Database::ItemTemplate::Write(buffer, itemTemplate);
            });

            return result;
        }
        bool BuildCheatItemSetStatTemplate(std::shared_ptr<Bytebuffer>& buffer, ClientDB::Data* statTemplateStorage, u32 statTemplateID, const Generated::ItemStatTemplateRecord& statTemplate)
        {
            bool result = CreatePacket(buffer, Generated::SendCheatCommandPacket::PACKET_ID, [&, statTemplateID]()
            {
                buffer->Put(Generated::CheatCommandEnum::ItemSetStatTemplate);

                buffer->PutU32(statTemplateID);
                buffer->Serialize(statTemplate);
            });

            return result;
        }
        bool BuildCheatItemSetArmorTemplate(std::shared_ptr<Bytebuffer>& buffer, ClientDB::Data* armorTemplateStorage, u32 armorTemplateID, const Generated::ItemArmorTemplateRecord& armorTemplate)
        {
            bool result = CreatePacket(buffer, Generated::SendCheatCommandPacket::PACKET_ID, [&, armorTemplateID]()
            {
                GameDefine::Database::ItemArmorTemplate itemArmorTemplate =
                {
                    .id = armorTemplateID,
                    .equipType = (u8)armorTemplate.equipType,
                    .bonusArmor = armorTemplate.bonusArmor,
                };

                buffer->Put(Generated::CheatCommandEnum::ItemSetArmorTemplate);
                GameDefine::Database::ItemArmorTemplate::Write(buffer, itemArmorTemplate);
            });

            return result;
        }
        bool BuildCheatItemSetWeaponTemplate(std::shared_ptr<Bytebuffer>& buffer, ClientDB::Data* weaponTemplateStorage, u32 weaponTemplateID, const Generated::ItemWeaponTemplateRecord& weaponTemplate)
        {
            bool result = CreatePacket(buffer, Generated::SendCheatCommandPacket::PACKET_ID, [&, weaponTemplateID]()
            {
                GameDefine::Database::ItemWeaponTemplate itemWeaponTemplate =
                {
                    .id = weaponTemplateID,
                    .weaponStyle = (u8)weaponTemplate.weaponStyle,
                    .minDamage = weaponTemplate.damageRange.x,
                    .maxDamage = weaponTemplate.damageRange.y,
                    .speed = weaponTemplate.speed,
                };

                buffer->Put(Generated::CheatCommandEnum::ItemSetWeaponTemplate);
                GameDefine::Database::ItemWeaponTemplate::Write(buffer, itemWeaponTemplate);
            });

            return result;

        }
        bool BuildCheatItemSetShieldTemplate(std::shared_ptr<Bytebuffer>& buffer, ClientDB::Data* shieldTemplateStorage, u32 shieldTemplateID, const Generated::ItemShieldTemplateRecord& shieldTemplate)
        {
            bool result = CreatePacket(buffer, Generated::SendCheatCommandPacket::PACKET_ID, [&, shieldTemplateID]()
            {
                GameDefine::Database::ItemShieldTemplate itemShieldTemplate =
                {
                    .id = shieldTemplateID,
                    .bonusArmor = shieldTemplate.bonusArmor,
                    .block = shieldTemplate.block,
                };

                buffer->Put(Generated::CheatCommandEnum::ItemSetShieldTemplate);
                GameDefine::Database::ItemShieldTemplate::Write(buffer, itemShieldTemplate);
            });

            return result;
        }
        bool BuildCheatItemAdd(std::shared_ptr<Bytebuffer>& buffer, u32 itemID, u32 itemCount)
        {
            bool result = CreatePacket(buffer, Generated::SendCheatCommandPacket::PACKET_ID, [&, itemID, itemCount]()
            {
                buffer->Put(Generated::CheatCommandEnum::ItemAdd);
                buffer->PutU32(itemID);
                buffer->PutU32(itemCount);
            });

            return result;
        }
        bool BuildCheatItemRemove(std::shared_ptr<Bytebuffer>& buffer, u32 itemID, u32 itemCount)
        {
            bool result = CreatePacket(buffer, Generated::SendCheatCommandPacket::PACKET_ID, [&, itemID, itemCount]()
            {
                buffer->Put(Generated::CheatCommandEnum::ItemRemove);
                buffer->PutU32(itemID);
                buffer->PutU32(itemCount);
            });

            return result;
        }

        bool BuildCheatCreatureAdd(std::shared_ptr<Bytebuffer>& buffer, u32 creatureTemplateID)
        {
            bool result = CreatePacket(buffer, Generated::SendCheatCommandPacket::PACKET_ID, [&, creatureTemplateID]()
            {
                buffer->Put(Generated::CheatCommandEnum::CreatureAdd);
                buffer->PutU32(creatureTemplateID);
            });

            return result;
        }
        bool BuildCheatCreatureRemove(std::shared_ptr<Bytebuffer>& buffer, ObjectGUID guid)
        {
            bool result = CreatePacket(buffer, Generated::SendCheatCommandPacket::PACKET_ID, [&, guid]()
            {
                buffer->Put(Generated::CheatCommandEnum::CreatureRemove);
                buffer->Serialize(guid);
            });

            return result;
        }

        bool BuildCheatCreatureInfo(std::shared_ptr<Bytebuffer>& buffer, ObjectGUID guid)
        {
            bool result = CreatePacket(buffer, Generated::SendCheatCommandPacket::PACKET_ID, [&, guid]()
            {
                buffer->Put(Generated::CheatCommandEnum::CreatureInfo);
                buffer->Serialize(guid);
            });

            return result;
        }

        bool BuildCheatMapAdd(std::shared_ptr<Bytebuffer>& buffer, ClientDB::Data* mapStorage, u32 mapID, const Generated::MapRecord& map)
        {
            bool result = CreatePacket(buffer, Generated::SendCheatCommandPacket::PACKET_ID, [&, mapID]()
            {
                GameDefine::Database::Map mapTemplate =
                {
                    .id = mapID,
                    .flags = 0,
                    .name = mapStorage->GetString(map.name),
                    .type = map.instanceType,
                    .maxPlayers = map.maxPlayers,
                };

                buffer->Put(Generated::CheatCommandEnum::MapAdd);
                GameDefine::Database::Map::Write(buffer, mapTemplate);
            });

            return result;
        }
        bool BuildCheatGotoAdd(std::shared_ptr<Bytebuffer>& buffer, const Generated::GotoAddCommand& command)
        {
            bool result = CreatePacket(buffer, Generated::SendCheatCommandPacket::PACKET_ID, [&]()
            {
                buffer->Put(Generated::CheatCommandEnum::GotoAdd);
                buffer->PutString(command.location);
                buffer->PutU32(command.mapID);
                buffer->PutF32(command.x);
                buffer->PutF32(command.y);
                buffer->PutF32(command.z);
                buffer->PutF32(command.orientation);
            });

            return result;
        }
        bool BuildCheatGotoAddHere(std::shared_ptr<Bytebuffer>& buffer, const Generated::GotoAddHereCommand& command)
        {
            bool result = CreatePacket(buffer, Generated::SendCheatCommandPacket::PACKET_ID, [&]()
            {
                buffer->Put(Generated::CheatCommandEnum::GotoAddHere);
                buffer->PutString(command.location);
            });

            return result;
        }
        bool BuildCheatGotoRemove(std::shared_ptr<Bytebuffer>& buffer, const Generated::GotoRemoveCommand& command)
        {
            bool result = CreatePacket(buffer, Generated::SendCheatCommandPacket::PACKET_ID, [&]()
            {
                buffer->Put(Generated::CheatCommandEnum::GotoRemove);
                buffer->PutString(command.location);
            });

            return result;
        }
        bool BuildCheatGotoMap(std::shared_ptr<Bytebuffer>& buffer, const Generated::GotoMapCommand& command)
        {
            bool result = CreatePacket(buffer, Generated::SendCheatCommandPacket::PACKET_ID, [&]()
            {
                buffer->Put(Generated::CheatCommandEnum::GotoMap);
                buffer->PutU32(command.mapID);
            });

            return result;
        }
        bool BuildCheatGotoLocation(std::shared_ptr<Bytebuffer>& buffer, const Generated::GotoLocationCommand& command)
        {
            bool result = CreatePacket(buffer, Generated::SendCheatCommandPacket::PACKET_ID, [&]()
            {
                buffer->Put(Generated::CheatCommandEnum::GotoLocation);
                buffer->PutString(command.location);
            });

            return result;
        }
        bool BuildCheatGotoXYZ(std::shared_ptr<Bytebuffer>& buffer, const Generated::GotoXYZCommand& command)
        {
            bool result = CreatePacket(buffer, Generated::SendCheatCommandPacket::PACKET_ID, [&]()
            {
                buffer->Put(Generated::CheatCommandEnum::GotoXYZ);
                buffer->PutF32(command.x);
                buffer->PutF32(command.y);
                buffer->PutF32(command.z);
            });

            return result;
        }

        bool BuildCheatTriggerAdd(std::shared_ptr<Bytebuffer>& buffer, const std::string& name, u16 flags, u16 mapID, const vec3& position, const vec3& extents)
        {
            bool result = CreatePacket(buffer, Generated::SendCheatCommandPacket::PACKET_ID, [&, mapID]()
            {
                buffer->Put(Generated::CheatCommandEnum::TriggerAdd);
                buffer->PutString(name);
                buffer->PutU16(flags);
                buffer->PutU16(mapID);
                buffer->Put(position);
                buffer->Put(extents);
            });

            return result;
        }

        bool BuildCheatTriggerRemove(std::shared_ptr<Bytebuffer>& buffer, u32 triggerID)
        {
            bool result = CreatePacket(buffer, Generated::SendCheatCommandPacket::PACKET_ID, [&, triggerID]()
            {
                buffer->Put(Generated::CheatCommandEnum::TriggerRemove);
                buffer->PutU32(triggerID);
            });

            return result;
        }
    }
}
#include "MessageBuilderUtil.h"
#include "Game-Lib/ECS/Components/UnitStatsComponent.h"
#include "Game-Lib/ECS/Singletons/Database/ClientDBSingleton.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <FileFormat/Novus/ClientDB/ClientDB.h>

#include <Gameplay/GameDefine.h>
#include <Gameplay/Network/Define.h>

#include <entt/entt.hpp>

namespace ECS::Util::MessageBuilder
{
    u32 AddHeader(std::shared_ptr<Bytebuffer>& buffer, Network::GameOpcode opcode, Network::MessageHeader::Flags flags, u16 size)
    {
        Network::MessageHeader header =
        {
            .opcode = static_cast<Network::OpcodeType>(opcode),
            .size = size,
            .flags = flags
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

    bool CreatePacket(std::shared_ptr<Bytebuffer>& buffer, Network::GameOpcode opcode, std::function<void()> func)
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

    bool CreatePing(std::shared_ptr<Bytebuffer>& buffer, std::function<void()> func)
    {
        if (!buffer)
            return false;

        u32 headerPos = AddHeader(buffer, Network::GameOpcode::Invalid, { .isPing = 1 });

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
            bool createPacketResult = CreatePacket(buffer, Network::GameOpcode::Client_Connect, [&]()
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
            bool result = CreatePing(buffer, [&buffer, ping]()
            {
                buffer->PutU16(ping);
            });

            return result;
        }
    }

    namespace Entity
    {
        bool BuildMoveMessage(std::shared_ptr<Bytebuffer>& buffer, const vec3& position, const quat& rotation, const Components::MovementFlags& movementFlags, f32 verticalVelocity)
        {
            bool result = CreatePacket(buffer, Network::GameOpcode::Shared_EntityMove, [&]()
            {
                buffer->Put(position);
                buffer->Put(rotation);
                buffer->Put(movementFlags);
                buffer->Put(verticalVelocity);
            });

            return result;
        }
        
        bool BuildTargetUpdateMessage(std::shared_ptr<Bytebuffer>& buffer, GameDefine::ObjectGuid targetGuid)
        {
            bool result = CreatePacket(buffer, Network::GameOpcode::Shared_EntityTargetUpdate, [&]()
            {
                buffer->Serialize(targetGuid);
            });

            return result;
        }
    }

    namespace Container
    {
        bool BuildRequestSwapSlots(std::shared_ptr<Bytebuffer>& buffer, u16 srcContainerIndex, u16 destContainerIndex, u16 srcSlotIndex, u16 destSlotIndex)
        {
            bool result = CreatePacket(buffer, Network::GameOpcode::Client_ContainerSwapSlots, [&buffer, srcContainerIndex, destContainerIndex, srcSlotIndex, destSlotIndex]()
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
        bool BuildLocalRequestSpellCast(std::shared_ptr<Bytebuffer>& buffer)
        {
            bool result = CreatePacket(buffer, Network::GameOpcode::Client_LocalRequestSpellCast, [&]()
            {
                buffer->PutU32(0);
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
            bool result = CreatePacket(buffer, Network::GameOpcode::Client_SendCheatCommand, [&]()
            {
                buffer->Put(Network::CheatCommands::Damage);
                buffer->PutU32(damage);
            });

            return result;
        }
        bool BuildCheatKill(std::shared_ptr<Bytebuffer>& buffer)
        {
            bool result = CreatePacket(buffer, Network::GameOpcode::Client_SendCheatCommand, [&]()
            {
                buffer->Put(Network::CheatCommands::Kill);
            });

            return result;
        }
        bool BuildCheatHeal(std::shared_ptr<Bytebuffer>& buffer, u32 heal)
        {
            bool result = CreatePacket(buffer, Network::GameOpcode::Client_SendCheatCommand, [&]()
            {
                buffer->Put(Network::CheatCommands::Heal);
                buffer->PutU32(heal);
            });

            return result;
        }
        bool BuildCheatResurrect(std::shared_ptr<Bytebuffer>& buffer)
        {
            bool result = CreatePacket(buffer, Network::GameOpcode::Client_SendCheatCommand, [&]()
            {
                buffer->Put(Network::CheatCommands::Resurrect);
            });

            return result;
        }
        bool BuildCheatMorph(std::shared_ptr<Bytebuffer>& buffer, u32 displayID)
        {
            bool result = CreatePacket(buffer, Network::GameOpcode::Client_SendCheatCommand, [&]()
            {
                buffer->Put(Network::CheatCommands::Morph);
                buffer->PutU32(displayID);
            });

            return result;
        }
        bool BuildCheatDemorph(std::shared_ptr<Bytebuffer>& buffer)
        {
            bool result = CreatePacket(buffer, Network::GameOpcode::Client_SendCheatCommand, [&]()
            {
                buffer->Put(Network::CheatCommands::Demorph);
            });

            return result;
        }
        bool BuildCheatTeleport(std::shared_ptr<Bytebuffer>& buffer, u32 mapID, const vec3& position)
        {
            bool result = CreatePacket(buffer, Network::GameOpcode::Client_SendCheatCommand, [&]()
            {
                buffer->Put(Network::CheatCommands::Teleport);
                buffer->PutU32(mapID);
                buffer->Put(position);
            });

            return result;
        }
        bool BuildCheatCreateChar(std::shared_ptr<Bytebuffer>& buffer, const std::string& name)
        {
            bool result = CreatePacket(buffer, Network::GameOpcode::Client_SendCheatCommand, [&]()
            {
                buffer->Put(Network::CheatCommands::CreateCharacter);
                buffer->PutString(name);
            });

            return result;
        }
        bool BuildCheatDeleteChar(std::shared_ptr<Bytebuffer>& buffer, const std::string& name)
        {
            bool result = CreatePacket(buffer, Network::GameOpcode::Client_SendCheatCommand, [&]()
            {
                buffer->Put(Network::CheatCommands::DeleteCharacter);
                buffer->PutString(name);
            });

            return result;
        }
        bool BuildCheatSetRace(std::shared_ptr<Bytebuffer>& buffer, GameDefine::UnitRace race)
        {
            bool result = CreatePacket(buffer, Network::GameOpcode::Client_SendCheatCommand, [&]()
            {
                buffer->Put(Network::CheatCommands::SetRace);
                buffer->Put(race);
            });

            return result;
        }
        bool BuildCheatSetGender(std::shared_ptr<Bytebuffer>& buffer, GameDefine::UnitGender gender)
        {
            bool result = CreatePacket(buffer, Network::GameOpcode::Client_SendCheatCommand, [&]()
            {
                buffer->Put(Network::CheatCommands::SetGender);
                buffer->Put(gender);
            });

            return result;
        }
        bool BuildCheatSetClass(std::shared_ptr<Bytebuffer>& buffer, GameDefine::UnitClass unitClass)
        {
            bool result = CreatePacket(buffer, Network::GameOpcode::Client_SendCheatCommand, [&]()
            {
                buffer->Put(Network::CheatCommands::SetClass);
                buffer->Put(unitClass);
            });

            return result;
        }
        bool BuildCheatSetLevel(std::shared_ptr<Bytebuffer>& buffer, u16 level)
        {
            bool result = CreatePacket(buffer, Network::GameOpcode::Client_SendCheatCommand, [&]()
            {
                buffer->Put(Network::CheatCommands::SetLevel);
                buffer->PutU16(level);
            });

            return result;
        }
        bool BuildCheatSetItemTemplate(std::shared_ptr<Bytebuffer>& buffer, ClientDB::Data* itemStorage, u32 itemID, const Generated::ItemRecord& item)
        {
            bool result = CreatePacket(buffer, Network::GameOpcode::Client_SendCheatCommand, [&, itemID]()
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

                buffer->Put(Network::CheatCommands::SetItemTemplate);
                GameDefine::Database::ItemTemplate::Write(buffer, itemTemplate);
            });

            return result;
        }
        bool BuildCheatSetItemStatTemplate(std::shared_ptr<Bytebuffer>& buffer, ClientDB::Data* statTemplateStorage, u32 statTemplateID, const Generated::ItemStatTemplateRecord& statTemplate)
        {
            bool result = CreatePacket(buffer, Network::GameOpcode::Client_SendCheatCommand, [&, statTemplateID]()
            {
                buffer->Put(Network::CheatCommands::SetItemStatTemplate);

                buffer->PutU32(statTemplateID);
                buffer->Serialize(statTemplate);
            });

            return result;
        }
        bool BuildCheatSetItemArmorTemplate(std::shared_ptr<Bytebuffer>& buffer, ClientDB::Data* armorTemplateStorage, u32 armorTemplateID, const Generated::ItemArmorTemplateRecord& armorTemplate)
        {
            bool result = CreatePacket(buffer, Network::GameOpcode::Client_SendCheatCommand, [&, armorTemplateID]()
            {
                GameDefine::Database::ItemArmorTemplate itemArmorTemplate =
                {
                    .id = armorTemplateID,
                    .equipType = (u8)armorTemplate.equipType,
                    .bonusArmor = armorTemplate.bonusArmor,
                };

                buffer->Put(Network::CheatCommands::SetItemArmorTemplate);
                GameDefine::Database::ItemArmorTemplate::Write(buffer, itemArmorTemplate);
            });

            return result;
        }
        bool BuildCheatSetItemWeaponTemplate(std::shared_ptr<Bytebuffer>& buffer, ClientDB::Data* weaponTemplateStorage, u32 weaponTemplateID, const Generated::ItemWeaponTemplateRecord& weaponTemplate)
        {
            bool result = CreatePacket(buffer, Network::GameOpcode::Client_SendCheatCommand, [&, weaponTemplateID]()
            {
                GameDefine::Database::ItemWeaponTemplate itemWeaponTemplate =
                {
                    .id = weaponTemplateID,
                    .weaponStyle = (u8)weaponTemplate.weaponStyle,
                    .minDamage = weaponTemplate.damageRange.x,
                    .maxDamage = weaponTemplate.damageRange.y,
                    .speed = weaponTemplate.speed,
                };

                buffer->Put(Network::CheatCommands::SetItemWeaponTemplate);
                GameDefine::Database::ItemWeaponTemplate::Write(buffer, itemWeaponTemplate);
            });

            return result;

        }
        bool BuildCheatSetItemShieldTemplate(std::shared_ptr<Bytebuffer>& buffer, ClientDB::Data* shieldTemplateStorage, u32 shieldTemplateID, const Generated::ItemShieldTemplateRecord& shieldTemplate)
        {
            bool result = CreatePacket(buffer, Network::GameOpcode::Client_SendCheatCommand, [&, shieldTemplateID]()
            {
                GameDefine::Database::ItemShieldTemplate itemShieldTemplate =
                {
                    .id = shieldTemplateID,
                    .bonusArmor = shieldTemplate.bonusArmor,
                    .block = shieldTemplate.block,
                };

                buffer->Put(Network::CheatCommands::SetItemShieldTemplate);
                GameDefine::Database::ItemShieldTemplate::Write(buffer, itemShieldTemplate);
            });

            return result;
        }
        bool BuildCheatAddItem(std::shared_ptr<Bytebuffer>& buffer, u32 itemID, u32 itemCount)
        {
            bool result = CreatePacket(buffer, Network::GameOpcode::Client_SendCheatCommand, [&, itemID, itemCount]()
            {
                buffer->Put(Network::CheatCommands::AddItem);
                buffer->PutU32(itemID);
                buffer->PutU32(itemCount);
            });

            return result;
        }
        bool BuildCheatRemoveItem(std::shared_ptr<Bytebuffer>& buffer, u32 itemID, u32 itemCount)
        {
            bool result = CreatePacket(buffer, Network::GameOpcode::Client_SendCheatCommand, [&, itemID, itemCount]()
            {
                buffer->Put(Network::CheatCommands::RemoveItem);
                buffer->PutU32(itemID);
                buffer->PutU32(itemCount);
            });

            return result;
        }
        bool BuildCheatTriggerAdd(std::shared_ptr<Bytebuffer>& buffer, const std::string& name, u16 flags, u16 mapID, const vec3& position, const vec3& extents)
        {
            bool result = CreatePacket(buffer, Network::GameOpcode::Client_SendCheatCommand, [&, mapID]()
            {
                buffer->Put(Network::CheatCommands::TriggerAdd);
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
            bool result = CreatePacket(buffer, Network::GameOpcode::Client_SendCheatCommand, [&, triggerID]()
            {
                buffer->Put(Network::CheatCommands::TriggerRemove);
                buffer->PutU32(triggerID);
            });

            return result;
        }
    }
    namespace ProximityTrigger
    {
        bool BuildProximityTriggerEnter(std::shared_ptr<Bytebuffer>& buffer, u32 triggerID)
        {
            bool result = CreatePacket(buffer, Network::GameOpcode::Client_TriggerEnter, [&, triggerID]()
            {
                buffer->PutU32(triggerID);
            });

            return result;
        }
    }
}

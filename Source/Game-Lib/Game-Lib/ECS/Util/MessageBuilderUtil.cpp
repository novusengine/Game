#include "MessageBuilderUtil.h"
#include "Game-Lib/ECS/Components/UnitStatsComponent.h"
#include "Game-Lib/ECS/Singletons/Database/ClientDBSingleton.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <FileFormat/Novus/ClientDB/ClientDB.h>

#include <Gameplay/GameDefine.h>
#include <Gameplay/Network/Define.h>

#include <MetaGen/PacketList.h>
#include <MetaGen/Shared/Packet/Packet.h>

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
            bool createPacketResult = CreatePacket(buffer, MetaGen::Shared::Packet::ClientConnectPacket::PACKET_ID, [&]()
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
            bool result = CreatePacket(buffer, MetaGen::Shared::Packet::ClientPingPacket::PACKET_ID, [&buffer, ping]()
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
            bool result = CreatePacket(buffer, MetaGen::Shared::Packet::ClientUnitMovePacket::PACKET_ID, [&]()
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
            bool result = CreatePacket(buffer, MetaGen::Shared::Packet::ClientUnitTargetUpdatePacket::PACKET_ID, [&, targetGUID]()
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
            bool result = CreatePacket(buffer, MetaGen::Shared::Packet::SharedContainerSwapSlotsPacket::PACKET_ID, [&buffer, srcContainerIndex, destContainerIndex, srcSlotIndex, destSlotIndex]()
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
            bool result = CreatePacket(buffer, MetaGen::Shared::Packet::ClientSpellCastPacket::PACKET_ID, [&, spellID]()
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
            bool result = CreatePacket(buffer, MetaGen::Shared::Packet::ClientSendChatMessagePacket::PACKET_ID, [&]()
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
            bool result = CreatePacket(buffer, MetaGen::Shared::Packet::ClientSendCheatCommandPacket::PACKET_ID, [&]()
            {
                buffer->Put(MetaGen::Shared::Cheat::CheatCommandEnum::Damage);
                buffer->PutU32(damage);
            });

            return result;
        }
        bool BuildCheatKill(std::shared_ptr<Bytebuffer>& buffer)
        {
            bool result = CreatePacket(buffer, MetaGen::Shared::Packet::ClientSendCheatCommandPacket::PACKET_ID, [&]()
            {
                buffer->Put(MetaGen::Shared::Cheat::CheatCommandEnum::Kill);
            });

            return result;
        }
        bool BuildCheatHeal(std::shared_ptr<Bytebuffer>& buffer, u32 heal)
        {
            bool result = CreatePacket(buffer, MetaGen::Shared::Packet::ClientSendCheatCommandPacket::PACKET_ID, [&]()
            {
                buffer->Put(MetaGen::Shared::Cheat::CheatCommandEnum::Heal);
                buffer->PutU32(heal);
            });

            return result;
        }
        bool BuildCheatResurrect(std::shared_ptr<Bytebuffer>& buffer)
        {
            bool result = CreatePacket(buffer, MetaGen::Shared::Packet::ClientSendCheatCommandPacket::PACKET_ID, [&]()
            {
                buffer->Put(MetaGen::Shared::Cheat::CheatCommandEnum::Resurrect);
            });

            return result;
        }
        bool BuildCheatUnitMorph(std::shared_ptr<Bytebuffer>& buffer, u32 displayID)
        {
            bool result = CreatePacket(buffer, MetaGen::Shared::Packet::ClientSendCheatCommandPacket::PACKET_ID, [&]()
            {
                buffer->Put(MetaGen::Shared::Cheat::CheatCommandEnum::UnitMorph);
                buffer->PutU32(displayID);
            });

            return result;
        }
        bool BuildCheatUnitDemorph(std::shared_ptr<Bytebuffer>& buffer)
        {
            bool result = CreatePacket(buffer, MetaGen::Shared::Packet::ClientSendCheatCommandPacket::PACKET_ID, [&]()
            {
                buffer->Put(MetaGen::Shared::Cheat::CheatCommandEnum::UnitDemorph);
            });

            return result;
        }
        bool BuildCheatTeleport(std::shared_ptr<Bytebuffer>& buffer, u32 mapID, const vec3& position)
        {
            bool result = CreatePacket(buffer, MetaGen::Shared::Packet::ClientSendCheatCommandPacket::PACKET_ID, [&]()
            {
                buffer->Put(MetaGen::Shared::Cheat::CheatCommandEnum::Teleport);
                buffer->PutU32(mapID);
                buffer->Put(position);
            });

            return result;
        }
        bool BuildCheatCharacterAdd(std::shared_ptr<Bytebuffer>& buffer, const std::string& name)
        {
            bool result = CreatePacket(buffer, MetaGen::Shared::Packet::ClientSendCheatCommandPacket::PACKET_ID, [&]()
            {
                buffer->Put(MetaGen::Shared::Cheat::CheatCommandEnum::CharacterAdd);
                buffer->PutString(name);
            });

            return result;
        }
        bool BuildCheatCharacterRemove(std::shared_ptr<Bytebuffer>& buffer, const std::string& name)
        {
            bool result = CreatePacket(buffer, MetaGen::Shared::Packet::ClientSendCheatCommandPacket::PACKET_ID, [&]()
            {
                buffer->Put(MetaGen::Shared::Cheat::CheatCommandEnum::CharacterRemove);
                buffer->PutString(name);
            });

            return result;
        }
        bool BuildCheatUnitSetRace(std::shared_ptr<Bytebuffer>& buffer, GameDefine::UnitRace race)
        {
            bool result = CreatePacket(buffer, MetaGen::Shared::Packet::ClientSendCheatCommandPacket::PACKET_ID, [&]()
            {
                buffer->Put(MetaGen::Shared::Cheat::CheatCommandEnum::UnitSetRace);
                buffer->Put(race);
            });

            return result;
        }
        bool BuildCheatUnitSetGender(std::shared_ptr<Bytebuffer>& buffer, GameDefine::UnitGender gender)
        {
            bool result = CreatePacket(buffer, MetaGen::Shared::Packet::ClientSendCheatCommandPacket::PACKET_ID, [&]()
            {
                buffer->Put(MetaGen::Shared::Cheat::CheatCommandEnum::UnitSetGender);
                buffer->Put(gender);
            });

            return result;
        }
        bool BuildCheatUnitSetClass(std::shared_ptr<Bytebuffer>& buffer, GameDefine::UnitClass unitClass)
        {
            bool result = CreatePacket(buffer, MetaGen::Shared::Packet::ClientSendCheatCommandPacket::PACKET_ID, [&]()
            {
                buffer->Put(MetaGen::Shared::Cheat::CheatCommandEnum::UnitSetClass);
                buffer->Put(unitClass);
            });

            return result;
        }
        bool BuildCheatUnitSetLevel(std::shared_ptr<Bytebuffer>& buffer, u16 level)
        {
            bool result = CreatePacket(buffer, MetaGen::Shared::Packet::ClientSendCheatCommandPacket::PACKET_ID, [&]()
            {
                buffer->Put(MetaGen::Shared::Cheat::CheatCommandEnum::UnitSetLevel);
                buffer->PutU16(level);
            });

            return result;
        }
        bool BuildCheatItemSetTemplate(std::shared_ptr<Bytebuffer>& buffer, ClientDB::Data* itemStorage, u32 itemID, const MetaGen::Shared::ClientDB::ItemRecord& item)
        {
            bool result = CreatePacket(buffer, MetaGen::Shared::Packet::ClientSendCheatCommandPacket::PACKET_ID, [&, itemID]()
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

                buffer->Put(MetaGen::Shared::Cheat::CheatCommandEnum::ItemSetTemplate);
                GameDefine::Database::ItemTemplate::Write(buffer, itemTemplate);
            });

            return result;
        }
        bool BuildCheatItemSetStatTemplate(std::shared_ptr<Bytebuffer>& buffer, ClientDB::Data* statTemplateStorage, u32 statTemplateID, const MetaGen::Shared::ClientDB::ItemStatTemplateRecord& statTemplate)
        {
            bool result = CreatePacket(buffer, MetaGen::Shared::Packet::ClientSendCheatCommandPacket::PACKET_ID, [&, statTemplateID]()
            {
                buffer->Put(MetaGen::Shared::Cheat::CheatCommandEnum::ItemSetStatTemplate);

                buffer->PutU32(statTemplateID);
                buffer->Serialize(statTemplate);
            });

            return result;
        }
        bool BuildCheatItemSetArmorTemplate(std::shared_ptr<Bytebuffer>& buffer, ClientDB::Data* armorTemplateStorage, u32 armorTemplateID, const MetaGen::Shared::ClientDB::ItemArmorTemplateRecord& armorTemplate)
        {
            bool result = CreatePacket(buffer, MetaGen::Shared::Packet::ClientSendCheatCommandPacket::PACKET_ID, [&, armorTemplateID]()
            {
                GameDefine::Database::ItemArmorTemplate itemArmorTemplate =
                {
                    .id = armorTemplateID,
                    .equipType = (u8)armorTemplate.equipType,
                    .bonusArmor = armorTemplate.bonusArmor,
                };

                buffer->Put(MetaGen::Shared::Cheat::CheatCommandEnum::ItemSetArmorTemplate);
                GameDefine::Database::ItemArmorTemplate::Write(buffer, itemArmorTemplate);
            });

            return result;
        }
        bool BuildCheatItemSetWeaponTemplate(std::shared_ptr<Bytebuffer>& buffer, ClientDB::Data* weaponTemplateStorage, u32 weaponTemplateID, const MetaGen::Shared::ClientDB::ItemWeaponTemplateRecord& weaponTemplate)
        {
            bool result = CreatePacket(buffer, MetaGen::Shared::Packet::ClientSendCheatCommandPacket::PACKET_ID, [&, weaponTemplateID]()
            {
                GameDefine::Database::ItemWeaponTemplate itemWeaponTemplate =
                {
                    .id = weaponTemplateID,
                    .weaponStyle = (u8)weaponTemplate.weaponStyle,
                    .minDamage = weaponTemplate.damageRange.x,
                    .maxDamage = weaponTemplate.damageRange.y,
                    .speed = weaponTemplate.speed,
                };

                buffer->Put(MetaGen::Shared::Cheat::CheatCommandEnum::ItemSetWeaponTemplate);
                GameDefine::Database::ItemWeaponTemplate::Write(buffer, itemWeaponTemplate);
            });

            return result;

        }
        bool BuildCheatItemSetShieldTemplate(std::shared_ptr<Bytebuffer>& buffer, ClientDB::Data* shieldTemplateStorage, u32 shieldTemplateID, const MetaGen::Shared::ClientDB::ItemShieldTemplateRecord& shieldTemplate)
        {
            bool result = CreatePacket(buffer, MetaGen::Shared::Packet::ClientSendCheatCommandPacket::PACKET_ID, [&, shieldTemplateID]()
            {
                GameDefine::Database::ItemShieldTemplate itemShieldTemplate =
                {
                    .id = shieldTemplateID,
                    .bonusArmor = shieldTemplate.bonusArmor,
                    .block = shieldTemplate.block,
                };

                buffer->Put(MetaGen::Shared::Cheat::CheatCommandEnum::ItemSetShieldTemplate);
                GameDefine::Database::ItemShieldTemplate::Write(buffer, itemShieldTemplate);
            });

            return result;
        }
        bool BuildCheatItemAdd(std::shared_ptr<Bytebuffer>& buffer, u32 itemID, u32 itemCount)
        {
            bool result = CreatePacket(buffer, MetaGen::Shared::Packet::ClientSendCheatCommandPacket::PACKET_ID, [&, itemID, itemCount]()
            {
                buffer->Put(MetaGen::Shared::Cheat::CheatCommandEnum::ItemAdd);
                buffer->PutU32(itemID);
                buffer->PutU32(itemCount);
            });

            return result;
        }
        bool BuildCheatItemRemove(std::shared_ptr<Bytebuffer>& buffer, u32 itemID, u32 itemCount)
        {
            bool result = CreatePacket(buffer, MetaGen::Shared::Packet::ClientSendCheatCommandPacket::PACKET_ID, [&, itemID, itemCount]()
            {
                buffer->Put(MetaGen::Shared::Cheat::CheatCommandEnum::ItemRemove);
                buffer->PutU32(itemID);
                buffer->PutU32(itemCount);
            });

            return result;
        }

        bool BuildCheatCreatureAdd(std::shared_ptr<Bytebuffer>& buffer, u32 creatureTemplateID)
        {
            bool result = CreatePacket(buffer, MetaGen::Shared::Packet::ClientSendCheatCommandPacket::PACKET_ID, [&, creatureTemplateID]()
            {
                buffer->Put(MetaGen::Shared::Cheat::CheatCommandEnum::CreatureAdd);
                buffer->PutU32(creatureTemplateID);
            });

            return result;
        }
        bool BuildCheatCreatureRemove(std::shared_ptr<Bytebuffer>& buffer, ObjectGUID guid)
        {
            bool result = CreatePacket(buffer, MetaGen::Shared::Packet::ClientSendCheatCommandPacket::PACKET_ID, [&, guid]()
            {
                buffer->Put(MetaGen::Shared::Cheat::CheatCommandEnum::CreatureRemove);
                buffer->Serialize(guid);
            });

            return result;
        }

        bool BuildCheatCreatureInfo(std::shared_ptr<Bytebuffer>& buffer, ObjectGUID guid)
        {
            bool result = CreatePacket(buffer, MetaGen::Shared::Packet::ClientSendCheatCommandPacket::PACKET_ID, [&, guid]()
            {
                buffer->Put(MetaGen::Shared::Cheat::CheatCommandEnum::CreatureInfo);
                buffer->Serialize(guid);
            });

            return result;
        }

        bool BuildCheatMapAdd(std::shared_ptr<Bytebuffer>& buffer, ClientDB::Data* mapStorage, u32 mapID, const MetaGen::Shared::ClientDB::MapRecord& map)
        {
            bool result = CreatePacket(buffer, MetaGen::Shared::Packet::ClientSendCheatCommandPacket::PACKET_ID, [&, mapID]()
            {
                GameDefine::Database::Map mapTemplate =
                {
                    .id = mapID,
                    .flags = 0,
                    .internalName = mapStorage->GetString(map.nameInternal),
                    .name = mapStorage->GetString(map.name),
                    .type = map.instanceType,
                    .maxPlayers = map.maxPlayers,
                };

                buffer->Put(MetaGen::Shared::Cheat::CheatCommandEnum::MapAdd);
                GameDefine::Database::Map::Write(buffer, mapTemplate);
            });

            return result;
        }
        bool BuildCheatGotoAdd(std::shared_ptr<Bytebuffer>& buffer, const MetaGen::Game::Command::GotoAddCommand& command)
        {
            bool result = CreatePacket(buffer, MetaGen::Shared::Packet::ClientSendCheatCommandPacket::PACKET_ID, [&]()
            {
                buffer->Put(MetaGen::Shared::Cheat::CheatCommandEnum::GotoAdd);
                buffer->PutString(command.location);
                buffer->PutU32(command.mapID);
                buffer->PutF32(command.x);
                buffer->PutF32(command.y);
                buffer->PutF32(command.z);
                buffer->PutF32(command.orientation);
            });

            return result;
        }
        bool BuildCheatGotoAddHere(std::shared_ptr<Bytebuffer>& buffer, const MetaGen::Game::Command::GotoAddHereCommand& command)
        {
            bool result = CreatePacket(buffer, MetaGen::Shared::Packet::ClientSendCheatCommandPacket::PACKET_ID, [&]()
            {
                buffer->Put(MetaGen::Shared::Cheat::CheatCommandEnum::GotoAddHere);
                buffer->PutString(command.location);
            });

            return result;
        }
        bool BuildCheatGotoRemove(std::shared_ptr<Bytebuffer>& buffer, const MetaGen::Game::Command::GotoRemoveCommand& command)
        {
            bool result = CreatePacket(buffer, MetaGen::Shared::Packet::ClientSendCheatCommandPacket::PACKET_ID, [&]()
            {
                buffer->Put(MetaGen::Shared::Cheat::CheatCommandEnum::GotoRemove);
                buffer->PutString(command.location);
            });

            return result;
        }
        bool BuildCheatGotoMap(std::shared_ptr<Bytebuffer>& buffer, const MetaGen::Game::Command::GotoMapCommand& command)
        {
            bool result = CreatePacket(buffer, MetaGen::Shared::Packet::ClientSendCheatCommandPacket::PACKET_ID, [&]()
            {
                buffer->Put(MetaGen::Shared::Cheat::CheatCommandEnum::GotoMap);
                buffer->PutU32(command.mapID);
            });

            return result;
        }
        bool BuildCheatGotoLocation(std::shared_ptr<Bytebuffer>& buffer, const MetaGen::Game::Command::GotoLocationCommand& command)
        {
            bool result = CreatePacket(buffer, MetaGen::Shared::Packet::ClientSendCheatCommandPacket::PACKET_ID, [&]()
            {
                buffer->Put(MetaGen::Shared::Cheat::CheatCommandEnum::GotoLocation);
                buffer->PutString(command.location);
            });

            return result;
        }
        bool BuildCheatGotoXYZ(std::shared_ptr<Bytebuffer>& buffer, const MetaGen::Game::Command::GotoXYZCommand& command)
        {
            bool result = CreatePacket(buffer, MetaGen::Shared::Packet::ClientSendCheatCommandPacket::PACKET_ID, [&]()
            {
                buffer->Put(MetaGen::Shared::Cheat::CheatCommandEnum::GotoXYZ);
                buffer->PutF32(command.x);
                buffer->PutF32(command.y);
                buffer->PutF32(command.z);
            });

            return result;
        }

        bool BuildCheatTriggerAdd(std::shared_ptr<Bytebuffer>& buffer, const std::string& name, u16 flags, u16 mapID, const vec3& position, const vec3& extents)
        {
            bool result = CreatePacket(buffer, MetaGen::Shared::Packet::ClientSendCheatCommandPacket::PACKET_ID, [&, mapID]()
            {
                buffer->Put(MetaGen::Shared::Cheat::CheatCommandEnum::TriggerAdd);
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
            bool result = CreatePacket(buffer, MetaGen::Shared::Packet::ClientSendCheatCommandPacket::PACKET_ID, [&, triggerID]()
            {
                buffer->Put(MetaGen::Shared::Cheat::CheatCommandEnum::TriggerRemove);
                buffer->PutU32(triggerID);
            });

            return result;
        }
        bool BuildCheatSpellSet(std::shared_ptr<Bytebuffer>& buffer, ClientDB::Data* spellStorage, u32 spellID, const MetaGen::Shared::ClientDB::SpellRecord& spell)
        {
            bool result = CreatePacket(buffer, MetaGen::Shared::Packet::ClientSendCheatCommandPacket::PACKET_ID, [&, spellID]()
            {
                GameDefine::Database::Spell spellDefinition =
                {
                    .id = spellID,

                    .name = spellStorage->GetString(spell.name),
                    .description = spellStorage->GetString(spell.description),
                    .auraDescription = spellStorage->GetString(spell.auraDescription),
                    .iconID = spell.iconID,

                    .castTime = spell.castTime,
                    .cooldown = spell.cooldown,
                    .duration = spell.duration
                };

                buffer->Put(MetaGen::Shared::Cheat::CheatCommandEnum::SpellSet);
                GameDefine::Database::Spell::Write(buffer, spellDefinition);
            });

            return result;
        }
        bool BuildCheatSpellEffectSet(std::shared_ptr<Bytebuffer>& buffer, ClientDB::Data* spellEffectsStorage, u32 spellEffectsID, const MetaGen::Shared::ClientDB::SpellEffectsRecord& spellEffect)
        {
            bool result = CreatePacket(buffer, MetaGen::Shared::Packet::ClientSendCheatCommandPacket::PACKET_ID, [&, spellEffectsID]()
            {
                GameDefine::Database::SpellEffect spellEffectsDefinition =
                {
                    .id = spellEffectsID,
                    .spellID = spellEffect.spellID,
                    .effectPriority = spellEffect.effectPriority,
                    .effectType = spellEffect.effectType,

                    .effectValue1 = spellEffect.effectValues[0],
                    .effectValue2 = spellEffect.effectValues[1],
                    .effectValue3 = spellEffect.effectValues[2],

                    .effectMiscValue1 = spellEffect.effectMiscValues[0],
                    .effectMiscValue2 = spellEffect.effectMiscValues[1],
                    .effectMiscValue3 = spellEffect.effectMiscValues[2]
                };

                buffer->Put(MetaGen::Shared::Cheat::CheatCommandEnum::SpellEffectSet);
                GameDefine::Database::SpellEffect::Write(buffer, spellEffectsDefinition);
            });

            return result;
        }
        bool BuildCheatSpellProcDataSet(std::shared_ptr<Bytebuffer>& buffer, ClientDB::Data* spellProcDataStorage, u32 spellProcDataID, const MetaGen::Shared::ClientDB::SpellProcDataRecord& spellProcData)
        {
            bool result = CreatePacket(buffer, MetaGen::Shared::Packet::ClientSendCheatCommandPacket::PACKET_ID, [&, spellProcDataID]()
            {
                buffer->Put(MetaGen::Shared::Cheat::CheatCommandEnum::SpellProcDataSet);
                buffer->PutU32(spellProcDataID);
                buffer->Serialize(spellProcData);
            });

            return result;
        }
        bool BuildCheatSpellProcLinkSet(std::shared_ptr<Bytebuffer>& buffer, ClientDB::Data* spellProcLinkStorage, u32 spellProcLinkID, const MetaGen::Shared::ClientDB::SpellProcLinkRecord& spellProcLink)
        {
            bool result = CreatePacket(buffer, MetaGen::Shared::Packet::ClientSendCheatCommandPacket::PACKET_ID, [&, spellProcLinkID]()
            {
                buffer->Put(MetaGen::Shared::Cheat::CheatCommandEnum::SpellProcLinkSet);
                buffer->PutU32(spellProcLinkID);
                buffer->Serialize(spellProcLink);
            });

            return result;
        }
        bool BuildCreatureAddScript(std::shared_ptr<Bytebuffer>& buffer, const std::string& scriptName)
        {
            bool result = CreatePacket(buffer, MetaGen::Shared::Packet::ClientSendCheatCommandPacket::PACKET_ID, [&]()
            {
                buffer->Put(MetaGen::Shared::Cheat::CheatCommandEnum::CreatureAddScript);
                buffer->PutString(scriptName);
            });

            return result;
        }
        bool BuildCreatureRemoveScript(std::shared_ptr<Bytebuffer>& buffer)
        {
            bool result = CreatePacket(buffer, MetaGen::Shared::Packet::ClientSendCheatCommandPacket::PACKET_ID, [&]()
            {
                buffer->Put(MetaGen::Shared::Cheat::CheatCommandEnum::CreatureRemoveScript);
            });

            return result;
        }
    }
}
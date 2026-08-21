#include "MessageBuilderUtil.h"
#include "Game-Lib/ECS/Components/UnitStatsComponent.h"
#include "Game-Lib/ECS/Singletons/Database/ClientDBSingleton.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <FileFormat/Novus/ClientDB/ClientDB.h>

#include <Gameplay/GameDefine.h>
#include <Gameplay/Network/Define.h>

#include <MetaGen/PacketList.h>
#include <MetaGen/Shared/Localization/Localization.h>
#include <MetaGen/Shared/Packet/Packet.h>

#include <entt/entt.hpp>

#include <algorithm>

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
                buffer->PutU8(static_cast<u8>(MetaGen::Shared::Localization::LocaleEnum::EnUS));
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
        bool BuildSpellCast(std::shared_ptr<Bytebuffer>& buffer, u32 spellID, ObjectGUID targetGUID, const vec3& targetPosition)
        {
            bool result = CreatePacket(buffer, MetaGen::Shared::Packet::ClientSpellCastPacket::PACKET_ID, [&, spellID, targetGUID, targetPosition]()
            {
                buffer->PutU32(spellID);
                buffer->Serialize(targetGUID);
                buffer->Put(targetPosition);
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
                GameDefine::Database::ItemTemplate::Write(buffer.get(), itemTemplate);
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
                GameDefine::Database::ItemArmorTemplate::Write(buffer.get(), itemArmorTemplate);
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
                GameDefine::Database::ItemWeaponTemplate::Write(buffer.get(), itemWeaponTemplate);
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
                GameDefine::Database::ItemShieldTemplate::Write(buffer.get(), itemShieldTemplate);
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

        bool BuildCheatFactionReaction(std::shared_ptr<Bytebuffer>& buffer, ObjectGUID observerGUID, ObjectGUID targetGUID)
        {
            return CreatePacket(buffer, MetaGen::Shared::Packet::ClientSendCheatCommandPacket::PACKET_ID, [&]()
            {
                buffer->Put(MetaGen::Shared::Cheat::CheatCommandEnum::FactionReaction);
                buffer->Serialize(observerGUID);
                buffer->Serialize(targetGUID);
            });
        }

        bool BuildCheatUnitSetFaction(std::shared_ptr<Bytebuffer>& buffer, u16 factionID)
        {
            return CreatePacket(buffer, MetaGen::Shared::Packet::ClientSendCheatCommandPacket::PACKET_ID, [&]()
            {
                buffer->Put(MetaGen::Shared::Cheat::CheatCommandEnum::UnitSetFaction);
                buffer->PutU16(factionID);
            });
        }

        bool BuildCheatFactionReputationInfo(std::shared_ptr<Bytebuffer>& buffer, ObjectGUID characterGUID, u16 factionID)
        {
            return CreatePacket(buffer, MetaGen::Shared::Packet::ClientSendCheatCommandPacket::PACKET_ID, [&]()
            {
                buffer->Put(MetaGen::Shared::Cheat::CheatCommandEnum::FactionReputationInfo);
                buffer->Serialize(characterGUID);
                buffer->PutU16(factionID);
            });
        }

        bool BuildCheatFactionReputationSet(std::shared_ptr<Bytebuffer>& buffer, ObjectGUID characterGUID, u16 factionID, i32 value)
        {
            return CreatePacket(buffer, MetaGen::Shared::Packet::ClientSendCheatCommandPacket::PACKET_ID, [&]()
            {
                buffer->Put(MetaGen::Shared::Cheat::CheatCommandEnum::FactionReputationSet);
                buffer->Serialize(characterGUID);
                buffer->PutU16(factionID);
                buffer->PutI32(value);
            });
        }

        bool BuildCheatFactionReputationModify(std::shared_ptr<Bytebuffer>& buffer, ObjectGUID characterGUID, u16 factionID, i32 delta)
        {
            return CreatePacket(buffer, MetaGen::Shared::Packet::ClientSendCheatCommandPacket::PACKET_ID, [&]()
            {
                buffer->Put(MetaGen::Shared::Cheat::CheatCommandEnum::FactionReputationModify);
                buffer->Serialize(characterGUID);
                buffer->PutU16(factionID);
                buffer->PutI32(delta);
            });
        }

        bool BuildCheatFactionReputationRemove(std::shared_ptr<Bytebuffer>& buffer, ObjectGUID characterGUID, u16 factionID)
        {
            return CreatePacket(buffer, MetaGen::Shared::Packet::ClientSendCheatCommandPacket::PACKET_ID, [&]()
            {
                buffer->Put(MetaGen::Shared::Cheat::CheatCommandEnum::FactionReputationRemove);
                buffer->Serialize(characterGUID);
                buffer->PutU16(factionID);
            });
        }

        bool BuildCheatFactionReputationSetFlags(std::shared_ptr<Bytebuffer>& buffer, ObjectGUID characterGUID, u16 factionID, u16 flags)
        {
            return CreatePacket(buffer, MetaGen::Shared::Packet::ClientSendCheatCommandPacket::PACKET_ID, [&]()
            {
                buffer->Put(MetaGen::Shared::Cheat::CheatCommandEnum::FactionReputationSetFlags);
                buffer->Serialize(characterGUID);
                buffer->PutU16(factionID);
                buffer->PutU16(flags);
            });
        }

        bool BuildCheatFactionReputationLock(std::shared_ptr<Bytebuffer>& buffer, ObjectGUID characterGUID, u16 factionID, bool locked)
        {
            return CreatePacket(buffer, MetaGen::Shared::Packet::ClientSendCheatCommandPacket::PACKET_ID, [&]()
            {
                buffer->Put(MetaGen::Shared::Cheat::CheatCommandEnum::FactionReputationLock);
                buffer->Serialize(characterGUID);
                buffer->PutU16(factionID);
                buffer->PutU8(locked ? 1u : 0u);
            });
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
        bool BuildDatabaseEditorSnapshotRequest(std::shared_ptr<Bytebuffer>& buffer, MetaGen::Shared::DatabaseEditor::DatabaseEditorTypeEnum editor, u32 requestID)
        {
            return CreatePacket(buffer, MetaGen::Shared::Packet::ClientSendCheatCommandPacket::PACKET_ID, [&]()
            {
                return buffer->Put(MetaGen::Shared::Cheat::CheatCommandEnum::DatabaseEditor) &&
                       buffer->Put(MetaGen::Shared::DatabaseEditor::DatabaseEditorActionEnum::Snapshot) &&
                       buffer->Put(editor) && buffer->PutU32(requestID);
            });
        }

        bool BuildDatabaseEditorMutation(std::shared_ptr<Bytebuffer>& buffer, MetaGen::Shared::DatabaseEditor::DatabaseEditorTypeEnum editor, u8 artifact, MetaGen::Shared::DatabaseEditor::DatabaseEditorMutationTypeEnum mutationType, u32 requestID, const u8* bytes, u32 size)
        {
            constexpr u32 chunkCapacity = 1024;
            using Action = MetaGen::Shared::DatabaseEditor::DatabaseEditorActionEnum;
            using Phase = MetaGen::Shared::DatabaseEditor::DatabaseEditorTransferPhaseEnum;

            bool writeSucceeded = true;
            bool packetSucceeded = CreatePacket(buffer, MetaGen::Shared::Packet::ClientSendCheatCommandPacket::PACKET_ID, [&]()
            {
                writeSucceeded = buffer->Put(MetaGen::Shared::Cheat::CheatCommandEnum::DatabaseEditor) &&
                                 buffer->Put(Action::Mutation) && buffer->Put(Phase::Begin) &&
                                 buffer->Put(editor) && buffer->PutU8(artifact) && buffer->Put(mutationType) &&
                                 buffer->PutU32(requestID) && buffer->PutU32(size);
            });
            if (!packetSucceeded || !writeSucceeded)
                return false;

            for (u32 offset = 0; offset < size; offset += chunkCapacity)
            {
                const u16 chunkSize = static_cast<u16>(std::min(chunkCapacity, size - offset));
                writeSucceeded = true;
                packetSucceeded = CreatePacket(buffer, MetaGen::Shared::Packet::ClientSendCheatCommandPacket::PACKET_ID, [&]()
                {
                    writeSucceeded = buffer->Put(MetaGen::Shared::Cheat::CheatCommandEnum::DatabaseEditor) &&
                                     buffer->Put(Action::Mutation) && buffer->Put(Phase::Chunk) &&
                                     buffer->PutU32(requestID) && buffer->PutU32(offset) &&
                                     buffer->PutU16(chunkSize) && buffer->PutBytes(bytes + offset, chunkSize);
                });
                if (!packetSucceeded || !writeSucceeded)
                    return false;
            }

            writeSucceeded = true;
            packetSucceeded = CreatePacket(buffer, MetaGen::Shared::Packet::ClientSendCheatCommandPacket::PACKET_ID, [&]()
            {
                writeSucceeded = buffer->Put(MetaGen::Shared::Cheat::CheatCommandEnum::DatabaseEditor) &&
                                 buffer->Put(Action::Mutation) && buffer->Put(Phase::Commit) && buffer->PutU32(requestID);
            });
            return packetSucceeded && writeSucceeded;
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
        bool BuildCreatureMove(std::shared_ptr<Bytebuffer>& buffer)
        {
            bool result = CreatePacket(buffer, MetaGen::Shared::Packet::ClientSendCheatCommandPacket::PACKET_ID, [&]()
            {
                buffer->Put(MetaGen::Shared::Cheat::CheatCommandEnum::CreatureMove);
            });

            return result;
        }
        bool BuildCreatureFollow(std::shared_ptr<Bytebuffer>& buffer)
        {
            bool result = CreatePacket(buffer, MetaGen::Shared::Packet::ClientSendCheatCommandPacket::PACKET_ID, [&]()
            {
                buffer->Put(MetaGen::Shared::Cheat::CheatCommandEnum::CreatureFollow);
            });

            return result;
        }
        bool BuildCreatureWander(std::shared_ptr<Bytebuffer>& buffer)
        {
            bool result = CreatePacket(buffer, MetaGen::Shared::Packet::ClientSendCheatCommandPacket::PACKET_ID, [&]()
            {
                buffer->Put(MetaGen::Shared::Cheat::CheatCommandEnum::CreatureWander);
            });

            return result;
        }
        bool BuildCreatureStop(std::shared_ptr<Bytebuffer>& buffer)
        {
            bool result = CreatePacket(buffer, MetaGen::Shared::Packet::ClientSendCheatCommandPacket::PACKET_ID, [&]()
            {
                buffer->Put(MetaGen::Shared::Cheat::CheatCommandEnum::CreatureStop);
            });

            return result;
        }
    }
}

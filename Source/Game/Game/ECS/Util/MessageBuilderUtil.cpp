#include "MessageBuilderUtil.h"
#include "Game/ECS/Components/UnitStatsComponent.h"

#include <Gameplay/Network/Define.h>

#include <entt/entt.hpp>

namespace ECS::Util::MessageBuilder
{
    u32 AddHeader(std::shared_ptr<Bytebuffer>& buffer, Network::GameOpcode opcode, u16 size)
    {
        Network::PacketHeader header =
        {
            .opcode = static_cast<Network::OpcodeType>(opcode),
            .size = size
        };

        if (buffer->GetSpace() < sizeof(Network::PacketHeader))
            return std::numeric_limits<u32>().max();

        u32 headerPos = static_cast<u32>(buffer->writtenData);
        buffer->Put(header);

        return headerPos;
    }

    bool ValidatePacket(const std::shared_ptr<Bytebuffer>& buffer, u32 headerPos)
    {
        if (buffer->writtenData < headerPos + sizeof(Network::PacketHeader))
            return false;

        Network::PacketHeader* header = reinterpret_cast<Network::PacketHeader*>(buffer->GetDataPointer() + headerPos);

        u32 headerSize = static_cast<u32>(buffer->writtenData - headerPos) - sizeof(Network::PacketHeader);
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
        
        bool BuildTargetUpdateMessage(std::shared_ptr<Bytebuffer>& buffer, entt::entity target)
        {
            bool result = CreatePacket(buffer, Network::GameOpcode::Shared_EntityTargetUpdate, [&]()
            {
                buffer->Put(target);
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
    }
}
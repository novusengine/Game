#include "MapEditorBackend.h"

#include "Game-Lib/Application/EnttRegistries.h"
#include "Game-Lib/ECS/Singletons/NetworkState.h"
#include "Game-Lib/ECS/Util/MessageBuilderUtil.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <Base/Memory/Bytebuffer.h>

#include <Network/Client.h>

#include <entt/entt.hpp>

namespace Editor
{
    namespace
    {
        ECS::Singletons::NetworkState* GetNetworkState()
        {
            EnttRegistries* registries = ServiceLocator::GetEnttRegistries();
            if (!registries || !registries->gameRegistry)
                return nullptr;

            auto& context = registries->gameRegistry->ctx();
            return context.contains<ECS::Singletons::NetworkState>() ? &context.get<ECS::Singletons::NetworkState>() : nullptr;
        }
    }

    MapEditorData* MapEditorBackend::GetData(bool create) const
    {
        EnttRegistries* registries = ServiceLocator::GetEnttRegistries();
        if (!registries || !registries->dbRegistry)
            return nullptr;

        auto& context = registries->dbRegistry->ctx();
        if (context.contains<MapEditorData>())
            return &context.get<MapEditorData>();

        return create ? &context.emplace<MapEditorData>() : nullptr;
    }

    bool MapEditorBackend::RequestSnapshot()
    {
        MapEditorData* data = GetData(true);
        ECS::Singletons::NetworkState* networkState = GetNetworkState();
        if (!data || !networkState || !networkState->client || !networkState->client->IsConnected() || !networkState->isInWorld)
            return false;
        if (data->state == DatabaseEditorDataState::Loading)
            return true;

        const u32 requestID = data->StartRequest();
        std::shared_ptr<Bytebuffer> buffer = Bytebuffer::Borrow<64>();
        if (!ECS::Util::MessageBuilder::Cheat::BuildDatabaseEditorSnapshotRequest(buffer, MetaGen::Shared::DatabaseEditor::DatabaseEditorTypeEnum::Map, requestID))
        {
            data->FailSnapshot(requestID);
            return false;
        }

        networkState->client->Send(buffer);
        return true;
    }

    u32 MapEditorBackend::SendMutation(MetaGen::Shared::DatabaseEditor::DatabaseEditorMutationTypeEnum mutationType, const u8* bytes, u32 size)
    {
        MapEditorData* data = GetData(false);
        ECS::Singletons::NetworkState* networkState = GetNetworkState();
        if (!data || data->state != DatabaseEditorDataState::Ready || !networkState || !networkState->client || !networkState->client->IsConnected() || !networkState->isInWorld || !bytes || size == 0)
        {
            return 0;
        }

        const u32 requestID = data->StartMutationRequest();
        std::shared_ptr<Bytebuffer> buffer = Bytebuffer::BorrowRuntime(size + 128);
        if (!buffer || !ECS::Util::MessageBuilder::Cheat::BuildDatabaseEditorMutation(buffer, MetaGen::Shared::DatabaseEditor::DatabaseEditorTypeEnum::Map, 0, mutationType, requestID, bytes, size))
        {
            return 0;
        }

        networkState->client->Send(buffer);
        return requestID;
    }

    u32 MapEditorBackend::CreateMap(u32 flags, std::string_view internalName, std::string_view name, u8 type, u16 maxPlayers)
    {
        std::shared_ptr<Bytebuffer> payload = Bytebuffer::Borrow<640>();
        if (!payload->PutU32(flags) || !payload->PutString(internalName) || !payload->PutString(name) || !payload->PutU8(type) || !payload->PutU16(maxPlayers))
        {
            return 0;
        }

        return SendMutation(MetaGen::Shared::DatabaseEditor::DatabaseEditorMutationTypeEnum::Create, payload->GetDataPointer(), static_cast<u32>(payload->writtenData));
    }

    u32 MapEditorBackend::UpdateMap(const GameDefine::Database::Map& map)
    {
        std::shared_ptr<Bytebuffer> payload = Bytebuffer::Borrow<640>();
        if (!GameDefine::Database::Map::Write(payload.get(), map))
            return 0;

        return SendMutation(MetaGen::Shared::DatabaseEditor::DatabaseEditorMutationTypeEnum::Update, payload->GetDataPointer(), static_cast<u32>(payload->writtenData));
    }

    std::optional<DatabaseEditorMutationResult> MapEditorBackend::TakeMutationResult(u32 requestID)
    {
        MapEditorData* data = GetData(false);
        return data ? data->TakeMutationResult(requestID) : std::nullopt;
    }
}

#include "Game-Lib/Editor/DatabaseEditorData.h"

#include <Base/Memory/Bytebuffer.h>

#include <Network/Define.h>

#include <catch2/catch2.hpp>

#include <algorithm>
#include <limits>
#include <memory>
#include <optional>
#include <vector>

namespace
{
    class RevisionedEditorData final : public Editor::DatabaseEditorData
    {
    public:
        RevisionedEditorData() : DatabaseEditorData(1) {}

        std::vector<u8> appliedValues;

    private:
        bool ValidateSnapshot(std::vector<::ClientDB::Data>& storages) const override
        {
            return storages.size() == 1;
        }

        bool ApplyChangeSet(u16 changeCount, Bytebuffer& payload) override
        {
            for (u16 index = 0; index < changeCount; ++index)
            {
                u8 artifact = 0;
                u8 mutationType = 0;
                u32 artifactID = 0;
                u32 payloadLength = 0;
                u8 value = 0;
                if (!payload.GetU8(artifact) || !payload.GetU8(mutationType) || !payload.GetU32(artifactID) || !payload.GetU32(payloadLength) ||
                    artifact != 0 || mutationType != static_cast<u8>(MetaGen::Shared::DatabaseEditor::DatabaseEditorMutationTypeEnum::Update) ||
                    artifactID == 0 || payloadLength == 0 || payloadLength > payload.GetActiveSize() || !payload.GetU8(value) || !payload.SkipRead(payloadLength - 1))
                {
                    return false;
                }
                appliedValues.push_back(value);
            }

            return true;
        }
    };

    std::shared_ptr<Bytebuffer> MakeChangeSet(u32 artifactID, u8 value)
    {
        std::shared_ptr<Bytebuffer> payload = Bytebuffer::BorrowRuntime(32);
        REQUIRE(payload);
        REQUIRE(payload->PutU8(0));
        REQUIRE(payload->PutU8(static_cast<u8>(MetaGen::Shared::DatabaseEditor::DatabaseEditorMutationTypeEnum::Update)));
        REQUIRE(payload->PutU32(artifactID));
        REQUIRE(payload->PutU32(1));
        REQUIRE(payload->PutU8(value));
        return payload;
    }
}

TEST_CASE("Database editor change sets queue against a snapshot baseline and enforce revision order", "[DatabaseEditor]")
{
    RevisionedEditorData data;
    const u32 requestID = data.StartRequest();
    REQUIRE(data.BeginSnapshot(requestID, 1, 10));

    std::shared_ptr<Bytebuffer> second = MakeChangeSet(2, 2);
    std::shared_ptr<Bytebuffer> first = MakeChangeSet(1, 1);
    REQUIRE(data.ReceiveChangeSet(12, 1, second->GetDataPointer(), second->writtenData));
    REQUIRE(data.ReceiveChangeSet(11, 1, first->GetDataPointer(), first->writtenData));

    ::ClientDB::Data storage;
    REQUIRE(storage.Initialize(1));
    std::shared_ptr<Bytebuffer> snapshot = Bytebuffer::BorrowRuntime(storage.GetSerializedSize());
    REQUIRE(snapshot);
    REQUIRE(storage.Save(snapshot));
    REQUIRE(snapshot->writtenData <= std::numeric_limits<u16>::max());
    REQUIRE(data.AppendSnapshotChunk(requestID, 0, static_cast<u32>(snapshot->writtenData), 0, snapshot->GetDataPointer(), static_cast<u16>(snapshot->writtenData)));
    REQUIRE(data.CompleteSnapshot(requestID, true));

    CHECK(data.GetRevision() == 12);
    REQUIRE(data.appliedValues.size() == 2);
    CHECK(data.appliedValues[0] == 1);
    CHECK(data.appliedValues[1] == 2);

    constexpr size_t CHANGE_SET_PACKET_PAYLOAD_LIMIT = std::min<size_t>(std::numeric_limits<u16>::max(), Network::DEFAULT_BUFFER_SIZE - sizeof(Network::MessageHeader));
    constexpr size_t CHANGE_SET_HEADER_SIZE = sizeof(u8) + sizeof(u64) + sizeof(u16);
    constexpr size_t CHANGE_HEADER_SIZE = sizeof(u8) + sizeof(u8) + sizeof(u32) + sizeof(u32);
    constexpr size_t MAX_BODY_SIZE = CHANGE_SET_PACKET_PAYLOAD_LIMIT - CHANGE_SET_HEADER_SIZE;
    constexpr u32 MAX_CANONICAL_PAYLOAD_SIZE = static_cast<u32>(MAX_BODY_SIZE - CHANGE_HEADER_SIZE);
    std::shared_ptr<Bytebuffer> boundary = Bytebuffer::BorrowRuntime(MAX_BODY_SIZE);
    REQUIRE(boundary);
    REQUIRE(boundary->PutU8(0));
    REQUIRE(boundary->PutU8(static_cast<u8>(MetaGen::Shared::DatabaseEditor::DatabaseEditorMutationTypeEnum::Update)));
    REQUIRE(boundary->PutU32(3));
    REQUIRE(boundary->PutU32(MAX_CANONICAL_PAYLOAD_SIZE));
    REQUIRE(boundary->PutU8(3));
    const std::vector<u8> padding(MAX_CANONICAL_PAYLOAD_SIZE - 1, 0);
    REQUIRE(boundary->PutBytes(padding.data(), padding.size()));
    REQUIRE(boundary->writtenData == MAX_BODY_SIZE);
    CHECK(data.ReceiveChangeSet(13, 1, boundary->GetDataPointer(), boundary->writtenData));
    CHECK(data.GetRevision() == 13);
    REQUIRE(data.appliedValues.size() == 3);
    CHECK(data.appliedValues[2] == 3);

    std::shared_ptr<Bytebuffer> stale = MakeChangeSet(4, 4);
    CHECK(data.ReceiveChangeSet(13, 1, stale->GetDataPointer(), stale->writtenData));
    CHECK(data.appliedValues.size() == 3);

    std::shared_ptr<Bytebuffer> gap = MakeChangeSet(5, 5);
    CHECK_FALSE(data.ReceiveChangeSet(15, 1, gap->GetDataPointer(), gap->writtenData));
    CHECK(data.state == Editor::DatabaseEditorDataState::Failed);
    CHECK(data.GetRevision() == 13);

    data.RecordMutationResult({ .requestID = 7, .artifactID = 5, .succeeded = true, .revision = 13 });
    const std::optional<Editor::DatabaseEditorMutationResult> result = data.TakeMutationResult(7);
    REQUIRE(result);
    CHECK(result->revision == 13);

    RevisionedEditorData oversizedData;
    oversizedData.StartRequest();
    const std::vector<u8> oversizedBody(MAX_BODY_SIZE + 1, 0);
    CHECK_FALSE(oversizedData.ReceiveChangeSet(1, 1, oversizedBody.data(), oversizedBody.size()));
    CHECK(oversizedData.state == Editor::DatabaseEditorDataState::Failed);
}

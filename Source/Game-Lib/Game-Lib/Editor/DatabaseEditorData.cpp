#include "DatabaseEditorData.h"

#include <Base/Memory/Bytebuffer.h>

#include <Network/Define.h>

#include <algorithm>
#include <limits>
#include <memory>
#include <utility>

namespace Editor
{
    namespace
    {
        constexpr u32 MAX_ARTIFACT_SIZE = 64 * 1024 * 1024;
        constexpr size_t MAX_CHANGE_SET_PACKET_PAYLOAD_SIZE = std::min<size_t>(std::numeric_limits<u16>::max(), Network::DEFAULT_BUFFER_SIZE - sizeof(Network::MessageHeader));
        constexpr size_t CHANGE_SET_HEADER_SIZE = sizeof(u8) + sizeof(u64) + sizeof(u16);
        constexpr size_t CHANGE_HEADER_SIZE = sizeof(u8) + sizeof(u8) + sizeof(u32) + sizeof(u32);
        constexpr size_t MAX_CHANGE_SET_BODY_SIZE = MAX_CHANGE_SET_PACKET_PAYLOAD_SIZE - CHANGE_SET_HEADER_SIZE;
        constexpr size_t MAX_QUEUED_CHANGE_SETS = 256;
        constexpr u16 MAX_CHANGE_SET_CHANGES = static_cast<u16>(MAX_CHANGE_SET_BODY_SIZE / CHANGE_HEADER_SIZE);
        constexpr size_t MAX_MUTATION_RESULTS = 64;
    }

    DatabaseEditorData::DatabaseEditorData(u8 artifactCount)
        : _artifactCount(artifactCount), _incomingArtifacts(artifactCount), _storages(artifactCount)
    {
    }

    u32 DatabaseEditorData::StartRequest()
    {
        ++_nextRequestID;
        if (_nextRequestID == 0)
            ++_nextRequestID;

        _requestID = _nextRequestID;
        state = DatabaseEditorDataState::Loading;
        _snapshotRevision = 0;
        _incomingChangeSets.clear();
        for (IncomingArtifact& artifact : _incomingArtifacts)
        {
            artifact = {};
        }

        return _requestID;
    }

    u32 DatabaseEditorData::StartMutationRequest()
    {
        ++_nextRequestID;
        if (_nextRequestID == 0)
            ++_nextRequestID;
        return _nextRequestID;
    }

    bool DatabaseEditorData::BeginSnapshot(u32 requestID, u8 artifactCount, u64 revision)
    {
        if (state != DatabaseEditorDataState::Loading || requestID != _requestID || artifactCount != _artifactCount)
            return false;

        for (IncomingArtifact& artifact : _incomingArtifacts)
        {
            artifact = {};
        }
        _snapshotRevision = revision;
        return true;
    }

    bool DatabaseEditorData::AppendSnapshotChunk(u32 requestID, u8 artifactIndex, u32 totalSize, u32 offset, const u8* bytes, u16 size)
    {
        if (state != DatabaseEditorDataState::Loading || requestID != _requestID || artifactIndex >= _artifactCount || totalSize == 0 || totalSize > MAX_ARTIFACT_SIZE || size == 0 || !bytes)
        {
            return false;
        }

        IncomingArtifact& artifact = _incomingArtifacts[artifactIndex];
        if (artifact.totalSize == 0)
        {
            artifact.totalSize = totalSize;
            artifact.bytes.reserve(totalSize);
        }
        else if (artifact.totalSize != totalSize)
        {
            return false;
        }

        if (offset != artifact.bytes.size() || static_cast<u64>(offset) + size > totalSize)
            return false;

        artifact.bytes.insert(artifact.bytes.end(), bytes, bytes + size);
        return true;
    }

    bool DatabaseEditorData::CompleteSnapshot(u32 requestID, bool succeeded)
    {
        if (state != DatabaseEditorDataState::Loading || requestID != _requestID || !succeeded)
        {
            FailSnapshot(requestID);
            return false;
        }

        std::vector<::ClientDB::Data> storages(_artifactCount);
        for (size_t index = 0; index < _artifactCount; ++index)
        {
            const IncomingArtifact& artifact = _incomingArtifacts[index];
            if (artifact.totalSize == 0 || artifact.bytes.size() != artifact.totalSize || artifact.totalSize > std::numeric_limits<u32>::max())
            {
                FailSnapshot(requestID);
                return false;
            }

            std::shared_ptr<Bytebuffer> buffer = Bytebuffer::BorrowRuntime(artifact.totalSize);
            if (!buffer || !buffer->PutBytes(artifact.bytes.data(), artifact.bytes.size()) || !storages[index].Read(buffer))
            {
                FailSnapshot(requestID);
                return false;
            }
        }

        if (!ValidateSnapshot(storages))
        {
            FailSnapshot(requestID);
            return false;
        }

        for (size_t index = 0; index < _artifactCount; ++index)
        {
            const IncomingArtifact& artifact = _incomingArtifacts[index];
            std::shared_ptr<Bytebuffer> buffer = Bytebuffer::BorrowRuntime(artifact.totalSize);
            if (!buffer || !buffer->PutBytes(artifact.bytes.data(), artifact.bytes.size()) || !_storages[index].Read(buffer))
            {
                FailSnapshot(requestID);
                return false;
            }
            _incomingArtifacts[index] = {};
        }

        _revision = _snapshotRevision;
        std::ranges::sort(_incomingChangeSets, {}, &IncomingChangeSet::revision);
        for (const IncomingChangeSet& changeSet : _incomingChangeSets)
        {
            if (changeSet.revision <= _revision)
                continue;
            if (changeSet.revision != _revision + 1)
            {
                FailSnapshot(requestID);
                return false;
            }

            std::shared_ptr<Bytebuffer> payload = Bytebuffer::BorrowRuntime(std::max<size_t>(changeSet.bytes.size(), 1));
            if (!payload || (!changeSet.bytes.empty() && !payload->PutBytes(changeSet.bytes.data(), changeSet.bytes.size())) || !ApplyChangeSet(changeSet.changeCount, *payload) || payload->GetActiveSize() != 0)
            {
                FailSnapshot(requestID);
                return false;
            }
            _revision = changeSet.revision;
        }
        _incomingChangeSets.clear();
        state = DatabaseEditorDataState::Ready;
        OnSnapshotLoaded();
        return true;
    }

    void DatabaseEditorData::FailSnapshot(u32 requestID)
    {
        if (requestID != _requestID)
            return;

        for (IncomingArtifact& artifact : _incomingArtifacts)
        {
            artifact = {};
        }
        _incomingChangeSets.clear();
        state = DatabaseEditorDataState::Failed;
    }

    void DatabaseEditorData::RecordMutationResult(DatabaseEditorMutationResult result)
    {
        if (_mutationResults.size() == MAX_MUTATION_RESULTS)
            _mutationResults.pop_front();
        _mutationResults.push_back(std::move(result));
    }

    std::optional<DatabaseEditorMutationResult> DatabaseEditorData::TakeMutationResult(u32 requestID)
    {
        const auto resultItr = std::ranges::find(_mutationResults, requestID, &DatabaseEditorMutationResult::requestID);
        if (resultItr == _mutationResults.end())
            return std::nullopt;

        DatabaseEditorMutationResult result = std::move(*resultItr);
        _mutationResults.erase(resultItr);
        return result;
    }

    bool DatabaseEditorData::ReceiveChangeSet(u64 revision, u16 changeCount, const u8* bytes, size_t size)
    {
        if (changeCount == 0 || changeCount > MAX_CHANGE_SET_CHANGES || size > MAX_CHANGE_SET_BODY_SIZE || (size > 0 && !bytes))
        {
            state = DatabaseEditorDataState::Failed;
            return false;
        }

        if (state == DatabaseEditorDataState::Loading)
        {
            if (_incomingChangeSets.size() >= MAX_QUEUED_CHANGE_SETS)
            {
                state = DatabaseEditorDataState::Failed;
                return false;
            }
            IncomingChangeSet& changeSet = _incomingChangeSets.emplace_back();
            changeSet.revision = revision;
            changeSet.changeCount = changeCount;
            if (size > 0)
                changeSet.bytes.assign(bytes, bytes + size);
            return true;
        }

        if (state != DatabaseEditorDataState::Ready)
            return false;
        if (revision <= _revision)
            return true;
        if (revision != _revision + 1)
        {
            state = DatabaseEditorDataState::Failed;
            return false;
        }

        std::shared_ptr<Bytebuffer> payload = Bytebuffer::BorrowRuntime(std::max<size_t>(size, 1));
        if (!payload || (size > 0 && !payload->PutBytes(bytes, size)) || !ApplyChangeSet(changeCount, *payload) || payload->GetActiveSize() != 0)
        {
            state = DatabaseEditorDataState::Failed;
            return false;
        }
        _revision = revision;
        OnSnapshotLoaded();
        return true;
    }

    ::ClientDB::Data* DatabaseEditorData::GetStorage(u8 artifact)
    {
        return artifact < _storages.size() ? &_storages[artifact] : nullptr;
    }

    const ::ClientDB::Data* DatabaseEditorData::GetStorage(u8 artifact) const
    {
        return artifact < _storages.size() ? &_storages[artifact] : nullptr;
    }
}

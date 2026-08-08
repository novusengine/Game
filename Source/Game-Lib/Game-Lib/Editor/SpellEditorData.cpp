#include "SpellEditorData.h"

#include "Game-Lib/ECS/Util/Database/SpellUtil.h"

#include <Base/Memory/Bytebuffer.h>

#include <MetaGen/Shared/ClientDB/ClientDB.h>

#include <algorithm>
#include <limits>
#include <utility>

namespace Editor
{
    namespace
    {
        constexpr u32 MAX_ARTIFACT_SIZE = 64 * 1024 * 1024;
        constexpr size_t MAX_MUTATION_RESULTS = 64;

        bool HasExpectedSchema(::ClientDB::Data& storage, const std::vector<::ClientDB::FieldInfo>& expectedFields)
        {
            const std::vector<::ClientDB::FieldInfo>& fields = storage.GetFields();
            if (fields.size() != expectedFields.size())
                return false;

            for (size_t index = 0; index < fields.size(); ++index)
            {
                const ::ClientDB::FieldInfo& field = fields[index];
                const ::ClientDB::FieldInfo& expectedField = expectedFields[index];
                if (field.name != expectedField.name || field.type != expectedField.type || field.count != expectedField.count)
                    return false;
            }

            return true;
        }
    }

    u32 SpellEditorData::StartRequest()
    {
        ++_nextRequestID;
        if (_nextRequestID == 0)
            ++_nextRequestID;

        _requestID = _nextRequestID;
        state = SpellEditorDataState::Loading;
        for (IncomingArtifact& artifact : _incomingArtifacts)
        {
            artifact = {};
        }

        return _requestID;
    }

    u32 SpellEditorData::StartMutationRequest()
    {
        ++_nextRequestID;
        if (_nextRequestID == 0)
            ++_nextRequestID;

        return _nextRequestID;
    }

    bool SpellEditorData::BeginSnapshot(u32 requestID, u8 artifactCount)
    {
        if (state != SpellEditorDataState::Loading || requestID != _requestID || artifactCount != ARTIFACT_COUNT)
            return false;

        for (IncomingArtifact& artifact : _incomingArtifacts)
        {
            artifact = {};
        }

        return true;
    }

    bool SpellEditorData::AppendSnapshotChunk(u32 requestID, MetaGen::Shared::Spell::SpellEditorArtifactEnum type,
        u32 totalSize, u32 offset, const u8* bytes, u16 size)
    {
        const size_t index = static_cast<size_t>(type);
        if (state != SpellEditorDataState::Loading || requestID != _requestID || index >= ARTIFACT_COUNT ||
            totalSize == 0 || totalSize > MAX_ARTIFACT_SIZE || size == 0 || !bytes)
        {
            return false;
        }

        IncomingArtifact& artifact = _incomingArtifacts[index];
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

    bool SpellEditorData::CompleteSnapshot(u32 requestID, bool succeeded)
    {
        if (state != SpellEditorDataState::Loading || requestID != _requestID || !succeeded)
        {
            FailSnapshot(requestID);
            return false;
        }

        std::array<::ClientDB::Data, ARTIFACT_COUNT> storages;
        for (size_t index = 0; index < ARTIFACT_COUNT; ++index)
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

        using ArtifactType = MetaGen::Shared::Spell::SpellEditorArtifactEnum;
        if (!HasExpectedSchema(storages[static_cast<size_t>(ArtifactType::Spell)], MetaGen::Shared::ClientDB::SpellRecord::FIELD_LIST) ||
            !HasExpectedSchema(storages[static_cast<size_t>(ArtifactType::SpellAura)], MetaGen::Shared::ClientDB::SpellAuraRecord::FIELD_LIST) ||
            !HasExpectedSchema(storages[static_cast<size_t>(ArtifactType::SpellEffects)], MetaGen::Shared::ClientDB::SpellEffectsRecord::FIELD_LIST) ||
            !HasExpectedSchema(storages[static_cast<size_t>(ArtifactType::SpellProcData)], MetaGen::Shared::ClientDB::SpellProcDataRecord::FIELD_LIST) ||
            !HasExpectedSchema(storages[static_cast<size_t>(ArtifactType::SpellProcLink)], MetaGen::Shared::ClientDB::SpellProcLinkRecord::FIELD_LIST) ||
            !HasExpectedSchema(storages[static_cast<size_t>(ArtifactType::SpellAuraConstraintGroup)], MetaGen::Shared::ClientDB::SpellAuraConstraintGroupRecord::FIELD_LIST) ||
            !HasExpectedSchema(storages[static_cast<size_t>(ArtifactType::SpellAuraConstraint)], MetaGen::Shared::ClientDB::SpellAuraConstraintRecord::FIELD_LIST))
        {
            FailSnapshot(requestID);
            return false;
        }

        for (size_t index = 0; index < ARTIFACT_COUNT; ++index)
        {
            const IncomingArtifact& artifact = _incomingArtifacts[index];
            std::shared_ptr<Bytebuffer> buffer = Bytebuffer::BorrowRuntime(artifact.totalSize);
            if (!buffer || !buffer->PutBytes(artifact.bytes.data(), artifact.bytes.size()) || !_storages[index].Read(buffer))
            {
                FailSnapshot(requestID);
                return false;
            }
        }

        spellIndex.spellIDToEffectList.clear();
        ::ClientDB::Data& effects = _storages[static_cast<size_t>(ArtifactType::SpellEffects)];
        effects.Each([&](u32 effectID, const MetaGen::Shared::ClientDB::SpellEffectsRecord& effect)
        {
            ECSUtil::Spell::AddSpellEffect(spellIndex, effect.spellID, effectID);
            return true;
        });

        for (const auto& entry : spellIndex.spellIDToEffectList)
        {
            ECSUtil::Spell::SortSpellEffects(spellIndex, &effects, entry.first);
        }

        for (IncomingArtifact& artifact : _incomingArtifacts)
        {
            artifact = {};
        }

        state = SpellEditorDataState::Ready;
        return true;
    }

    void SpellEditorData::FailSnapshot(u32 requestID)
    {
        if (requestID != _requestID)
            return;

        for (IncomingArtifact& artifact : _incomingArtifacts)
        {
            artifact = {};
        }

        state = SpellEditorDataState::Failed;
    }

    void SpellEditorData::RecordMutationResult(SpellEditorMutationResult result)
    {
        if (_mutationResults.size() == MAX_MUTATION_RESULTS)
            _mutationResults.pop_front();

        _mutationResults.push_back(std::move(result));
    }

    std::optional<SpellEditorMutationResult> SpellEditorData::TakeMutationResult(u32 requestID)
    {
        const auto resultItr = std::ranges::find(_mutationResults, requestID, &SpellEditorMutationResult::requestID);
        if (resultItr == _mutationResults.end())
            return std::nullopt;

        SpellEditorMutationResult result = std::move(*resultItr);
        _mutationResults.erase(resultItr);
        return result;
    }

    ::ClientDB::Data* SpellEditorData::GetStorage(MetaGen::Shared::Spell::SpellEditorArtifactEnum type)
    {
        const size_t index = static_cast<size_t>(type);
        return index < ARTIFACT_COUNT ? &_storages[index] : nullptr;
    }

    const ::ClientDB::Data* SpellEditorData::GetStorage(MetaGen::Shared::Spell::SpellEditorArtifactEnum type) const
    {
        const size_t index = static_cast<size_t>(type);
        return index < ARTIFACT_COUNT ? &_storages[index] : nullptr;
    }
}

#pragma once

#include "Game-Lib/ECS/Singletons/Database/SpellSingleton.h"

#include <FileFormat/Novus/ClientDB/ClientDB.h>

#include <MetaGen/Shared/Spell/Spell.h>

#include <array>
#include <deque>
#include <optional>
#include <string>
#include <vector>

namespace Editor
{
    enum class SpellEditorDataState : u8
    {
        Unavailable,
        Loading,
        Ready,
        Failed
    };

    struct SpellEditorMutationResult
    {
    public:
        u32 requestID = 0;
        MetaGen::Shared::Spell::SpellEditorArtifactEnum artifact = MetaGen::Shared::Spell::SpellEditorArtifactEnum::Spell;
        u32 artifactID = 0;
        MetaGen::Shared::Spell::SpellEditorMutationTypeEnum mutationType = MetaGen::Shared::Spell::SpellEditorMutationTypeEnum::Update;
        bool succeeded = false;
        std::string response;
    };

    struct SpellEditorData
    {
    public:
        u32 StartRequest();
        u32 StartMutationRequest();
        bool BeginSnapshot(u32 requestID, u8 artifactCount);
        bool AppendSnapshotChunk(u32 requestID, MetaGen::Shared::Spell::SpellEditorArtifactEnum type,
            u32 totalSize, u32 offset, const u8* bytes, u16 size);
        bool CompleteSnapshot(u32 requestID, bool succeeded);
        void FailSnapshot(u32 requestID);
        void RecordMutationResult(SpellEditorMutationResult result);
        std::optional<SpellEditorMutationResult> TakeMutationResult(u32 requestID);

        ::ClientDB::Data* GetStorage(MetaGen::Shared::Spell::SpellEditorArtifactEnum type);
        const ::ClientDB::Data* GetStorage(MetaGen::Shared::Spell::SpellEditorArtifactEnum type) const;

    public:
        SpellEditorDataState state = SpellEditorDataState::Unavailable;
        ECS::Singletons::SpellSingleton spellIndex;

    private:
        static constexpr size_t ARTIFACT_COUNT = static_cast<size_t>(MetaGen::Shared::Spell::SpellEditorArtifactEnum::Count);

        struct IncomingArtifact
        {
        public:
            u32 totalSize = 0;
            std::vector<u8> bytes;
        };

    private:
        u32 _nextRequestID = 0;
        u32 _requestID = 0;
        std::array<IncomingArtifact, ARTIFACT_COUNT> _incomingArtifacts;
        std::array<::ClientDB::Data, ARTIFACT_COUNT> _storages;
        std::deque<SpellEditorMutationResult> _mutationResults;
    };
}

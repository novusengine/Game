#pragma once

#include <FileFormat/Novus/ClientDB/ClientDB.h>

#include <MetaGen/Shared/DatabaseEditor/DatabaseEditor.h>

#include <deque>
#include <optional>
#include <string>
#include <vector>

class Bytebuffer;

namespace Editor
{
    enum class DatabaseEditorDataState : u8
    {
        Unavailable,
        Loading,
        Ready,
        Failed
    };

    struct DatabaseEditorMutationResult
    {
    public:
        u32 requestID = 0;
        u8 artifact = 0;
        u32 artifactID = 0;
        MetaGen::Shared::DatabaseEditor::DatabaseEditorMutationTypeEnum mutationType = MetaGen::Shared::DatabaseEditor::DatabaseEditorMutationTypeEnum::Update;
        bool succeeded = false;
        u64 revision = 0;
        std::string response;
    };

    class DatabaseEditorData
    {
    public:
        explicit DatabaseEditorData(u8 artifactCount);
        virtual ~DatabaseEditorData() = default;

        DatabaseEditorData(const DatabaseEditorData&) = delete;
        DatabaseEditorData& operator=(const DatabaseEditorData&) = delete;

        u32 StartRequest();
        u32 StartMutationRequest();
        bool BeginSnapshot(u32 requestID, u8 artifactCount, u64 revision);
        bool AppendSnapshotChunk(u32 requestID, u8 artifact, u32 totalSize, u32 offset, const u8* bytes, u16 size);
        bool CompleteSnapshot(u32 requestID, bool succeeded);
        void FailSnapshot(u32 requestID);
        void RecordMutationResult(DatabaseEditorMutationResult result);
        std::optional<DatabaseEditorMutationResult> TakeMutationResult(u32 requestID);
        bool ReceiveChangeSet(u64 revision, u16 changeCount, const u8* bytes, size_t size);
        void FailChangeSet() { state = DatabaseEditorDataState::Failed; }
        u64 GetRevision() const { return _revision; }

        ::ClientDB::Data* GetStorage(u8 artifact);
        const ::ClientDB::Data* GetStorage(u8 artifact) const;

    public:
        DatabaseEditorDataState state = DatabaseEditorDataState::Unavailable;

    protected:
        virtual bool ValidateSnapshot(std::vector<::ClientDB::Data>& storages) const = 0;
        virtual bool ApplyChangeSet(u16, Bytebuffer&) { return false; }
        virtual void OnSnapshotLoaded() {}

    private:
        struct IncomingArtifact
        {
        public:
            u32 totalSize = 0;
            std::vector<u8> bytes;
        };

        struct IncomingChangeSet
        {
        public:
            u64 revision = 0;
            u16 changeCount = 0;
            std::vector<u8> bytes;
        };

    private:
        u32 _nextRequestID = 0;
        u32 _requestID = 0;
        u8 _artifactCount = 0;
        u64 _revision = 0;
        u64 _snapshotRevision = 0;
        std::vector<IncomingArtifact> _incomingArtifacts;
        std::vector<IncomingChangeSet> _incomingChangeSets;
        std::vector<::ClientDB::Data> _storages;
        std::deque<DatabaseEditorMutationResult> _mutationResults;
    };
}

#pragma once

#include <Base/Types.h>
#include <Gameplay/Network/Define.h>

#include <MetaGen/Shared/Development/Development.h>
#include <MetaGen/Shared/Packet/Packet.h>

#include <chrono>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

class Bytebuffer;

namespace Editor
{
    struct CreatureAIScriptCatalogEntry
    {
    public:
        std::string name;
        std::string relativePath;
        u64 revision = 0;
        bool sourceAvailable = false;
        bool locked = false;
        bool lockedByRequester = false;
    };

    struct CreatureAIInspection
    {
    public:
        ObjectGUID creatureGUID;
        u32 creatureTemplateID = 0;
        std::string guidScriptName;
        std::string templateScriptName;
        std::string effectiveScriptName;
        MetaGen::Shared::Development::CreatureAIScriptBindingScopeEnum effectiveScope = MetaGen::Shared::Development::CreatureAIScriptBindingScopeEnum::None;
    };

    class CreatureAIEditorBackend
    {
    public:
        enum class TransferPurpose : u8
        {
            None,
            Catalog,
            Workspace,
            View,
            Checkout,
            Duplicate
        };

        struct PendingRequest
        {
        public:
            MetaGen::Shared::Development::DevelopmentOperationEnum operation = MetaGen::Shared::Development::DevelopmentOperationEnum::None;
            MetaGen::Shared::Development::DevelopmentResourceEnum resource = MetaGen::Shared::Development::DevelopmentResourceEnum::CreatureAIScript;
            TransferPurpose transferPurpose = TransferPurpose::None;
            std::string scriptName;
            std::string relativePath;
            std::string duplicateName;
            std::string duplicatePath;
            std::vector<u8> source;
            u64 leaseToken = 0;
            u64 revision = 0;
            u32 leaseDurationMilliseconds = 0;
            bool synchronizeWorkspace = false;
        };

        struct IncomingTransfer
        {
        public:
            u32 requestID = 0;
            u64 revision = 0;
            std::vector<u8> bytes;
            size_t receivedBytes = 0;
        };

        struct Checkout
        {
        public:
            std::string scriptName;
            std::string relativePath;
            std::filesystem::path localPath;
            u64 leaseToken = 0;
            u64 revision = 0;
            std::filesystem::file_time_type lastWriteTime;
            std::chrono::steady_clock::time_point nextLeaseRenewal;
            bool uploadPending = false;
            bool releaseRequested = false;
        };

    public:
        void Update();
        void Reset();

        bool RequestCatalog();
        bool InspectUnit(u32 unitID);
        void ClearInspection();
        bool View(std::string scriptName);
        bool Edit(std::string scriptName);
        bool Create(std::string scriptName, std::string relativePath, std::string source);
        bool Duplicate(std::string sourceScriptName, std::string scriptName, std::string relativePath);
        bool FinishEditing(std::string scriptName);
        bool Link(bool guidScope, std::string scriptName);
        bool Unlink(bool guidScope);

        void HandleActionResult(MetaGen::Shared::Packet::ServerDevelopmentActionResultPacket& packet);
        bool BeginTransfer(u32 requestID, u32 totalSize, u64 revision);
        bool AppendTransfer(u32 requestID, u32 offset, const u8* bytes, u16 size);
        bool CompleteTransfer(u32 requestID, bool succeeded);
        void HandleInspection(MetaGen::Shared::Packet::ServerCreatureAIDevelopmentInfoPacket& packet);

        const std::vector<CreatureAIScriptCatalogEntry>& GetCatalog() const { return _catalog; }
        const std::optional<CreatureAIInspection>& GetInspection() const { return _inspection; }
        const std::string& GetStatus() const { return _status; }
        u64 GetChangeVersion() const { return _changeVersion; }
        bool IsBusy() const { return !_pendingRequests.empty(); }

    private:
        bool SendSimple(MetaGen::Shared::Development::DevelopmentOperationEnum operation, PendingRequest request);
        bool RequestCatalog(bool synchronizeWorkspace);
        bool RequestWorkspace();
        bool SendUpload(Checkout& checkout, const std::vector<u8>& source);
        bool SendReserve(PendingRequest request);
        bool SendBinding(MetaGen::Shared::Development::DevelopmentOperationEnum operation, bool guidScope, std::string scriptName);
        bool SendLease(MetaGen::Shared::Development::DevelopmentOperationEnum operation, const Checkout& checkout);
        bool SendPacket(const std::function<bool(::Bytebuffer&)>& writePayload);
        u32 StartRequest(PendingRequest request);
        void FailRequest(u32 requestID, std::string status);
        void FinishTransfer(u32 requestID, PendingRequest request, std::vector<u8> bytes);
        bool ParseCatalog(const std::vector<u8>& bytes);
        bool WriteWorkspace(const std::vector<u8>& bytes);
        bool WriteLocalSource(const PendingRequest& request, const std::vector<u8>& bytes, bool editable, std::filesystem::path& outPath);
        bool OpenInIDE(const std::filesystem::path& path);
        void BeginCreation(PendingRequest request, std::vector<u8> source);
        void SetStatus(std::string status);
        ObjectGUID GetInspectedGUID() const;

    private:
        std::vector<CreatureAIScriptCatalogEntry> _catalog;
        std::optional<CreatureAIInspection> _inspection;
        std::unordered_map<u32, PendingRequest> _pendingRequests;
        std::optional<IncomingTransfer> _incomingTransfer;
        std::unordered_map<std::string, Checkout> _checkouts;
        std::string _status = "Creature AI data has not been requested.";
        u64 _changeVersion = 1;
        u32 _nextRequestID = 1;
        u32 _latestInspectionRequestID = 0;
        bool _workspaceReady = false;
        std::chrono::steady_clock::time_point _nextFilePoll;
    };
}

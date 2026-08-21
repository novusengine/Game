#include "CreatureAIEditorBackend.h"

#include "Game-Lib/Application/EnttRegistries.h"
#include "Game-Lib/ECS/Singletons/NetworkState.h"
#include "Game-Lib/ECS/Util/MessageBuilderUtil.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <Base/CVarSystem/CVarSystemPrivate.h>
#include <Base/Memory/Bytebuffer.h>

#include <MetaGen/Shared/Cheat/Cheat.h>

#include <Network/Client.h>

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <limits>
#include <system_error>
#include <unordered_set>

#if defined(_WIN32)
#include <Windows.h>
#include <shellapi.h>
#endif

namespace Editor
{
    namespace
    {
        using Operation = MetaGen::Shared::Development::DevelopmentOperationEnum;
        using Resource = MetaGen::Shared::Development::DevelopmentResourceEnum;
        using Result = MetaGen::Shared::Development::DevelopmentResultEnum;
        using BindingScope = MetaGen::Shared::Development::CreatureAIScriptBindingScopeEnum;

        constexpr u32 MAX_SCRIPT_SOURCE_SIZE = 4 * 1024 * 1024;
        constexpr u32 MAX_CATALOG_SIZE = 8 * 1024 * 1024;
        constexpr u32 MAX_WORKSPACE_SIZE = 16 * 1024 * 1024;
        constexpr u32 MAX_WORKSPACE_FILE_COUNT = 4096;
        constexpr u16 TRANSFER_CHUNK_SIZE = 1024;
        constexpr auto FILE_POLL_INTERVAL = std::chrono::milliseconds(350);

        enum class UploadPhase : u8
        {
            Begin,
            Chunk,
            Commit
        };

        AutoCVar_String CVAR_CreatureAIEditorIDE(CVarCategory::Client, "creatureAIEditorIDEExecutable", "Executable used to edit checked-out creature AI scripts. Empty locates a standard Visual Studio Code installation.", "");
        AutoCVar_String CVAR_CreatureAIEditorIDEArguments(CVarCategory::Client, "creatureAIEditorIDEArguments", "Arguments passed to the creature AI editor. Use {workspace} and {file} for their paths.", "--new-window {workspace} --goto {file}");

        ECS::Singletons::NetworkState* GetNetworkState()
        {
            EnttRegistries* registries = ServiceLocator::GetEnttRegistries();
            if (!registries || !registries->gameRegistry)
                return nullptr;

            auto& context = registries->gameRegistry->ctx();
            return context.contains<ECS::Singletons::NetworkState>()
                ? &context.get<ECS::Singletons::NetworkState>()
                : nullptr;
        }

        std::string SanitizePathSegment(std::string value)
        {
            for (char& character : value)
            {
                const bool valid = (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
                    (character >= '0' && character <= '9') || character == '-' || character == '_' || character == '.';
                if (!valid)
                    character = '_';
            }

            return value.empty() ? "server" : value;
        }

        std::filesystem::path GetWorkspaceRoot()
        {
            const char* connectIP = CVarSystem::Get()->GetStringCVar(CVarCategory::Network, "connectIP");
            const std::string serverKey = SanitizePathSegment(std::string(connectIP ? connectIP : "server") + "-4000");
            return std::filesystem::current_path() / "data" / "pact" / "data" / "temp" / serverKey;
        }

        std::filesystem::path GetWorkspaceFilePath()
        {
            return GetWorkspaceRoot() / "Novus.code-workspace";
        }

        std::filesystem::path ResolveLocalPath(std::string_view relativePath)
        {
            std::filesystem::path normalized = std::filesystem::path(relativePath).lexically_normal();
            if (normalized.is_absolute())
                return {};

            for (const auto& segment : normalized)
            {
                if (segment == "..")
                    return {};
            }

            return GetWorkspaceRoot() / normalized;
        }

        void ReplaceArgumentPlaceholder(std::string& arguments, std::string_view placeholder, const std::filesystem::path& path)
        {
            const std::string quotedPath = '"' + path.string() + '"';
            size_t offset = 0;
            while ((offset = arguments.find(placeholder, offset)) != std::string::npos)
            {
                arguments.replace(offset, placeholder.size(), quotedPath);
                offset += quotedPath.size();
            }
        }

        void MakeTreeWritable(const std::filesystem::path& root)
        {
            std::error_code error;
            if (!std::filesystem::exists(root, error) || error)
                return;

            std::filesystem::recursive_directory_iterator iterator(root, std::filesystem::directory_options::skip_permission_denied, error);
            const std::filesystem::recursive_directory_iterator end;
            for (; !error && iterator != end; iterator.increment(error))
            {
                std::filesystem::permissions(iterator->path(), std::filesystem::perms::owner_write, std::filesystem::perm_options::add, error);
                error.clear();
            }
            std::filesystem::permissions(root, std::filesystem::perms::owner_write, std::filesystem::perm_options::add, error);
        }

#if defined(_WIN32)
        std::filesystem::path FindVisualStudioCode()
        {
            const wchar_t* localAppData = _wgetenv(L"LOCALAPPDATA");
            if (localAppData)
            {
                const std::filesystem::path executable = std::filesystem::path(localAppData) / "Programs" / "Microsoft VS Code" / "Code.exe";
                std::error_code error;
                if (std::filesystem::is_regular_file(executable, error) && !error)
                    return executable;
            }

            const wchar_t* programFiles = _wgetenv(L"ProgramFiles");
            if (programFiles)
            {
                const std::filesystem::path executable = std::filesystem::path(programFiles) / "Microsoft VS Code" / "Code.exe";
                std::error_code error;
                if (std::filesystem::is_regular_file(executable, error) && !error)
                    return executable;
            }

            return {};
        }
#endif
    }

    void CreatureAIEditorBackend::SetStatus(std::string status)
    {
        _status = std::move(status);
        ++_changeVersion;
    }

    u32 CreatureAIEditorBackend::StartRequest(PendingRequest request)
    {
        u32 requestID = _nextRequestID++;
        if (requestID == 0)
            requestID = _nextRequestID++;
        _pendingRequests.emplace(requestID, std::move(request));
        return requestID;
    }

    void CreatureAIEditorBackend::FailRequest(u32 requestID, std::string status)
    {
        _pendingRequests.erase(requestID);
        SetStatus(std::move(status));
    }

    bool CreatureAIEditorBackend::SendPacket(const std::function<bool(Bytebuffer&)>& writePayload)
    {
        ECS::Singletons::NetworkState* networkState = GetNetworkState();
        if (!networkState || !networkState->client || !networkState->client->IsConnected() || !networkState->isInWorld)
        {
            SetStatus("Creature AI development actions require an active world connection.");
            return false;
        }

        std::shared_ptr<Bytebuffer> buffer = Bytebuffer::Borrow<4096>();
        bool wrotePayload = true;
        const bool created = ECS::Util::MessageBuilder::CreatePacket(buffer,
            MetaGen::Shared::Packet::ClientSendCheatCommandPacket::PACKET_ID, [&]()
        {
            wrotePayload = buffer->Put(MetaGen::Shared::Cheat::CheatCommandEnum::DevelopmentAction) && writePayload(*buffer);
        });
        if (!created || !wrotePayload)
        {
            SetStatus("The creature AI development request could not be encoded.");
            return false;
        }

        networkState->client->Send(buffer);
        return true;
    }

    bool CreatureAIEditorBackend::SendSimple(Operation operation, PendingRequest request)
    {
        request.operation = operation;
        const u32 requestID = StartRequest(std::move(request));
        const Resource resource = _pendingRequests.at(requestID).resource;
        if (!SendPacket([requestID, resource, operation](Bytebuffer& buffer)
        {
            return buffer.PutU32(requestID) && buffer.Put(resource) && buffer.Put(operation);
        }))
        {
            _pendingRequests.erase(requestID);
            return false;
        }

        return true;
    }

    bool CreatureAIEditorBackend::RequestWorkspace()
    {
        PendingRequest request;
        request.resource = Resource::ScriptWorkspace;
        request.transferPurpose = TransferPurpose::Workspace;
        _workspaceReady = false;
        SetStatus("Synchronizing the server script development workspace...");
        return SendSimple(Operation::Fetch, std::move(request));
    }

    bool CreatureAIEditorBackend::RequestCatalog()
    {
        return RequestCatalog(true);
    }

    bool CreatureAIEditorBackend::RequestCatalog(bool synchronizeWorkspace)
    {
        PendingRequest request;
        request.transferPurpose = TransferPurpose::Catalog;
        request.synchronizeWorkspace = synchronizeWorkspace;
        SetStatus("Requesting the creature AI script catalog...");
        return SendSimple(Operation::Catalog, std::move(request));
    }

    bool CreatureAIEditorBackend::InspectUnit(u32 unitID)
    {
        EnttRegistries* registries = ServiceLocator::GetEnttRegistries();
        ECS::Singletons::NetworkState* networkState = GetNetworkState();
        if (!registries || !registries->gameRegistry || !networkState)
            return false;

        const entt::entity entity = static_cast<entt::entity>(unitID);
        const auto guidItr = networkState->entityToNetworkID.find(entity);
        if (!registries->gameRegistry->valid(entity) || guidItr == networkState->entityToNetworkID.end())
        {
            SetStatus("The selected unit is not a networked creature.");
            return false;
        }

        PendingRequest request;
        request.operation = Operation::Inspect;
        const u32 requestID = StartRequest(std::move(request));
        _latestInspectionRequestID = requestID;
        if (!SendPacket([requestID, creatureGUID = guidItr->second](Bytebuffer& buffer)
        {
            return buffer.PutU32(requestID) && buffer.Put(Resource::CreatureAIScript) && buffer.Put(Operation::Inspect) &&
                buffer.Serialize(creatureGUID);
        }))
        {
            _pendingRequests.erase(requestID);
            return false;
        }

        SetStatus("Inspecting the selected creature's AI bindings...");
        return true;
    }

    void CreatureAIEditorBackend::ClearInspection()
    {
        _inspection.reset();
        _latestInspectionRequestID = 0;
        for (auto requestItr = _pendingRequests.begin(); requestItr != _pendingRequests.end();)
        {
            if (requestItr->second.operation == Operation::Inspect)
                requestItr = _pendingRequests.erase(requestItr);
            else
                ++requestItr;
        }
        SetStatus("No creature is currently inspected.");
    }

    bool CreatureAIEditorBackend::View(std::string scriptName)
    {
        if (!_workspaceReady)
        {
            SetStatus("Wait for the server script development workspace to finish synchronizing.");
            return false;
        }

        const auto checkoutItr = _checkouts.find(scriptName);
        if (checkoutItr != _checkouts.end())
        {
            const bool opened = OpenInIDE(checkoutItr->second.localPath);
            SetStatus(opened ? "Opened the existing editable checkout." : "The configured IDE could not be opened.");
            return opened;
        }

        PendingRequest request;
        request.operation = Operation::Fetch;
        request.transferPurpose = TransferPurpose::View;
        request.scriptName = std::move(scriptName);
        const u32 requestID = StartRequest(std::move(request));
        const std::string& name = _pendingRequests.at(requestID).scriptName;
        if (!SendPacket([requestID, name](Bytebuffer& buffer)
        {
            return buffer.PutU32(requestID) && buffer.Put(Resource::CreatureAIScript) && buffer.Put(Operation::Fetch) && buffer.PutString(name);
        }))
        {
            _pendingRequests.erase(requestID);
            return false;
        }
        SetStatus("Fetching " + name + " for viewing...");
        return true;
    }

    bool CreatureAIEditorBackend::Edit(std::string scriptName)
    {
        if (!_workspaceReady)
        {
            SetStatus("Wait for the server script development workspace to finish synchronizing.");
            return false;
        }

        PendingRequest request;
        request.operation = Operation::Checkout;
        request.transferPurpose = TransferPurpose::Checkout;
        request.scriptName = std::move(scriptName);
        const u32 requestID = StartRequest(std::move(request));
        const std::string& name = _pendingRequests.at(requestID).scriptName;
        if (!SendPacket([requestID, name](Bytebuffer& buffer)
        {
            return buffer.PutU32(requestID) && buffer.Put(Resource::CreatureAIScript) && buffer.Put(Operation::Checkout) && buffer.PutString(name);
        }))
        {
            _pendingRequests.erase(requestID);
            return false;
        }
        SetStatus("Checking out " + name + "...");
        return true;
    }

    bool CreatureAIEditorBackend::SendReserve(PendingRequest request)
    {
        request.operation = Operation::Reserve;
        const u32 requestID = StartRequest(std::move(request));
        const PendingRequest& stored = _pendingRequests.at(requestID);
        if (!SendPacket([requestID, name = stored.scriptName, path = stored.relativePath](Bytebuffer& buffer)
        {
            return buffer.PutU32(requestID) && buffer.Put(Resource::CreatureAIScript) && buffer.Put(Operation::Reserve) &&
                buffer.PutString(name) && buffer.PutString(path);
        }))
        {
            _pendingRequests.erase(requestID);
            return false;
        }
        SetStatus("Reserving " + stored.scriptName + "...");
        return true;
    }

    bool CreatureAIEditorBackend::Create(std::string scriptName, std::string relativePath, std::string source)
    {
        if (!_workspaceReady)
        {
            SetStatus("Wait for the server script development workspace to finish synchronizing.");
            return false;
        }
        if (source.size() > MAX_SCRIPT_SOURCE_SIZE)
        {
            SetStatus("The new creature AI script exceeds the transfer limit.");
            return false;
        }

        PendingRequest request;
        request.scriptName = std::move(scriptName);
        request.relativePath = std::move(relativePath);
        request.source.assign(source.begin(), source.end());
        return SendReserve(std::move(request));
    }

    bool CreatureAIEditorBackend::Duplicate(std::string sourceScriptName, std::string scriptName, std::string relativePath)
    {
        if (!_workspaceReady)
        {
            SetStatus("Wait for the server script development workspace to finish synchronizing.");
            return false;
        }

        PendingRequest request;
        request.operation = Operation::Fetch;
        request.transferPurpose = TransferPurpose::Duplicate;
        request.scriptName = std::move(sourceScriptName);
        request.duplicateName = std::move(scriptName);
        request.duplicatePath = std::move(relativePath);
        const u32 requestID = StartRequest(std::move(request));
        const std::string& sourceName = _pendingRequests.at(requestID).scriptName;
        if (!SendPacket([requestID, sourceName](Bytebuffer& buffer)
        {
            return buffer.PutU32(requestID) && buffer.Put(Resource::CreatureAIScript) && buffer.Put(Operation::Fetch) && buffer.PutString(sourceName);
        }))
        {
            _pendingRequests.erase(requestID);
            return false;
        }
        SetStatus("Fetching " + sourceName + " for duplication...");
        return true;
    }

    bool CreatureAIEditorBackend::FinishEditing(std::string scriptName)
    {
        const auto checkoutItr = _checkouts.find(scriptName);
        if (checkoutItr == _checkouts.end() || checkoutItr->second.leaseToken == 0)
        {
            SetStatus("This client does not hold an edit lease for " + scriptName + ".");
            return false;
        }

        Checkout& checkout = checkoutItr->second;
        checkout.releaseRequested = true;
        if (checkout.uploadPending)
        {
            SetStatus("Finishing " + scriptName + " after its pending upload completes...");
            return true;
        }

        std::error_code error;
        const auto writeTime = std::filesystem::last_write_time(checkout.localPath, error);
        if (!error && writeTime != checkout.lastWriteTime)
        {
            std::ifstream stream(checkout.localPath, std::ios::binary | std::ios::ate);
            std::streamsize size = -1;
            if (stream)
                size = static_cast<std::streamsize>(stream.tellg());
            if (size < 0 || static_cast<u64>(size) > MAX_SCRIPT_SOURCE_SIZE)
            {
                checkout.releaseRequested = false;
                SetStatus("The local script could not be read before releasing its edit lease.");
                return false;
            }

            stream.seekg(0);
            std::vector<u8> source(static_cast<size_t>(size));
            if (size > 0)
                stream.read(reinterpret_cast<char*>(source.data()), size);
            if (!stream && size > 0)
            {
                checkout.releaseRequested = false;
                SetStatus("The local script could not be read before releasing its edit lease.");
                return false;
            }

            checkout.lastWriteTime = writeTime;
            if (!SendUpload(checkout, source))
            {
                checkout.releaseRequested = false;
                return false;
            }
            SetStatus("Uploading the final save before releasing " + scriptName + "...");
            return true;
        }

        if (!SendLease(Operation::ReleaseLease, checkout))
        {
            checkout.releaseRequested = false;
            return false;
        }
        SetStatus("Releasing the edit lease for " + scriptName + "...");
        return true;
    }

    ObjectGUID CreatureAIEditorBackend::GetInspectedGUID() const
    {
        return _inspection ? _inspection->creatureGUID : ObjectGUID{};
    }

    bool CreatureAIEditorBackend::SendBinding(Operation operation, bool guidScope, std::string scriptName)
    {
        const ObjectGUID creatureGUID = GetInspectedGUID();
        if (!creatureGUID.IsValid())
        {
            SetStatus("Select and inspect a creature before changing a binding.");
            return false;
        }

        PendingRequest request;
        request.operation = operation;
        request.scriptName = std::move(scriptName);
        const u32 requestID = StartRequest(std::move(request));
        const std::string& name = _pendingRequests.at(requestID).scriptName;
        const BindingScope scope = guidScope ? BindingScope::Guid : BindingScope::Template;
        if (!SendPacket([requestID, operation, scope, creatureGUID, name](Bytebuffer& buffer)
        {
            if (!buffer.PutU32(requestID) || !buffer.Put(Resource::CreatureAIScript) || !buffer.Put(operation) || !buffer.Put(scope) || !buffer.Serialize(creatureGUID))
            {
                return false;
            }

            return operation != Operation::Link || buffer.PutString(name);
        }))
        {
            _pendingRequests.erase(requestID);
            return false;
        }
        SetStatus(operation == Operation::Link ? "Updating the creature AI binding..." : "Removing the creature AI binding...");
        return true;
    }

    bool CreatureAIEditorBackend::Link(bool guidScope, std::string scriptName)
    {
        return SendBinding(Operation::Link, guidScope, std::move(scriptName));
    }

    bool CreatureAIEditorBackend::Unlink(bool guidScope)
    {
        return SendBinding(Operation::Unlink, guidScope, {});
    }

    bool CreatureAIEditorBackend::SendLease(Operation operation, const Checkout& checkout)
    {
        PendingRequest request;
        request.operation = operation;
        request.scriptName = checkout.scriptName;
        request.leaseToken = checkout.leaseToken;
        const u32 requestID = StartRequest(std::move(request));
        if (!SendPacket([requestID, operation, name = checkout.scriptName, token = checkout.leaseToken](Bytebuffer& buffer)
        {
            return buffer.PutU32(requestID) && buffer.Put(Resource::CreatureAIScript) && buffer.Put(operation) &&
                buffer.PutString(name) && buffer.PutU64(token);
        }))
        {
            _pendingRequests.erase(requestID);
            return false;
        }

        return true;
    }

    bool CreatureAIEditorBackend::SendUpload(Checkout& checkout, const std::vector<u8>& source)
    {
        if (source.size() > MAX_SCRIPT_SOURCE_SIZE)
        {
            SetStatus("The edited creature AI script exceeds the transfer limit.");
            return false;
        }

        PendingRequest request;
        request.operation = Operation::Upload;
        request.scriptName = checkout.scriptName;
        request.relativePath = checkout.relativePath;
        request.leaseToken = checkout.leaseToken;
        request.revision = checkout.revision;
        const u32 requestID = StartRequest(std::move(request));

        if (!SendPacket([&](Bytebuffer& buffer)
        {
            return buffer.PutU32(requestID) && buffer.Put(Resource::CreatureAIScript) && buffer.Put(Operation::Upload) &&
                buffer.Put(UploadPhase::Begin) && buffer.PutString(checkout.scriptName) && buffer.PutU64(checkout.leaseToken) &&
                buffer.PutU64(checkout.revision) && buffer.PutU32(static_cast<u32>(source.size()));
        }))
        {
            _pendingRequests.erase(requestID);
            return false;
        }

        for (u32 offset = 0; offset < source.size(); offset += TRANSFER_CHUNK_SIZE)
        {
            const u16 size = static_cast<u16>(std::min<size_t>(TRANSFER_CHUNK_SIZE, source.size() - offset));
            if (!SendPacket([&, offset, size](Bytebuffer& buffer)
            {
                return buffer.PutU32(requestID) && buffer.Put(Resource::CreatureAIScript) && buffer.Put(Operation::Upload) &&
                    buffer.Put(UploadPhase::Chunk) && buffer.PutU32(offset) && buffer.PutU16(size) && buffer.PutBytes(source.data() + offset, size);
            }))
            {
                _pendingRequests.erase(requestID);
                return false;
            }
        }

        if (!SendPacket([requestID](Bytebuffer& buffer)
        {
            return buffer.PutU32(requestID) && buffer.Put(Resource::CreatureAIScript) && buffer.Put(Operation::Upload) && buffer.Put(UploadPhase::Commit);
        }))
        {
            _pendingRequests.erase(requestID);
            return false;
        }

        checkout.uploadPending = true;
        SetStatus("Uploading " + checkout.scriptName + "...");
        return true;
    }

    void CreatureAIEditorBackend::HandleActionResult(MetaGen::Shared::Packet::ServerDevelopmentActionResultPacket& packet)
    {
        const auto requestItr = _pendingRequests.find(packet.requestID);
        if (requestItr == _pendingRequests.end())
            return;

        PendingRequest& request = requestItr->second;
        const Operation operation = static_cast<Operation>(packet.operation);
        if (packet.resource != static_cast<u8>(request.resource) || operation != request.operation)
        {
            FailRequest(packet.requestID, "The server returned a mismatched script development response.");
            return;
        }
        if (packet.result != static_cast<u8>(Result::Success))
        {
            if (operation == Operation::Upload)
            {
                const auto checkoutItr = _checkouts.find(request.scriptName);
                if (checkoutItr != _checkouts.end())
                {
                    checkoutItr->second.uploadPending = false;
                    checkoutItr->second.releaseRequested = false;
                }
            }
            else if (operation == Operation::RenewLease)
            {
                const auto checkoutItr = _checkouts.find(request.scriptName);
                if (checkoutItr != _checkouts.end())
                    checkoutItr->second.leaseToken = 0;
            }
            else if (operation == Operation::ReleaseLease)
            {
                const auto checkoutItr = _checkouts.find(request.scriptName);
                if (checkoutItr != _checkouts.end())
                    checkoutItr->second.releaseRequested = false;
            }
            FailRequest(packet.requestID, packet.response.empty() ? "The creature AI development action failed." : std::move(packet.response));
            return;
        }

        request.relativePath = packet.relativePath.empty() ? request.relativePath : packet.relativePath;
        request.leaseToken = packet.leaseToken;
        request.leaseDurationMilliseconds = packet.leaseDurationMilliseconds;
        request.revision = packet.revision;

        if (operation == Operation::Catalog || operation == Operation::Fetch || operation == Operation::Checkout || operation == Operation::Inspect)
        {
            SetStatus(packet.response);
            return;
        }

        if (operation == Operation::Reserve)
        {
            Checkout checkout;
            checkout.scriptName = request.scriptName;
            checkout.relativePath = request.relativePath;
            checkout.leaseToken = request.leaseToken;
            checkout.revision = 0;
            checkout.nextLeaseRenewal = std::chrono::steady_clock::now() + std::chrono::milliseconds(request.leaseDurationMilliseconds / 2);
            std::vector<u8> source = std::move(request.source);
            PendingRequest localSourceRequest = request;
            localSourceRequest.source.clear();
            _pendingRequests.erase(requestItr);
            auto [checkoutItr, inserted] = _checkouts.insert_or_assign(checkout.scriptName, std::move(checkout));
            (void)inserted;
            if (WriteLocalSource(localSourceRequest, source, true, checkoutItr->second.localPath))
            {
                checkoutItr->second.lastWriteTime = std::filesystem::last_write_time(checkoutItr->second.localPath);
                (void)OpenInIDE(checkoutItr->second.localPath);
            }
            if (!SendUpload(checkoutItr->second, source))
                SetStatus("The script name was reserved, but its initial source could not be uploaded.");
            return;
        }

        if (operation == Operation::Upload)
        {
            const auto checkoutItr = _checkouts.find(request.scriptName);
            bool releaseRequested = false;
            if (checkoutItr != _checkouts.end())
            {
                checkoutItr->second.revision = packet.revision;
                checkoutItr->second.uploadPending = false;
                releaseRequested = checkoutItr->second.releaseRequested;
            }
            _pendingRequests.erase(requestItr);
            SetStatus(packet.response);
            if (releaseRequested && checkoutItr != _checkouts.end())
            {
                if (!SendLease(Operation::ReleaseLease, checkoutItr->second))
                    checkoutItr->second.releaseRequested = false;
                else
                    SetStatus("Final save uploaded; releasing the edit lease for " + checkoutItr->second.scriptName + "...");
                return;
            }
            RequestCatalog(false);
            return;
        }

        if (operation == Operation::RenewLease)
        {
            const auto checkoutItr = _checkouts.find(request.scriptName);
            if (checkoutItr != _checkouts.end())
            {
                checkoutItr->second.nextLeaseRenewal = std::chrono::steady_clock::now() +
                    std::chrono::milliseconds(packet.leaseDurationMilliseconds / 2);
            }
            _pendingRequests.erase(requestItr);
            return;
        }

        if (operation == Operation::ReleaseLease)
        {
            const auto checkoutItr = _checkouts.find(request.scriptName);
            bool removedLocalFile = true;
            if (checkoutItr != _checkouts.end())
            {
                std::error_code error;
                std::filesystem::permissions(checkoutItr->second.localPath, std::filesystem::perms::owner_write, std::filesystem::perm_options::add, error);
                error.clear();
                const bool localFileExists = std::filesystem::exists(checkoutItr->second.localPath, error);
                removedLocalFile = !error && (!localFileExists || std::filesystem::remove(checkoutItr->second.localPath, error)) && !error;
                _checkouts.erase(checkoutItr);
            }
            _pendingRequests.erase(requestItr);
            SetStatus(removedLocalFile ? "Edit lease released and the local editable script was removed." : "Edit lease released, but the local editable script could not be removed.");
            RequestCatalog(false);
            return;
        }

        _pendingRequests.erase(requestItr);
        SetStatus(packet.response);
        if (operation == Operation::Link || operation == Operation::Unlink)
        {
            const ObjectGUID guid = GetInspectedGUID();
            if (guid.IsValid())
            {
                PendingRequest inspectRequest;
                inspectRequest.operation = Operation::Inspect;
                const u32 inspectRequestID = StartRequest(std::move(inspectRequest));
                _latestInspectionRequestID = inspectRequestID;
                if (!SendPacket([inspectRequestID, guid](Bytebuffer& buffer)
                {
                    return buffer.PutU32(inspectRequestID) && buffer.Put(Resource::CreatureAIScript) && buffer.Put(Operation::Inspect) && buffer.Serialize(guid);
                }))
                {
                    _pendingRequests.erase(inspectRequestID);
                }
            }
        }
    }

    bool CreatureAIEditorBackend::BeginTransfer(u32 requestID, u32 totalSize, u64 revision)
    {
        const auto requestItr = _pendingRequests.find(requestID);
        if (requestItr == _pendingRequests.end() || _incomingTransfer)
            return false;

        u32 maximumSize = MAX_SCRIPT_SOURCE_SIZE;
        if (requestItr->second.transferPurpose == TransferPurpose::Catalog)
            maximumSize = MAX_CATALOG_SIZE;
        else if (requestItr->second.transferPurpose == TransferPurpose::Workspace)
            maximumSize = MAX_WORKSPACE_SIZE;
        if (totalSize > maximumSize)
            return false;

        _incomingTransfer.emplace();
        _incomingTransfer->requestID = requestID;
        _incomingTransfer->revision = revision;
        _incomingTransfer->bytes.resize(totalSize);
        return true;
    }

    bool CreatureAIEditorBackend::AppendTransfer(u32 requestID, u32 offset, const u8* bytes, u16 size)
    {
        if (!_incomingTransfer || _incomingTransfer->requestID != requestID || size == 0 || offset != _incomingTransfer->receivedBytes || offset + size > _incomingTransfer->bytes.size())
        {
            return false;
        }

        std::copy_n(bytes, size, _incomingTransfer->bytes.data() + offset);
        _incomingTransfer->receivedBytes += size;
        return true;
    }

    bool CreatureAIEditorBackend::CompleteTransfer(u32 requestID, bool succeeded)
    {
        if (!_incomingTransfer || _incomingTransfer->requestID != requestID)
            return false;

        const bool complete = succeeded && _incomingTransfer->receivedBytes == _incomingTransfer->bytes.size();
        std::vector<u8> bytes = std::move(_incomingTransfer->bytes);
        _incomingTransfer.reset();
        const auto requestItr = _pendingRequests.find(requestID);
        if (!complete || requestItr == _pendingRequests.end())
        {
            FailRequest(requestID, "The script development transfer was incomplete.");
            return true;
        }

        PendingRequest request = std::move(requestItr->second);
        _pendingRequests.erase(requestItr);
        FinishTransfer(requestID, std::move(request), std::move(bytes));
        return true;
    }

    bool CreatureAIEditorBackend::ParseCatalog(const std::vector<u8>& bytes)
    {
        Bytebuffer buffer = Bytebuffer::CreateReadOnlyView(bytes.data(), bytes.size());
        u32 count = 0;
        if (!buffer.GetU32(count) || count > 100000)
            return false;

        std::vector<CreatureAIScriptCatalogEntry> entries;
        entries.reserve(count);
        for (u32 index = 0; index < count; ++index)
        {
            CreatureAIScriptCatalogEntry entry;
            u8 sourceAvailable = 0;
            u8 locked = 0;
            u8 lockedByRequester = 0;
            if (!buffer.GetString(entry.name) || !buffer.GetString(entry.relativePath) || !buffer.GetU64(entry.revision) || !buffer.GetU8(sourceAvailable) || !buffer.GetU8(locked) || !buffer.GetU8(lockedByRequester) || sourceAvailable > 1 || locked > 1 || lockedByRequester > 1)
            {
                return false;
            }
            entry.sourceAvailable = sourceAvailable == 1;
            entry.locked = locked == 1;
            entry.lockedByRequester = lockedByRequester == 1;
            entries.push_back(std::move(entry));
        }

        if (buffer.GetReadSpace() != 0)
            return false;

        _catalog = std::move(entries);
        ++_changeVersion;
        return true;
    }

    bool CreatureAIEditorBackend::WriteWorkspace(const std::vector<u8>& bytes)
    {
        struct WorkspaceFile
        {
        public:
            std::filesystem::path relativePath;
            std::vector<u8> bytes;
        };

        Bytebuffer buffer = Bytebuffer::CreateReadOnlyView(bytes.data(), bytes.size());
        u32 fileCount = 0;
        if (!buffer.GetU32(fileCount) || fileCount < 2 || fileCount > MAX_WORKSPACE_FILE_COUNT)
            return false;

        std::vector<WorkspaceFile> files;
        files.reserve(fileCount);
        std::unordered_set<std::string> relativePaths;
        bool hasLuaurc = false;
        bool hasTypes = false;
        for (u32 index = 0; index < fileCount; ++index)
        {
            std::string relativePathString;
            u32 fileSize = 0;
            if (!buffer.GetString(relativePathString) || !buffer.GetU32(fileSize) || fileSize > buffer.GetReadSpace())
                return false;

            const std::filesystem::path relativePath = std::filesystem::path(relativePathString).lexically_normal();
            if (relativePath.empty() || relativePath.is_absolute() || !relativePath.has_filename() || relativePath.generic_string() != relativePathString || relativePathString.find(':') != std::string::npos)
                return false;

            for (const std::filesystem::path& segment : relativePath)
            {
                if (segment == "..")
                    return false;
            }

            const bool isLuaurc = relativePath == ".luaurc";
            const bool isTypes = relativePath == "Types.def";
            const bool isAPIFile = relativePath.begin() != relativePath.end() && *relativePath.begin() == "API" && std::next(relativePath.begin()) != relativePath.end();
            if ((!isLuaurc && !isTypes && !isAPIFile) || !relativePaths.insert(relativePathString).second)
                return false;

            WorkspaceFile& file = files.emplace_back();
            file.relativePath = relativePath;
            file.bytes.resize(fileSize);
            if (fileSize != 0 && !buffer.GetBytes(file.bytes.data(), fileSize))
                return false;
            hasLuaurc = hasLuaurc || isLuaurc;
            hasTypes = hasTypes || isTypes;
        }
        if (!hasLuaurc || !hasTypes || buffer.GetReadSpace() != 0)
            return false;

        const std::filesystem::path workspaceRoot = GetWorkspaceRoot();
        std::error_code error;
        std::filesystem::create_directories(workspaceRoot / "Temp" / "AI", error);
        if (error)
            return false;

        for (const std::filesystem::path& replacePath : { workspaceRoot / ".luaurc", workspaceRoot / "Types.def" })
        {
            std::filesystem::permissions(replacePath, std::filesystem::perms::owner_write, std::filesystem::perm_options::add, error);
            error.clear();
        }
        MakeTreeWritable(workspaceRoot / "API");
        std::filesystem::remove_all(workspaceRoot / "API", error);
        if (error)
            return false;

        for (const WorkspaceFile& file : files)
        {
            const std::filesystem::path destination = workspaceRoot / file.relativePath;
            std::filesystem::create_directories(destination.parent_path(), error);
            if (error)
                return false;

            std::ofstream stream(destination, std::ios::binary | std::ios::trunc);
            if (!stream)
                return false;
            if (!file.bytes.empty())
                stream.write(reinterpret_cast<const char*>(file.bytes.data()), static_cast<std::streamsize>(file.bytes.size()));
            stream.close();
            if (!stream)
                return false;

            std::filesystem::permissions(destination, std::filesystem::perms::owner_write | std::filesystem::perms::group_write | std::filesystem::perms::others_write, std::filesystem::perm_options::remove, error);
            error.clear();
        }

        const std::filesystem::path workspaceFile = GetWorkspaceFilePath();
        std::ofstream stream(workspaceFile, std::ios::binary | std::ios::trunc);
        if (!stream)
            return false;
        stream << "{\n"
            "    \"folders\": [\n"
            "        { \"path\": \".\" }\n"
            "    ],\n"
            "    \"settings\": {\n"
            "        \"luau-lsp.platform.type\": \"standard\",\n"
            "        \"luau-lsp.sourcemap.enabled\": false,\n"
            "        \"luau-lsp.sourcemap.autogenerate\": false,\n"
            "        \"luau-lsp.fflags.sync\": false,\n"
            "        \"luau-lsp.fflags.override\": {\n"
            "            \"LuauIntegerType2\": \"true\",\n"
            "            \"LuauIntegerLibrary\": \"true\"\n"
            "        },\n"
            "        \"luau-lsp.types.definitionFiles\": {\n"
            "            \"novus\": \"Types.def\"\n"
            "        }\n"
            "    },\n"
            "    \"extensions\": {\n"
            "        \"recommendations\": [\"JohnnyMorganz.luau-lsp\"]\n"
            "    }\n"
            "}\n";
        return static_cast<bool>(stream);
    }

    bool CreatureAIEditorBackend::WriteLocalSource(const PendingRequest& request, const std::vector<u8>& bytes, bool editable, std::filesystem::path& outPath)
    {
        outPath = ResolveLocalPath((std::filesystem::path("Temp") / request.relativePath).generic_string());
        if (outPath.empty())
            return false;

        std::error_code error;
        std::filesystem::create_directories(outPath.parent_path(), error);
        if (error)
            return false;

        std::filesystem::permissions(outPath, std::filesystem::perms::owner_write, std::filesystem::perm_options::add, error);
        error.clear();
        std::ofstream stream(outPath, std::ios::binary | std::ios::trunc);
        if (!stream)
            return false;
        if (!bytes.empty())
            stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        stream.close();
        if (!stream)
            return false;

        if (!editable)
        {
            std::filesystem::permissions(outPath, std::filesystem::perms::owner_write | std::filesystem::perms::group_write | std::filesystem::perms::others_write, std::filesystem::perm_options::remove, error);
        }

        return true;
    }

    bool CreatureAIEditorBackend::OpenInIDE(const std::filesystem::path& path)
    {
#if defined(_WIN32)
        const std::string configuredExecutable = CVAR_CreatureAIEditorIDE.Get();
        const std::filesystem::path executable = configuredExecutable.empty() ? FindVisualStudioCode() : std::filesystem::path(configuredExecutable);
        if (executable.empty())
            return false;

        std::string arguments = CVAR_CreatureAIEditorIDEArguments.Get();
        const bool hasWorkspacePlaceholder = arguments.find("{workspace}") != std::string::npos;
        const bool hasFilePlaceholder = arguments.find("{file}") != std::string::npos;
        ReplaceArgumentPlaceholder(arguments, "{workspace}", GetWorkspaceFilePath());
        ReplaceArgumentPlaceholder(arguments, "{file}", path);
        if (!hasWorkspacePlaceholder)
            arguments.insert(0, "--new-window \"" + GetWorkspaceFilePath().string() + "\" ");
        if (!hasFilePlaceholder)
            arguments.append(" \"").append(path.string()).append("\"");

        const std::wstring argumentsWide(arguments.begin(), arguments.end());
        return reinterpret_cast<std::intptr_t>(ShellExecuteW(nullptr, L"open", executable.c_str(), argumentsWide.c_str(), GetWorkspaceRoot().c_str(), SW_SHOWNORMAL)) > 32;
#else
        (void)path;
        return false;
#endif
    }

    void CreatureAIEditorBackend::FinishTransfer(u32, PendingRequest request, std::vector<u8> bytes)
    {
        if (request.transferPurpose == TransferPurpose::Catalog)
        {
            if (!ParseCatalog(bytes))
            {
                SetStatus("The server returned an invalid creature AI catalog.");
                return;
            }
            if (request.synchronizeWorkspace)
            {
                if (!RequestWorkspace())
                    SetStatus("The creature AI catalog was refreshed, but the development workspace could not be requested.");
            }
            else
            {
                SetStatus("Creature AI script catalog refreshed.");
            }
            return;
        }

        if (request.transferPurpose == TransferPurpose::Workspace)
        {
            _workspaceReady = WriteWorkspace(bytes);
            SetStatus(_workspaceReady ? "Creature AI catalog and script development workspace refreshed." : "The server returned an invalid script development workspace.");
            return;
        }

        if (request.transferPurpose == TransferPurpose::Duplicate)
        {
            PendingRequest creation;
            creation.scriptName = std::move(request.duplicateName);
            creation.relativePath = std::move(request.duplicatePath);
            creation.source = std::move(bytes);
            SendReserve(std::move(creation));
            return;
        }

        const bool editable = request.transferPurpose == TransferPurpose::Checkout;
        std::filesystem::path localPath;
        if (!WriteLocalSource(request, bytes, editable, localPath))
        {
            SetStatus("The creature AI source could not be written to the local Temp workspace.");
            return;
        }

        if (editable)
        {
            Checkout checkout;
            checkout.scriptName = request.scriptName;
            checkout.relativePath = request.relativePath;
            checkout.localPath = localPath;
            checkout.leaseToken = request.leaseToken;
            checkout.revision = request.revision;
            checkout.lastWriteTime = std::filesystem::last_write_time(localPath);
            checkout.nextLeaseRenewal = std::chrono::steady_clock::now() + std::chrono::milliseconds(request.leaseDurationMilliseconds / 2);
            _checkouts.insert_or_assign(checkout.scriptName, std::move(checkout));
        }

        if (!OpenInIDE(localPath))
            SetStatus("Source fetched to " + localPath.string() + ", but the configured IDE could not be opened.");
        else
            SetStatus(editable ? "Script checked out. Saving the local file will upload and reload it." : "Script opened read-only.");
    }

    void CreatureAIEditorBackend::HandleInspection(MetaGen::Shared::Packet::ServerCreatureAIDevelopmentInfoPacket& packet)
    {
        const auto requestItr = _pendingRequests.find(packet.requestID);
        if (requestItr == _pendingRequests.end() || requestItr->second.operation != Operation::Inspect)
            return;
        if (packet.requestID != _latestInspectionRequestID)
        {
            _pendingRequests.erase(requestItr);
            return;
        }

        const BindingScope effectiveScope = static_cast<BindingScope>(packet.effectiveScope);
        if (effectiveScope != BindingScope::None && effectiveScope != BindingScope::Guid && effectiveScope != BindingScope::Template)
        {
            FailRequest(packet.requestID, "The server returned an invalid creature AI binding scope.");
            return;
        }

        _inspection = CreatureAIInspection{
            .creatureGUID = packet.creatureGUID,
            .creatureTemplateID = packet.creatureTemplateID,
            .guidScriptName = std::move(packet.guidScriptName),
            .templateScriptName = std::move(packet.templateScriptName),
            .effectiveScriptName = std::move(packet.effectiveScriptName),
            .effectiveScope = effectiveScope
        };
        _pendingRequests.erase(requestItr);
        SetStatus("Creature AI bindings refreshed.");
    }

    void CreatureAIEditorBackend::Update()
    {
        const auto now = std::chrono::steady_clock::now();
        for (auto& [name, checkout] : _checkouts)
        {
            (void)name;
            if (checkout.leaseToken != 0 && now >= checkout.nextLeaseRenewal)
            {
                checkout.nextLeaseRenewal = now + std::chrono::minutes(1);
                SendLease(Operation::RenewLease, checkout);
            }
        }

        if (now < _nextFilePoll)
            return;
        _nextFilePoll = now + FILE_POLL_INTERVAL;

        for (auto& [name, checkout] : _checkouts)
        {
            (void)name;
            if (checkout.localPath.empty() || checkout.uploadPending || checkout.releaseRequested)
                continue;

            std::error_code error;
            const auto writeTime = std::filesystem::last_write_time(checkout.localPath, error);
            if (error || writeTime == checkout.lastWriteTime)
                continue;

            checkout.lastWriteTime = writeTime;
            std::ifstream stream(checkout.localPath, std::ios::binary | std::ios::ate);
            if (!stream)
            {
                SetStatus("The edited creature AI script could not be read for upload.");
                continue;
            }
            const std::streamsize size = stream.tellg();
            if (size < 0 || static_cast<u64>(size) > MAX_SCRIPT_SOURCE_SIZE)
            {
                SetStatus("The edited creature AI script exceeds the transfer limit.");
                continue;
            }
            stream.seekg(0);
            std::vector<u8> source(static_cast<size_t>(size));
            if (size > 0)
                stream.read(reinterpret_cast<char*>(source.data()), size);
            if (!stream && size > 0)
            {
                SetStatus("The edited creature AI script could not be read for upload.");
                continue;
            }
            SendUpload(checkout, source);
        }
    }

    void CreatureAIEditorBackend::Reset()
    {
        _catalog.clear();
        _inspection.reset();
        _pendingRequests.clear();
        _incomingTransfer.reset();
        _checkouts.clear();
        _latestInspectionRequestID = 0;
        _workspaceReady = false;
        SetStatus("Creature AI editor disconnected.");
    }
}

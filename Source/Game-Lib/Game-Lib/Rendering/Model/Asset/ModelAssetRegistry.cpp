#include "ModelAssetRegistry.h"

#include "FallbackModel.h"
#include "ModelGeometryStorage.h"

#include "Game-Lib/Rendering/Asset/AssetDiagnostic.h"
#include "Game-Lib/Rendering/Asset/AssetValidation.h"
#include "Game-Lib/Rendering/Material/MaterialRegistry.h"
#include "Game-Lib/Rendering/Material/MaterialStorage.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <Base/Container/ConcurrentQueue.h>
#include <Base/CVarSystem/CVarSystem.h>
#include <Base/Util/DebugHandler.h>

#include <Filesystem/PactStorage.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <enkiTS/TaskScheduler.h>

#include <string>
#include <vector>

namespace
{
    const char* ToReason(PACT::PactReadResult result)
    {
        switch (result)
        {
        case PACT::PactReadResult::FileNotFound:
            return "file_not_found";
        case PACT::PactReadResult::FileAccessFailed:
            return "file_access_failed";
        case PACT::PactReadResult::FileReadFailed:
            return "file_read_failed";
        case PACT::PactReadResult::GenerationMismatch:
            return "generation_mismatch";
        case PACT::PactReadResult::Pending:
            return "unexpected_pending_read";
        default:
            return "pact_read_failed";
        }
    }
} // namespace

AutoCVar_Int CVAR_ModelAssetMaxInFlight(CVarCategory::Client | CVarCategory::Rendering,
    "modelAssetMaxInFlight", "maximum concurrent Model V2 asset read/decode jobs", 8, CVarFlags::None);

namespace ModelLoading
{
    struct ModelAssetRegistry::AsyncState
    {
        struct Result
        {
            PACT::PactReadResult readResult = PACT::PactReadResult::Failed;
            PACT::PactFileHandle file;
            ModelAssetReadResult decoded;
        };

        struct Completion
        {
            FileFormat::AssetID assetID = FileFormat::INVALID_ASSET_ID;
            u64 epoch = 0;
            Result result;
        };

        enum class State : u8
        {
            Queued,
            Running,
            Complete
        };

        struct Job;

        struct PendingLoad
        {
            Result result;
            Job* job = nullptr;
            u64 epoch = 0;
            u32 referenceCount = 1;
            State state = State::Queued;
        };

        struct Job : enki::ITaskSet
        {
            Job(FileFormat::AssetID assetID, u64 epoch, PACT::PactStorage* storage,
                moodycamel::ConcurrentQueue<Completion>* completions)
                : enki::ITaskSet(1), assetID(assetID), epoch(epoch), storage(storage), completions(completions)
            {
                m_Priority = enki::TASK_PRIORITY_LOW;
            }

            void ExecuteRange(enki::TaskSetPartition range, u32 threadNum) override
            {
                (void)range;
                (void)threadNum;
                ZoneScopedN("Model V2 Asset Read");

                Completion completion;
                completion.assetID = assetID;
                completion.epoch = epoch;
                completion.result.readResult = storage->ReadFile(assetID, completion.result.file);
                if (completion.result.readResult == PACT::PactReadResult::Success)
                {
                    const std::span<const u8> payload(
                        reinterpret_cast<const u8*>(completion.result.file.GetData()),
                        completion.result.file.GetSize());
                    completion.result.decoded = ModelAssetReader::Read(payload);
                }
                completions->enqueue(std::move(completion));
            }

            FileFormat::AssetID assetID = FileFormat::INVALID_ASSET_ID;
            u64 epoch = 0;
            PACT::PactStorage* storage = nullptr;
            moodycamel::ConcurrentQueue<Completion>* completions = nullptr;
        };

        robin_hood::unordered_map<FileFormat::AssetID, PendingLoad> pendingLoads;
        moodycamel::ConcurrentQueue<Completion> completions;
        std::vector<std::unique_ptr<Job>> activeJobs;
        u64 epoch = 1;
        u32 peakActiveJobs = 0;
        u32 peakPendingLoads = 0;
        u32 coalescedRequests = 0;
        u32 completedReads = 0;
        u32 committedLoads = 0;
        u32 cancelledLoads = 0;
        u64 decodedBytes = 0;
        u64 peakDecodedBytes = 0;
        u64 totalCommitTimeUS = 0;
        u64 maxCommitTimeUS = 0;
    };

    ModelAssetRegistry::ModelAssetRegistry(PACT::PactStorage* pactStorage, ModelGeometryStorage* geometryStorage,
                                           MaterialLoading::MaterialStorage* materialStorage,
                                           MaterialLoading::MaterialRegistry* materialRegistry)
        : _pactStorage(pactStorage), _geometryStorage(geometryStorage), _materialStorage(materialStorage),
          _materialRegistry(materialRegistry), _async(std::make_unique<AsyncState>())
    {
    }

    ModelAssetRegistry::~ModelAssetRegistry()
    {
        CancelAndDrainLoads();
    }

    ModelLoadStatus ModelAssetRegistry::BeginLoad(FileFormat::AssetID assetID, RenderAssets::ModelHandle& outHandle)
    {
        PumpCompletedLoads();
        const auto resident = _entries.find(assetID);
        if (resident != _entries.end())
        {
            ++resident->second.referenceCount;
            ++_cacheHits;
            outHandle = resident->second.handle;
            return ModelLoadStatus::Ready;
        }

        const auto pending = _async->pendingLoads.find(assetID);
        if (pending != _async->pendingLoads.end())
        {
            ++pending->second.referenceCount;
            ++_async->coalescedRequests;
            if (CommitPendingLoad(assetID, outHandle))
                return ModelLoadStatus::Ready;
            outHandle = RenderAssets::ModelHandle::Invalid();
            return ModelLoadStatus::Pending;
        }

        if (assetID == FileFormat::INVALID_ASSET_ID || AssetLoading::ShouldInjectFailure(AssetLoading::FailureInjection::Model))
        {
            outHandle = RecordFailure(assetID, assetID == FileFormat::INVALID_ASSET_ID ? "invalid_asset_id" : "injected_failure");
            return ModelLoadStatus::Ready;
        }

        AsyncState::PendingLoad load;
        load.epoch = _async->epoch;
        _async->pendingLoads.emplace(assetID, std::move(load));
        _async->peakPendingLoads = std::max(_async->peakPendingLoads,
                                            static_cast<u32>(_async->pendingLoads.size()));
        DispatchQueuedLoads();
        outHandle = RenderAssets::ModelHandle::Invalid();
        return ModelLoadStatus::Pending;
    }

    ModelLoadStatus ModelAssetRegistry::PollLoad(FileFormat::AssetID assetID, RenderAssets::ModelHandle& outHandle)
    {
        PumpCompletedLoads();
        const auto resident = _entries.find(assetID);
        if (resident != _entries.end())
        {
            outHandle = resident->second.handle;
            return ModelLoadStatus::Ready;
        }

        const auto pending = _async->pendingLoads.find(assetID);
        if (pending == _async->pendingLoads.end())
        {
            outHandle = _fallbackModel;
            return ModelLoadStatus::Ready;
        }
        if (!CommitPendingLoad(assetID, outHandle))
            return ModelLoadStatus::Pending;
        return ModelLoadStatus::Ready;
    }

    void ModelAssetRegistry::Update()
    {
        PumpCompletedLoads();
        DispatchQueuedLoads();
    }

    void ModelAssetRegistry::PumpCompletedLoads()
    {
        AsyncState::Completion completion;
        while (_async->completions.try_dequeue(completion))
        {
            const auto pending = _async->pendingLoads.find(completion.assetID);
            if (pending == _async->pendingLoads.end() || pending->second.epoch != completion.epoch)
            {
                ++_async->cancelledLoads;
                continue;
            }
            if (pending->second.referenceCount == 0)
            {
                ++_async->cancelledLoads;
                _async->pendingLoads.erase(pending);
                continue;
            }
            ++_async->completedReads;
            const u64 decodedBytes = completion.result.readResult == PACT::PactReadResult::Success ?
                completion.result.file.GetSize() : 0;
            _async->decodedBytes += decodedBytes;
            _async->peakDecodedBytes = std::max(_async->peakDecodedBytes, _async->decodedBytes);
            pending->second.result = std::move(completion.result);
            pending->second.state = AsyncState::State::Complete;
        }

        std::erase_if(_async->activeJobs, [this](const std::unique_ptr<AsyncState::Job>& job) {
            const auto pending = _async->pendingLoads.find(job->assetID);
            return job->GetIsComplete() &&
                (pending == _async->pendingLoads.end() || pending->second.state == AsyncState::State::Complete);
        });
        DispatchQueuedLoads();
    }

    void ModelAssetRegistry::DispatchQueuedLoads()
    {
        enki::TaskScheduler* taskScheduler = ServiceLocator::GetTaskScheduler();
        const size_t maxInFlight = static_cast<size_t>(std::max(CVAR_ModelAssetMaxInFlight.Get(), 1));
        for (auto& [assetID, pending] : _async->pendingLoads)
        {
            if (_async->activeJobs.size() >= maxInFlight)
                break;
            if (pending.state != AsyncState::State::Queued)
                continue;

            auto job = std::make_unique<AsyncState::Job>(assetID, pending.epoch, _pactStorage,
                                                         &_async->completions);
            pending.job = job.get();
            pending.state = AsyncState::State::Running;
            taskScheduler->AddTaskSetToPipe(job.get());
            _async->activeJobs.push_back(std::move(job));
            _async->peakActiveJobs = std::max(_async->peakActiveJobs,
                                              static_cast<u32>(_async->activeJobs.size()));
        }
    }

    bool ModelAssetRegistry::CommitPendingLoad(FileFormat::AssetID assetID,
                                                RenderAssets::ModelHandle& outHandle)
    {
        const auto pending = _async->pendingLoads.find(assetID);
        if (pending == _async->pendingLoads.end() || pending->second.state != AsyncState::State::Complete)
            return false;

        const u32 references = pending->second.referenceCount;
        const u64 decodedBytes = pending->second.result.readResult == PACT::PactReadResult::Success ?
            pending->second.result.file.GetSize() : 0;
        AsyncState::Result result = std::move(pending->second.result);
        _async->pendingLoads.erase(pending);
        _async->decodedBytes -= std::min(_async->decodedBytes, decodedBytes);
        const auto commitStart = std::chrono::steady_clock::now();
        if (result.readResult != PACT::PactReadResult::Success)
        {
            outHandle = RecordFailure(assetID, ToReason(result.readResult),
                                      result.readResult == PACT::PactReadResult::FileNotFound, references);
        }
        else if (!result.decoded)
        {
            const std::string reason = AssetLoading::Describe(result.decoded.diagnostic);
            outHandle = RecordFailure(assetID, reason.c_str(), false, references);
        }
        else
        {
            u32 materialTableOffset = 0;
            if (!_materialRegistry->AppendDefaultMaterialTable(result.decoded.view.materialSlots,
                                                                materialTableOffset) ||
                !_geometryStorage->Append(result.decoded.view, materialTableOffset,
                    static_cast<u32>(result.decoded.view.materialSlots.size()), outHandle))
            {
                outHandle = RecordFailure(assetID, "commit_failed", false, references);
            }
            else
            {
                _limitations.invalidSkeletonReferences += result.decoded.limitations.invalidSkeletonReferences;
                _limitations.invalidAnimationBoundsReferences +=
                    result.decoded.limitations.invalidAnimationBoundsReferences;
                _limitations.invalidEmbeddedModelReferences +=
                    result.decoded.limitations.invalidEmbeddedModelReferences;
                _entries.emplace(assetID, Entry{outHandle, references, false, false});
            }
        }
        const u64 commitTimeUS = static_cast<u64>(std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - commitStart).count());
        ++_async->committedLoads;
        _async->totalCommitTimeUS += commitTimeUS;
        _async->maxCommitTimeUS = std::max(_async->maxCommitTimeUS, commitTimeUS);
        DispatchQueuedLoads();
        return true;
    }

    void ModelAssetRegistry::WaitForPendingLoad(FileFormat::AssetID assetID)
    {
        enki::TaskScheduler* taskScheduler = ServiceLocator::GetTaskScheduler();
        for (;;)
        {
            PumpCompletedLoads();
            const auto pending = _async->pendingLoads.find(assetID);
            if (pending == _async->pendingLoads.end() || pending->second.state == AsyncState::State::Complete)
                return;
            if (pending->second.state == AsyncState::State::Running)
            {
                taskScheduler->WaitforTask(pending->second.job);
                continue;
            }

            if (!_async->activeJobs.empty())
                taskScheduler->WaitforTask(_async->activeJobs.front().get());
            else
                DispatchQueuedLoads();
        }
    }

    void ModelAssetRegistry::CancelAndDrainLoads()
    {
        if (!_async)
            return;

        ++_async->epoch;
        _async->cancelledLoads += static_cast<u32>(_async->pendingLoads.size());
        enki::TaskScheduler* taskScheduler = ServiceLocator::GetTaskScheduler();
        for (const std::unique_ptr<AsyncState::Job>& job : _async->activeJobs)
            taskScheduler->WaitforTask(job.get());
        _async->activeJobs.clear();

        AsyncState::Completion completion;
        while (_async->completions.try_dequeue(completion)) {}
        _async->pendingLoads.clear();
    }

    bool ModelAssetRegistry::InitializeFallback()
    {
        const std::array materials = {_materialStorage->GetFallbackMaterialInstance()};
        u32 materialTableOffset = 0;
        if (!_materialStorage->AppendMaterialTable(materials, materialTableOffset))
            return false;

        const ModelAssetView fallback = GetFallbackModelAssetView();
        if (!_geometryStorage->Append(fallback, materialTableOffset, static_cast<u32>(materials.size()),
                                      _fallbackModel))
            return false;

        return static_cast<RenderAssets::ModelHandle::type>(_fallbackModel) == 0;
    }

    RenderAssets::ModelHandle ModelAssetRegistry::Load(FileFormat::AssetID assetID)
    {
        ZoneScopedN("ModelAssetRegistry::Load");
        RenderAssets::ModelHandle handle;
        if (BeginLoad(assetID, handle) == ModelLoadStatus::Ready)
            return handle;
        WaitForPendingLoad(assetID);
        return CommitPendingLoad(assetID, handle) ? handle : _fallbackModel;
    }

    bool ModelAssetRegistry::Release(FileFormat::AssetID assetID)
    {
        const auto existing = _entries.find(assetID);
        if (existing != _entries.end() && existing->second.referenceCount > 0)
        {
            --existing->second.referenceCount;
            // Residency and stable geometry handles are intentionally retained until eviction is implemented.
            return true;
        }

        const auto pending = _async->pendingLoads.find(assetID);
        if (pending != _async->pendingLoads.end() && pending->second.referenceCount > 0)
        {
            --pending->second.referenceCount;
            if (pending->second.referenceCount == 0 && pending->second.state == AsyncState::State::Queued)
            {
                _async->pendingLoads.erase(pending);
                ++_async->cancelledLoads;
            }
            return true;
        }

        {
            ++_releaseUnderflows;
            NC_LOG_ERROR("MODEL_ASSET release_rejected asset={} reason=not_acquired", assetID);
            return false;
        }
    }

    EmbeddedModelLoadStatus ModelAssetRegistry::LoadEmbedded(FileFormat::AssetID assetID, bool acquireReference,
                                                             RenderAssets::ModelHandle& outHandle)
    {
        ZoneScopedN("ModelAssetRegistry::LoadEmbedded");

        outHandle = RenderAssets::ModelHandle::Invalid();
        if (assetID == FileFormat::INVALID_ASSET_ID)
            return EmbeddedModelLoadStatus::InvalidReference;

        const ModelLoadStatus loadStatus = acquireReference ? BeginLoad(assetID, outHandle) :
                                                              PollLoad(assetID, outHandle);
        if (loadStatus == ModelLoadStatus::Pending)
            return EmbeddedModelLoadStatus::Pending;
        const auto loaded = _entries.find(assetID);
        return loaded != _entries.end() && !loaded->second.usedFallback
            ? EmbeddedModelLoadStatus::Loaded
            : EmbeddedModelLoadStatus::Failed;
    }

    ModelAssetRegistryStats ModelAssetRegistry::GetStats() const
    {
        ModelAssetRegistryStats stats;
        stats.residentModels = static_cast<u32>(_entries.size()) + 1;
        for (const auto& [assetID, entry] : _entries)
            stats.references += entry.referenceCount;
        for (const auto& [assetID, pending] : _async->pendingLoads)
            stats.references += pending.referenceCount;
        stats.cacheHits = _cacheHits;
        stats.failures = _failures;
        stats.pendingLoads = static_cast<u32>(_async->pendingLoads.size());
        stats.activeJobs = static_cast<u32>(_async->activeJobs.size());
        stats.peakActiveJobs = _async->peakActiveJobs;
        stats.peakPendingLoads = _async->peakPendingLoads;
        stats.coalescedRequests = _async->coalescedRequests;
        stats.completedReads = _async->completedReads;
        stats.committedLoads = _async->committedLoads;
        stats.cancelledLoads = _async->cancelledLoads;
        stats.decodedBytes = _async->decodedBytes;
        stats.peakDecodedBytes = _async->peakDecodedBytes;
        stats.totalCommitTimeUS = _async->totalCommitTimeUS;
        stats.maxCommitTimeUS = _async->maxCommitTimeUS;
        stats.limitations = _limitations;
        return stats;
    }

    RenderAssets::ModelHandle ModelAssetRegistry::RecordFailure(FileFormat::AssetID assetID, const char* reason,
                                                                bool sourceFileMissing, u32 references)
    {
        NC_LOG_ERROR("MODEL_ASSET model_fallback asset={} reason={}", assetID, reason);
        _entries.emplace(assetID, Entry{_fallbackModel, references, true, sourceFileMissing});
        ++_failures;
        return _fallbackModel;
    }
} // namespace ModelLoading

#pragma once

#include "Game-Lib/Rendering/Terrain/TerrainLoader.h"

#include <Base/Types.h>
#include <FileFormat/Novus/Map/MapChunk.h>
#include <Renderer/Descriptors/TextureDesc.h>

#include <robinhood/robinhood.h>

#include <array>
#include <deque>
#include <limits>
#include <string>
#include <vector>

class DebugRenderer;
class TerrainRenderer;

namespace Editor
{
    enum class TerrainSculptOperation : u8
    {
        AddHeight,
        Flatten,
        Smooth
    };

    class TerrainEditSession
    {
    public:
        struct State
        {
        public:
            bool available = false;
            bool enabled = false;
            bool strokeActive = false;
            bool cursorHit = false;
            bool canUndo = false;
            bool canRedo = false;
            u32 dirtyChunkCount = 0;
            u32 blockedPaintCellCount = 0;
            vec3 cursorPosition = vec3(0.0f);
        };

        struct TextureLayerState
        {
        public:
            std::string path;
            u64 textureHash = 0;
            u32 layerIndex = 0;
            f32 averageWeight = 0.0f;
        };

    public:
        TerrainEditSession(TerrainLoader& terrainLoader, TerrainRenderer& terrainRenderer, DebugRenderer& debugRenderer);

        void Update(f32 deltaTime);

        bool SetEnabled(bool enabled);
        void SetPreviewRadius(f32 radius);
        bool BeginStroke(const std::string& name);
        bool ApplyStrokeSample(TerrainSculptOperation operation, const vec3& position, f32 radius, f32 strength, f32 hardness, f32 deltaTime, f32 targetHeight);
        bool SetPaintTexture(const std::string& virtualPath);
        bool ApplyPaintSample(const vec3& position, f32 radius, f32 pressure, f32 hardness, f32 deltaTime, f32 targetOpacity);
        void GetCursorTextureLayers(std::vector<TextureLayerState>& outLayers);
        bool CommitStroke();
        bool CancelStroke();
        bool Undo();
        bool Redo();
        bool Save();

        State GetState() const;

    private:
        struct VertexAddress
        {
        public:
            u32 chunkID = Terrain::CHUNK_INVALID_ID;
            u16 cellID = 0;
            u16 vertexID = 0;
        };

        struct VertexDelta
        {
        public:
            VertexAddress address;
            f32 before = 0.0f;
            f32 after = 0.0f;
        };

        struct TextureCellDelta
        {
        public:
            u32 chunkID = Terrain::CHUNK_INVALID_ID;
            u16 cellID = 0;
            std::vector<u8> before;
            std::vector<u8> after;
        };

        struct CellLayerDelta
        {
        public:
            u32 chunkID = Terrain::CHUNK_INVALID_ID;
            u16 cellID = 0;
            std::array<u64, Map::CellsData::CELL_LAYER_COUNT> before = {};
            std::array<u64, Map::CellsData::CELL_LAYER_COUNT> after = {};
        };

        struct ChunkAlphaMapDelta
        {
        public:
            u32 chunkID = Terrain::CHUNK_INVALID_ID;
            u64 before = Terrain::TEXTURE_ID_INVALID;
            u64 after = Terrain::TEXTURE_ID_INVALID;
        };

        struct Transaction
        {
        public:
            std::string name;
            std::vector<VertexDelta> deltas;
            robin_hood::unordered_map<u64, u32> deltaLookup;
            std::vector<TextureCellDelta> textureCellDeltas;
            robin_hood::unordered_map<u32, u32> textureCellDeltaLookup;
            std::vector<CellLayerDelta> cellLayerDeltas;
            robin_hood::unordered_map<u64, u32> cellLayerDeltaLookup;
            std::vector<ChunkAlphaMapDelta> chunkAlphaMapDeltas;
            robin_hood::unordered_map<u32, u32> chunkAlphaMapDeltaLookup;
            robin_hood::unordered_set<u32> affectedChunkIDs;
        };

        struct EditableAlphaMap
        {
        public:
            std::string virtualPath;
            std::vector<u8> rgba;
            std::array<u8, Terrain::CHUNK_NUM_CELLS> layerUsageMasks = {};
            std::array<bool, Terrain::CHUNK_NUM_CELLS> layerUsageValid = {};
            Renderer::TextureID textureID = Renderer::TextureID::Invalid();
            bool dirty = false;
        };

        struct VertexCandidate
        {
        public:
            VertexAddress address;
            vec2 position = vec2(0.0f);
            f32 distance = 0.0f;
        };

        struct PaintCellChange
        {
        public:
            u16 minTexelX = std::numeric_limits<u16>::max();
            u16 minTexelY = std::numeric_limits<u16>::max();
            u16 maxTexelX = 0;
            u16 maxTexelY = 0;
            bool alphaChanged = false;
            bool layersChanged = false;
        };

        struct PaintCellWork
        {
        public:
            Map::Chunk* chunk = nullptr;
            EditableAlphaMap* alphaMap = nullptr;
            u32 chunkID = Terrain::CHUNK_INVALID_ID;
            u16 cellID = 0;
            u32 selectedLayerIndex = 0;
            u32 layerCount = 0;
            i32 minTexelX = 0;
            i32 maxTexelX = 0;
            i32 minTexelY = 0;
            i32 maxTexelY = 0;
            f32 cellMinWorldX = 0.0f;
            f32 cellMaxWorldZ = 0.0f;
            size_t beforeOffset = std::numeric_limits<size_t>::max();
            bool textureCellRecorded = false;
            bool layersChanged = false;
            PaintCellChange change;
        };

    private:
        void RefreshLoadedChunks();
        void RefreshLoadedBounds();
        void ResetForMapChange(const std::string& mapName);
        bool CalculateCursorHit(vec3& outPosition) const;
        bool SampleHeight(const vec2& worldPosition, f32& outHeight) const;
        bool ApplyDab(TerrainSculptOperation operation, const vec3& position, f32 radius, f32 strength, f32 hardness, f32 deltaTime, f32 targetHeight, robin_hood::unordered_set<u32>& outChangedCells);
        bool ApplyPaintDab(const vec3& position, f32 radius, f32 targetOpacity, robin_hood::unordered_map<u32, PaintCellChange>& outChangedCells);
        bool GetCellAtWorldPosition(const vec2& worldPosition, u32& outChunkID, u16& outCellID, vec2* outLocalPosition = nullptr) const;
        EditableAlphaMap* GetOrCreateAlphaMap(u32 chunkID);
        bool EnsureAlphaMapRenderable(u32 chunkID, EditableAlphaMap& alphaMap);
        u8 GetLayerUsageMask(EditableAlphaMap& alphaMap, u16 cellID);
        bool PrepareCellForTexture(Map::Chunk& chunk, EditableAlphaMap& alphaMap, u32 chunkID, u16 cellID, u32& outLayerIndex, bool& outAlphaMapRepacked);
        void RemoveTextureLayer(Map::Chunk& chunk, EditableAlphaMap& alphaMap, u16 cellID, u32 layerIndex);
        void UploadPaintChanges(const robin_hood::unordered_map<u32, PaintCellChange>& changedCells);
        void UploadChangedAlphaCells(const robin_hood::unordered_set<u32>& changedCells);
        bool SaveAlphaMaps(const std::vector<u32>& chunkIDs, robin_hood::unordered_set<u32>& outSavedChunkIDs);

        void GatherVertices(const vec2& center, f32 radius, std::vector<VertexCandidate>& outCandidates) const;
        void RecordBeforeChange(const VertexAddress& address, f32 height);
        void RecordTextureCellBeforeChange(u32 chunkID, u16 cellID, const u8* cellData);
        void RecordCellLayersBeforeChange(u32 chunkID, u16 cellID, const u64* layers);
        void RecordChunkAlphaMapBeforeChange(u32 chunkID, u64 alphaMapHash);
        void RefreshDerivedTerrain(const robin_hood::unordered_set<u32>& changedCells, robin_hood::unordered_set<u32>* outAffectedChunkIDs = nullptr);
        void ApplyTransaction(const Transaction& transaction, bool useAfterValues);
        void MarkTransactionChunksEdited(const Transaction& transaction);
        void EnforceHistoryBudget();

        static size_t CalculateTransactionSize(const Transaction& transaction);
        static u64 PackVertexAddress(const VertexAddress& address);
        static u32 PackCellAddress(u32 chunkID, u16 cellID);
        static vec2 GetVertexWorldPosition(u32 chunkID, u16 cellID, u16 vertexID);
        static f32 CalculateFalloff(f32 distance, f32 radius, f32 hardness);

    private:
        static constexpr size_t HISTORY_MEMORY_BUDGET = 128 * 1024 * 1024;

        TerrainLoader& _terrainLoader;
        TerrainRenderer& _terrainRenderer;
        DebugRenderer& _debugRenderer;

        robin_hood::unordered_map<u32, TerrainLoader::LoadedChunkView> _loadedChunks;
        robin_hood::unordered_set<u32> _dirtyChunks;
        robin_hood::unordered_set<u32> _editablePaintChunkIDs;
        robin_hood::unordered_map<u32, TerrainLoader::LoadedChunkView> _editableChunkScratch;
        robin_hood::unordered_map<u32, EditableAlphaMap> _editableAlphaMaps;
        robin_hood::unordered_map<u32, std::vector<u16>> _chunkCellScratch;
        robin_hood::unordered_map<u32, PaintCellChange> _paintCellScratch;
        robin_hood::unordered_set<u32> _blockedPaintCellScratch;
        robin_hood::unordered_set<u32> _changedCellScratch;
        robin_hood::unordered_set<u32> _affectedCellScratch;
        robin_hood::unordered_set<u32> _transactionChunkScratch;
        std::vector<VertexCandidate> _candidateScratch;
        std::vector<f32> _newHeightScratch;
        std::vector<PaintCellWork> _paintCellWorkScratch;
        std::vector<u8> _paintBeforeScratch;
        std::array<f32, 2048> _paintBlendScratch = {};
        std::vector<u16> _layerCellScratch;
        std::vector<u8> _alphaUploadScratch;
        std::deque<Transaction> _undoHistory;
        std::deque<Transaction> _redoHistory;
        Transaction _activeTransaction;

        std::string _mapName;
        u64 _observedContentGeneration = 0;
        size_t _historyBytes = 0;

        vec3 _cursorPosition = vec3(0.0f);
        vec3 _lastSamplePosition = vec3(0.0f);
        vec3 _loadedBoundsMin = vec3(0.0f);
        vec3 _loadedBoundsMax = vec3(0.0f);
        f32 _previewRadius = 10.0f;
        u64 _paintTextureHash = Terrain::TEXTURE_ID_INVALID;
        std::string _paintTexturePath;
        bool _enabled = false;
        bool _strokeActive = false;
        bool _hasCursorHit = false;
        bool _hasLastSample = false;
        bool _hasLoadedBounds = false;
    };
}

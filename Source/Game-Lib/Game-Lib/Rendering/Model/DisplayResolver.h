#pragma once
#include "Game-Lib/Rendering/Asset/RenderAssetHandles.h"
#include "Game-Lib/Rendering/Scene/RenderSceneHandles.h"

#include <Base/Types.h>

#include <FileFormat/Novus/Model/Model.h>

#include <robinhood/robinhood.h>

#include <vector>

namespace ClientDB
{
    struct Data;
}

namespace RenderAssets
{
    class RenderAssetResources;
}

namespace RenderScenes
{
    class RenderScene;
}

namespace ModelLoading
{
    enum class DisplaySource : u8
    {
        CreatureDisplayInfo = 0,
        ItemDisplayInfo = 1
    };

    enum class DisplayApplyResult : u8
    {
        Applied,
        SelectionNotFound,
        InvalidMaterialSlot,
        InvalidParameter,
        MaterialLoadFailed,
        SceneUpdateFailed
    };

    struct DisplayResolverStats
    {
        u32 selections = 0;
        u32 assignments = 0;
        u32 applyRequests = 0;
        u32 appliedSelections = 0;
        u32 missingSelections = 0;
        u32 failures = 0;
    };

    // Indexes CPU-side display registrations and their typed Model parameter overrides.
    // It resolves display choices into deduplicated material instances before updating a Scene instance.
    class DisplayResolver
    {
      public:
        explicit DisplayResolver(RenderAssets::RenderAssetResources* assets) : _assets(assets) { }

        bool Initialize(ClientDB::Data& registrations, ClientDB::Data& parameters);
        DisplayApplyResult Apply(RenderScenes::RenderScene& scene,
                                         RenderScenes::ModelInstanceHandle instance,
                                         RenderAssets::ModelHandle model,
                                         FileFormat::AssetID modelAssetID,
                                         DisplaySource source, u32 displayID, u8 modelVariant);
        DisplayResolverStats GetStats() const;

      private:
        struct Key
        {
            u32 displayID = 0;
            DisplaySource source = DisplaySource::CreatureDisplayInfo;
            u8 modelVariant = 0;

            bool operator==(const Key&) const = default;
        };

        struct KeyHash
        {
            size_t operator()(const Key& key) const;
        };

        struct Range
        {
            u32 offset = 0;
            u32 count = 0;
        };

        struct Registration
        {
            FileFormat::AssetID modelAssetID = FileFormat::INVALID_ASSET_ID;
            u32 overrideOffset = 0;
            u32 overrideCount = 0;
        };

        struct ParameterOverride
        {
            u64 value[2] = {};
            u32 stableID = 0;
            FileFormat::Model::ParameterType type = FileFormat::Model::ParameterType::Float;
        };

        RenderAssets::RenderAssetResources* _assets = nullptr;
        robin_hood::unordered_map<Key, Range, KeyHash> _ranges;
        std::vector<Registration> _registrations;
        std::vector<ParameterOverride> _overrides;
        u32 _applyRequests = 0;
        u32 _appliedSelections = 0;
        u32 _missingSelections = 0;
        u32 _failures = 0;
    };
} // namespace ModelLoading

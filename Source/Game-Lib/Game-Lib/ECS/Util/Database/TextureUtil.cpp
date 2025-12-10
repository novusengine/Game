#include "TextureUtil.h"

#include "Game-Lib/ECS/Singletons/Database/ClientDBSingleton.h"
#include "Game-Lib/ECS/Singletons/Database/TextureSingleton.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <MetaGen/Shared/ClientDB/ClientDB.h>

#include <entt/entt.hpp>

namespace ECSUtil::Texture
{
    bool Refresh()
    {
        entt::registry* registry = ServiceLocator::GetEnttRegistries()->dbRegistry;
        auto& ctx = registry->ctx();
        auto& clientDBSingleton = ctx.get<ECS::Singletons::ClientDBSingleton>();
        auto& textureSingleton = ctx.get<ECS::Singletons::TextureSingleton>();

        if (!clientDBSingleton.Has(ClientDBHash::ItemDisplayMaterialResources))
        {
            clientDBSingleton.Register(ClientDBHash::ItemDisplayMaterialResources, "ItemDisplayMaterialResources");

            auto* itemDisplayMaterialResourcesStorage = clientDBSingleton.Get(ClientDBHash::ItemDisplayMaterialResources);
            itemDisplayMaterialResourcesStorage->Initialize<MetaGen::Shared::ClientDB::ItemDisplayInfoMaterialResourceRecord>();
        }

        if (!clientDBSingleton.Has(ClientDBHash::ItemDisplayMaterialResources))
        {
            clientDBSingleton.Register(ClientDBHash::ItemDisplayModelMaterialResources, "ItemDisplayModelMaterialResources");

            auto* itemDisplayModelMaterialResourcesStorage = clientDBSingleton.Get(ClientDBHash::ItemDisplayModelMaterialResources);
            itemDisplayModelMaterialResourcesStorage->Initialize<MetaGen::Shared::ClientDB::ItemDisplayInfoModelMaterialResourceRecord>();
        }

        auto* textureFileDataStorage = clientDBSingleton.Get(ClientDBHash::TextureFileData);
        u32 numRecords = textureFileDataStorage->GetNumRows();

        textureSingleton.materialResourcesIDToTextureHashes.clear();
        textureSingleton.materialResourcesIDToTextureHashes.reserve(numRecords);

        textureFileDataStorage->Each([&textureFileDataStorage, &textureSingleton](u32 id, const MetaGen::Shared::ClientDB::TextureFileDataRecord& row)
        {
            if (id == 0) return true;

            u32 textureHash = textureFileDataStorage->GetStringHash(row.texture);
            textureSingleton.materialResourcesIDToTextureHashes[row.materialResourcesID].push_back(textureHash);
            return true;
        });

        return true;
    }

    bool HasTexture(ECS::Singletons::TextureSingleton& textureSingleton, u32 textureHash)
    {
        return textureSingleton.textureHashToPath.contains(textureHash);
    }
    const std::string& GetTexturePath(ECS::Singletons::TextureSingleton& textureSingleton, u32 textureHash)
    {
        return textureSingleton.textureHashToPath[textureHash];
    }
}

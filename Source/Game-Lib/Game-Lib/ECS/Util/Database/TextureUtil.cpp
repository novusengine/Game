#include "IconUtil.h"

#include "Game-Lib/ECS/Singletons/Database/ClientDBSingleton.h"
#include "Game-Lib/ECS/Singletons/Database/TextureSingleton.h"
#include "Game-Lib/Gameplay/Database/Shared.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <entt/entt.hpp>
#include "TextureUtil.h"

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
            itemDisplayMaterialResourcesStorage->Initialize({
                { "DisplayID",              ClientDB::FieldType::I32 },
                { "ComponentSection",       ClientDB::FieldType::I8 },
                { "MaterialResourcesID",    ClientDB::FieldType::I32 }
                });
        }

        if (!clientDBSingleton.Has(ClientDBHash::ItemDisplayMaterialResources))
        {
            clientDBSingleton.Register(ClientDBHash::ItemDisplayModelMaterialResources, "ItemDisplayModelMaterialResources");

            auto* itemDisplayModelMaterialResourcesStorage = clientDBSingleton.Get(ClientDBHash::ItemDisplayModelMaterialResources);
            itemDisplayModelMaterialResourcesStorage->Initialize({
                { "DisplayID",              ClientDB::FieldType::I32    },
                { "ModelIndex",             ClientDB::FieldType::I8     },
                { "TextureType",            ClientDB::FieldType::I8     },
                { "MaterialResourcesID",    ClientDB::FieldType::I32    }
                });
        }

        {
            auto* textureFileDataStorage = clientDBSingleton.Get(ClientDBHash::TextureFileData);
            u32 numRecords = textureFileDataStorage->GetNumRows();

            textureSingleton.materialResourcesIDToTextureHashes.clear();
            textureSingleton.materialResourcesIDToTextureHashes.reserve(numRecords);

            textureFileDataStorage->Each([&textureSingleton](u32 id, const ClientDB::Definitions::TextureFileData& row)
                {
                    if (id == 0) return true;

                    textureSingleton.materialResourcesIDToTextureHashes[row.materialResourcesID].push_back(row.textureHash);
                    return true;
                });
        }

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

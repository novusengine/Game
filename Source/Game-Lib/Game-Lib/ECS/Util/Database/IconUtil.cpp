#include "IconUtil.h"

#include "Game-Lib/ECS/Singletons/Database/ClientDBSingleton.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <Meta/Generated/Shared/ClientDB.h>

#include <entt/entt.hpp>

namespace ECSUtil::Icon
{
    bool Refresh()
    {
        entt::registry* registry = ServiceLocator::GetEnttRegistries()->dbRegistry;
        auto& ctx = registry->ctx();
        auto& clientDBSingleton = ctx.get<ECS::Singletons::ClientDBSingleton>();

        if (!clientDBSingleton.Has(ClientDBHash::Icon))
        {
            clientDBSingleton.Register(ClientDBHash::Icon, "Icon");

            auto* iconStorage = clientDBSingleton.Get(ClientDBHash::Icon);
            iconStorage->Initialize<Generated::IconRecord>();

            Generated::IconRecord defaultIcon;
            defaultIcon.texture = iconStorage->AddString("Data/Texture/interface/icons/inv_misc_questionmark.dds");

            iconStorage->Replace(0, defaultIcon);
        }

        auto* iconStorage = clientDBSingleton.Get(ClientDBHash::Icon);
        static const std::filesystem::path fileExtension = ".dds";
        std::filesystem::path currentPath = std::filesystem::current_path();
        std::filesystem::path relativeParentPath = "Data/Texture/interface/icons";
        std::filesystem::path absolutePath = std::filesystem::absolute(relativeParentPath).make_preferred();

        // Note : This is used because it is more performant than doing fs::relative by up to 3x on 140k textures
        std::string absolutePathStr = currentPath.string();
        size_t subStrIndex = absolutePathStr.length() + 1; // + 1 here for folder seperator

        bool storageIsDirty = false;

        if (std::filesystem::exists(absolutePath))
        {
            std::vector<std::filesystem::path> paths;
            std::filesystem::recursive_directory_iterator dirpos{ absolutePath };
            std::copy(begin(dirpos), end(dirpos), std::back_inserter(paths));

            for (const auto& path : paths)
            {
                if (!path.has_extension() || path.extension().compare(fileExtension) != 0)
                    continue;

                std::string texturePath = path.string().substr(subStrIndex);
                std::replace(texturePath.begin(), texturePath.end(), '\\', '/');

                if (iconStorage->HasString(texturePath))
                    continue;

                Generated::IconRecord icon;
                icon.texture = iconStorage->AddString(texturePath);

                iconStorage->Add(icon);
                storageIsDirty |= true;
            }
        }

        if (storageIsDirty)
            iconStorage->MarkDirty();

        return true;
    }
}

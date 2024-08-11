#include "AnimationUtil.h"

#include "Game-Lib/ECS/Singletons/ClientDBCollection.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <FileFormat/Novus/ClientDB/Definitions.h>

#include <entt/entt.hpp>

namespace Util::Animation
{
    const ::ClientDB::Definitions::AnimationData* GetAnimationDataRec(entt::registry& registry, ::Animation::Type type)
    {
        auto& clientDBCollection = registry.ctx().get<ECS::Singletons::ClientDBCollection>();
        auto* animationDatas = clientDBCollection.Get<ClientDB::Definitions::AnimationData>(ECS::Singletons::ClientDBHash::AnimationData);

        if (!animationDatas)
            return nullptr;

        u32 typeIndex = static_cast<u32>(type);
        return animationDatas->GetRow(typeIndex);
    }
}

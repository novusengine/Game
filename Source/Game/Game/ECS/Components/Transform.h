#pragma once
#include <Base/Types.h>
#include <Base/Util/Reflection.h>
#include <Base/Container/ConcurrentQueue.h>

#include "entt/entity/entity.hpp"

namespace ECS::Components{ struct Transform; }

namespace ECS::Singletons
{
    struct DirtyTransformQueue
    {
    public:
        template<typename F>
        void ProcessQueue(F&& fn)
        {
            TransformQueueItem item;
            while (elements.try_dequeue(item))
            {
                fn(item.et);
            }
        }

    private:
        friend struct ECS::Components::Transform;
        struct TransformQueueItem
        {
        public:
            entt::entity et;
        };

        moodycamel::ConcurrentQueue<TransformQueueItem> elements;
    };
}

namespace ECS::Components
{
    struct DirtyTransform
    {
    public:
        u64 dirtyFrame = 0;
    };

    struct Transform
    {
    public:
        // We are using Unitys Right Handed coordinate system
        // +X = right
        // +Y = up
        // +Z = forward
        static const vec3 WORLD_FORWARD;
        static const vec3 WORLD_RIGHT;
        static const vec3 WORLD_UP;

        //makes the component use pointer stable references in entt. do not remove
        static constexpr auto in_place_delete = true;

        vec3 GetLocalForward() const
        {
            return glm::toMat4(rotation) * vec4(WORLD_FORWARD, 0);
        }

        vec3 GetLocalRight() const
        {
            return glm::toMat4(rotation) * vec4(WORLD_RIGHT, 0);
        }

        vec3 GetLocalUp() const
        {
            return glm::toMat4(rotation) * vec4(WORLD_UP, 0);
        }

        mat4x4 GetMatrix() const
        {
            mat4x4 rotationMatrix = glm::toMat4(rotation);
            mat4x4 scaleMatrix = glm::scale(mat4x4(1.0f), scale);
            return glm::translate(mat4x4(1.0f), position) * rotationMatrix * scaleMatrix;
        }

        void SetDirty(ECS::Singletons::DirtyTransformQueue& dirtyQueue, entt::entity ownerEntity)
        {
            if (ownerEntity != entt::null)
            {
                dirtyQueue.elements.enqueue({ ownerEntity });
            }
        }

        vec3 position = vec3(0.0f, 0.0f, 0.0f);
        quat rotation = quat(1.0f, 0.0f, 0.0f, 0.0f);
        vec3 scale = vec3(1.0f, 1.0f, 1.0f);
    };
}

REFL_TYPE(ECS::Components::Transform)
REFL_FIELD(position)
REFL_FIELD(rotation)
REFL_FIELD(scale, Reflection::DragSpeed(0.1f))
REFL_END
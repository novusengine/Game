#pragma once
#include <Base/Types.h>
#include <entt/fwd.hpp>

#include <Base/Types.h>
#include <Base/Util/Reflection.h>
#include <Base/Container/ConcurrentQueue.h>

#include "entt/entity/entity.hpp"
#include "Base/Math/Math.h"

namespace ECS::Components { struct Transform; }

namespace ECS {

    struct TransformSystem
    {
    public:

        static TransformSystem& Get(entt::registry& registry);

        //api with entityID alone
        void SetPosition(entt::entity entity, const vec3& newPosition);
        void SetRotation(entt::entity entity, const quat& newRotation);
        void SetScale(entt::entity entity, const vec3& newScale);
        void SetPositionAndRotation(entt::entity entity, const vec3& newpos, const quat& newrotation);
        void SetComponents(entt::entity entity, const vec3& newpos, const quat& newrotation, const vec3& newscale);
        void AddOffset(entt::entity entity, const vec3& offset);

        //api with transform component and entity ID to save lookup
        void SetPosition(entt::entity entity, ECS::Components::Transform& transform, const vec3& newPosition);
        void SetRotation(entt::entity entity, ECS::Components::Transform& transform, const quat& newRotation);
        void SetScale(entt::entity entity, ECS::Components::Transform& transform, const vec3& newScale);
        void SetPositionAndRotation(entt::entity entity, ECS::Components::Transform& transform, const vec3& newpos, const quat& newrotation);
        void SetComponents(entt::entity entity, ECS::Components::Transform& transform, const vec3& newpos, const quat& newrotation, const vec3& newscale);
        void AddOffset(entt::entity entity, ECS::Components::Transform& transform, const vec3& offset);

        void ParentEntityTo(entt::entity parent, entt::entity child);

        template<typename F>
        void ProcessMovedEntities(F&& fn)
        {
            TransformQueueItem item;
            while (elements.try_dequeue(item))
            {
                fn(item.et);
            }
        }

    private:
        entt::registry* owner;

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
        friend struct ECS::TransformSystem;

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

        mat4x4 GetMatrix() const;

        mat4a GetLocalMatrix() const
        {
            return Math::AffineMatrix::TransformMatrix(position, rotation, scale);
        }

        void SetDirty(ECS::TransformSystem& dirtyQueue, entt::entity ownerEntity)
        {
            if (ownerEntity != entt::null)
            {
                dirtyQueue.elements.enqueue({ ownerEntity });
            }
        }

        const vec3& GetPosition() const {
            return position;
        }

        const vec3 GetWorldPosition() const;

        const quat& GetRotation() const {
            return rotation;
        }
        const vec3& GetScale() const {
            return scale;
        }

        struct SceneNode* ownerNode{ nullptr };

    private:
        vec3 position = vec3(0.0f, 0.0f, 0.0f);
        quat rotation = quat(1.0f, 0.0f, 0.0f, 0.0f);
        vec3 scale = vec3(1.0f, 1.0f, 1.0f);
    };

    struct SceneNode {
        mat4a matrix;
        Transform* transform{};
        entt::entity ownerEntity;

        SceneNode* parent{};
        SceneNode* firstChild{};
        SceneNode* nextBrother{};
        SceneNode* prevBrother{};
        int children{ 0 };

        //makes the component use pointer stable references in entt. do not remove
        static constexpr auto in_place_delete = true;

        SceneNode(Transform* tf, entt::entity owner) {

            transform = tf;
            tf->ownerNode = this;
            ownerEntity = owner;
        }
        ~SceneNode() {
            //disconnect properly

            //separate from parent
            detachParent();

            SceneNode* c = firstChild;
            //unparent children
            while (c) {
                SceneNode* next = c->nextBrother;
                c->nextBrother = nullptr;
                c->prevBrother = nullptr;
                c->parent = nullptr;
                c = next;
            }
        }

        void detachParent() {
            if (parent) {
                parent->children--;
                //its a circular linked list, if brother == node, its a single-child
                if (nextBrother == this) {
                    parent->firstChild = nullptr;
                }
                else {
                    prevBrother->nextBrother = nextBrother;
                    nextBrother->prevBrother = prevBrother;
                }

                nextBrother = nullptr;
                prevBrother = nullptr;
                parent = nullptr;
            }
        }

        void setParent(SceneNode* newParent) {
            if (parent == newParent) return; //already a child of this


            detachParent();
            newParent->children++;
            //parent has no children
            if (newParent->firstChild == nullptr) {
                newParent->firstChild = this;
                prevBrother = this;
                nextBrother = this;
            }
            else {
                //insert after the firstchild
                nextBrother = newParent->firstChild->nextBrother;
                prevBrother = newParent->firstChild;

                prevBrother->nextBrother = this;
                nextBrother->prevBrother = this;
            }

            parent = newParent;
        }

        //updates transform matrix of the children. does not recalculate matrix
        // the dirty queue can be kept as null if you dont want the nodes to get added to the dirty transform list
        inline void propagate_matrix(TransformSystem* dirtyQueue) {

            SceneNode* c = firstChild;
            if (c) {
                if (dirtyQueue) {
                    c->transform->SetDirty(*dirtyQueue, c->ownerEntity);
                }

                c->refresh_matrix();
                c->propagate_matrix(dirtyQueue);
                c = c->nextBrother;

                while (c != firstChild) {
                    if (dirtyQueue) {
                        c->transform->SetDirty(*dirtyQueue, c->ownerEntity);
                    }

                    c->refresh_matrix();
                    c->propagate_matrix(dirtyQueue);
                    c = c->nextBrother;
                }
            }
        }

        //recalculates the matrix. If the scene-node has a parent, it gets transform root from it
        inline void refresh_matrix() {
            if (parent)
            {
                matrix = Math::AffineMatrix::MatrixMul(parent->matrix, transform->GetLocalMatrix());
            }
            else {
                matrix = transform->GetLocalMatrix();
            }
        }
    };

    //packed non-ECS scenenode with transform and scenenode
    struct StandaloneNode {
        SceneNode node;
        Transform transform;
    };
}

inline mat4x4 ECS::Components::Transform::GetMatrix() const
{
    if (ownerNode) {
        mat4x4 mt = ownerNode->matrix;
        mt[3][3] = 1.f; //glm does not finish the matrix properly when transforming m4a into m4x4
        return mt;
    }
    else {
        mat4x4 mt = GetLocalMatrix();
        mt[3][3] = 1.f; //glm does not finish the matrix properly when transforming m4a into m4x4
        return mt;
    }
}

inline const vec3 ECS::Components::Transform::GetWorldPosition() const {

    if (ownerNode) {
        //calculate world position by transforming a zero vector
        //vec4 vct = ownerNode->matrix * glm::vec3{ 0.f, 0.f, 0.f};
        return ownerNode->matrix[3];//vct;
    }
    else {
        return GetPosition();
    }
}

REFL_TYPE(ECS::Components::Transform)
//REFL_FIELD(position)
//REFL_FIELD(rotation)
//REFL_FIELD(scale, Reflection::DragSpeed(0.1f))

REFL_END
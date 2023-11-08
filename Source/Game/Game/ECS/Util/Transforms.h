#pragma once

#include <Base/Types.h>
#include <Base/Container/ConcurrentQueue.h>
#include <Base/Math/Math.h>
#include <Base/Util/Reflection.h>

#include <entt/entt.hpp>

namespace ECS::Components { struct Transform; }
namespace Editor { class Inspector; }

namespace ECS
{
    struct TransformSystem
    {
    public:
        static TransformSystem& Get(entt::registry& registry);

        //api with entityID alone. Can do world transforms by accessing the scene component
        void SetLocalPosition(entt::entity entity, const vec3& newPosition);
        void SetWorldPosition(entt::entity entity, const vec3& newPosition);
        void SetLocalRotation(entt::entity entity, const quat& newRotation);
        void SetLocalScale(entt::entity entity, const vec3& newScale);
        void SetLocalPositionAndRotation(entt::entity entity, const vec3& newpos, const quat& newrotation);
        void SetLocalTransform(entt::entity entity, const vec3& newpos, const quat& newrotation, const vec3& newscale);
        void AddLocalOffset(entt::entity entity, const vec3& offset);

        //manually flags the entity as moved. will refresh its matrix and do the same for children
        void RefreshTransform(entt::entity entity, ECS::Components::Transform& transform);

        //api with transform component and entity ID to save lookup. Only local transforms
        void SetLocalPosition(entt::entity entity, ECS::Components::Transform& transform, const vec3& newPosition);
        void SetLocalRotation(entt::entity entity, ECS::Components::Transform& transform, const quat& newRotation);
        void SetLocalScale(entt::entity entity, ECS::Components::Transform& transform, const vec3& newScale);
        void SetLocalPositionAndRotation(entt::entity entity, ECS::Components::Transform& transform, const vec3& newpos, const quat& newrotation);
        void SetLocalTransform(entt::entity entity, ECS::Components::Transform& transform, const vec3& newpos, const quat& newrotation, const vec3& newscale);
        void AddLocalOffset(entt::entity entity, ECS::Components::Transform& transform, const vec3& offset);

        //connects an entity ID into a parent. Will create the required scene-node components on demand if needed
        void ParentEntityTo(entt::entity parent, entt::entity child);        

        //iterates the children of a given node. NOT recursive
        //callback is in the form SceneComponent* child
        template<typename F>
        void IterateChildren(entt::entity node, F&& callback);

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

    //Transform component for handling entity positions/rotations/scale
    //Can be used without a scenenode connection, in that case the transform will work in world space and recalculate the matrix on-demand
    //The components are not directly accessible. Use the transform system to modify them so that matrix refreshing and scenenode hierarchy is updated properly
    struct Transform
    {
        friend struct ECS::TransformSystem;
        friend class Editor::Inspector;
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
            return glm::toMat4(rotation) * vec4(WORLD_FORWARD, 0.0f);
        }

        vec3 GetLocalRight() const
        {
            return glm::toMat4(rotation) * vec4(WORLD_RIGHT, 0.0f);
        }

        vec3 GetLocalUp() const
        {
            return glm::toMat4(rotation) * vec4(WORLD_UP, 0.0f);
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

        const vec3& GetLocalPosition() const
        {
            return position;
        }

        const vec3 GetWorldPosition() const;

        const quat& GetLocalRotation() const
        {
            return rotation;
        }
        const vec3& GetLocalScale() const
        {
            return scale;
        }

        struct SceneNode* ownerNode{ nullptr };

    private:
        vec3 position = vec3(0.0f, 0.0f, 0.0f);
        quat rotation = quat(1.0f, 0.0f, 0.0f, 0.0f);
        vec3 scale = vec3(1.0f, 1.0f, 1.0f);
    };

    //scene node component that holds the information for parenting and children of a given entity or node
    // avoid using it directly, instead using the functions on TransformSystem to handle parenting and iterating children
    //it builds an intrusive double linked list for the siblings of the same parent. 
    //in the case a parent has only 1 child, the sibling list will point to the same object.
    //a scene node cant work on its own, it must have a valid pointer to a Transform component. 
    //when a transform component is connected to a scene node, it uses the scenenode matrix to hold the world matrix of the object.
    //the matrix must be refreshed with RefreshMatrix every time the transform component updates its values
    struct SceneNode
    {
        friend struct ECS::TransformSystem;
        friend struct Transform;
        friend class Editor::Inspector;

    public:
        SceneNode(Transform* tf, entt::entity owner)
        {
            transform = tf;
            tf->ownerNode = this;
            ownerEntity = owner;
        }
        ~SceneNode()
        {
            //separate from parent
            DetachParent();

            SceneNode* c = firstChild;
            //unparent children
            while (c)
            {
                SceneNode* next = c->nextSibling;
                c->nextSibling = nullptr;
                c->prevSibling = nullptr;
                c->parent = nullptr;
                c = next;
            }
        }

        bool HasParent() const
        {
            return parent != nullptr;
        }

        void DetachParent()
        {
            if (parent)
            {
                parent->children--;
                //its a circular linked list, if Sibling == node, its a single-child
                if (nextSibling == this)
                {
                    parent->firstChild = nullptr;
                }
                else
                {
                    prevSibling->nextSibling = nextSibling;
                    nextSibling->prevSibling = prevSibling;
                }

                nextSibling = nullptr;
                prevSibling = nullptr;
                parent = nullptr;
            }
        }

        void SetParent(SceneNode* newParent)
        {
            if (parent == newParent) return; //already a child of this

            DetachParent();
            newParent->children++;
            //parent has no children
            if (newParent->firstChild == nullptr)
            {
                newParent->firstChild = this;
                prevSibling = this;
                nextSibling = this;
            }
            else
            {
                //insert after the firstchild
                nextSibling = newParent->firstChild->nextSibling;
                prevSibling = newParent->firstChild;

                prevSibling->nextSibling = this;
                nextSibling->prevSibling = this;
            }
            parent = newParent;
        }

        //updates transform matrix of the children. does not recalculate matrix
        // the dirty queue can be kept as null if you dont want the nodes to get added to the dirty transform list
        inline void PropagateMatrixToChildren(TransformSystem* dirtyQueue)
        {
            SceneNode* c = firstChild;
            if (c)
            {
                if (dirtyQueue)
                {
                    c->transform->SetDirty(*dirtyQueue, c->ownerEntity);
                }

                c->RefreshMatrix();
                c->PropagateMatrixToChildren(dirtyQueue);
                c = c->nextSibling;

                while (c != firstChild)
                {
                    if (dirtyQueue)
                    {
                        c->transform->SetDirty(*dirtyQueue, c->ownerEntity);
                    }

                    c->RefreshMatrix();
                    c->PropagateMatrixToChildren(dirtyQueue);
                    c = c->nextSibling;
                }
            }
        }

        //recalculates the matrix. If the scene-node has a parent, it gets transform root from it
        inline void RefreshMatrix()
        {
            if (parent)
            {
                matrix = Math::AffineMatrix::MatrixMul(parent->matrix, transform->GetLocalMatrix());
            }
            else
            {
                matrix = transform->GetLocalMatrix();
            }
        }

    private:
        mat4a matrix;
        Transform* transform{};
        entt::entity ownerEntity;

        SceneNode* parent{};
        SceneNode* firstChild{};
        SceneNode* nextSibling{};
        SceneNode* prevSibling{};
        int children{ 0 };

        //makes the component use pointer stable references in entt. do not remove
        static constexpr auto in_place_delete = true;
    };
}

inline mat4x4 ECS::Components::Transform::GetMatrix() const
{
    if (ownerNode)
    {
        mat4x4 mt = ownerNode->matrix;
        mt[3][3] = 1.f; //glm does not finish the matrix properly when transforming m4a into m4x4
        return mt;
    }
    else
    {
        mat4x4 mt = GetLocalMatrix();
        mt[3][3] = 1.f; //glm does not finish the matrix properly when transforming m4a into m4x4
        return mt;
    }
}

inline const vec3 ECS::Components::Transform::GetWorldPosition() const
{
    return ownerNode ? ownerNode->matrix[3] : GetLocalPosition();
}

template<typename F>
void ECS::TransformSystem::IterateChildren(entt::entity entity, F&& callback)
{
    ECS::Components::SceneNode* node = owner->try_get<ECS::Components::SceneNode>(entity);
    if (!node) return;

    ECS::Components::SceneNode* c = node->firstChild;
    if (c)
    {
        callback(c);
        c = c->nextSibling;
        while (c != node->firstChild)
        {
            callback(c);
            c = c->nextSibling;
        }
    }
}

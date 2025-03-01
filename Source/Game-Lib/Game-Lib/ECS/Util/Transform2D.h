#pragma once

#include <Base/Types.h>
#include <Base/Container/ConcurrentQueue.h>
#include <Base/Math/Math.h>
#include <Base/Util/Reflection.h>

#include <entt/entt.hpp>
#include <queue>

namespace ECS::Components { struct Transform2D; }
namespace Editor { class Inspector; }

namespace ECS
{
    struct Transform2DSystem
    {
    public:
        static Transform2DSystem& Get(entt::registry& registry);

        void Clear();

        //api with entityID alone. Can do world transforms by accessing the scene component
        void SetLocalPosition(entt::entity entity, const vec2& newPosition);
        void SetWorldPosition(entt::entity entity, const vec2& newPosition);
        void SetLocalRotation(entt::entity entity, const quat& newRotation);
        void SetWorldRotation(entt::entity entity, const quat& newRotation);
        void SetLocalScale(entt::entity entity, const vec2& newScale);
        void SetLocalPositionAndRotation(entt::entity entity, const vec2& newPosition, const quat& newRotation);
        void SetLocalTransform(entt::entity entity, const vec2& newPosition, const quat& newRotation, const vec2& newScale);
        void AddLocalOffset(entt::entity entity, const vec2& offset);
        void SetLayer(entt::entity entity, u32 newLayer);
        void SetSize(entt::entity entity, const vec2& newSize);
        void SetAnchor(entt::entity entity, const vec2& newAnchor);
        void SetRelativePoint(entt::entity entity, const vec2& newRelativePoint);

        //manually flags the entity as moved. will refresh its matrix and do the same for children
        void RefreshTransform(entt::entity entity, ECS::Components::Transform2D& transform);

        //api with transform component and entity ID to save lookup. Only local transforms
        void SetLocalPosition(entt::entity entity, ECS::Components::Transform2D& transform, const vec2& newPosition);
        void SetLocalRotation(entt::entity entity, ECS::Components::Transform2D& transform, const quat& newRotation);
        void SetLocalScale(entt::entity entity, ECS::Components::Transform2D& transform, const vec2& newScale);
        void SetLocalPositionAndRotation(entt::entity entity, ECS::Components::Transform2D& transform, const vec2& newPosition, const quat& newRotation);
        void SetLocalTransform(entt::entity entity, ECS::Components::Transform2D& transform, const vec2& newPosition, const quat& newRotation, const vec2& newScale);
        void AddLocalOffset(entt::entity entity, ECS::Components::Transform2D& transform, const vec2& offset);
        void SetLayer(entt::entity entity, ECS::Components::Transform2D& transform, u32 newLayer);
        void SetSize(entt::entity entity, ECS::Components::Transform2D& transform, const vec2& newSize);
        void SetAnchor(entt::entity entity, ECS::Components::Transform2D& transform, const vec2& newAnchor);
        void SetRelativePoint(entt::entity entity, ECS::Components::Transform2D& transform, const vec2& newRelativePoint);

        //connects an entity ID into a parent. Will create the required scene-node components on demand if needed
        void ParentEntityTo(entt::entity parent, entt::entity child);        
        void ClearParent(entt::entity entity);

        //iterates the children of a given node. NOT recursive
        //callback is in the form SceneComponent* child
        template<typename F>
        void IterateChildren(entt::entity node, F&& callback);

        //iterates the children of a given node recursively, breadth first
        //callback is in the form SceneComponent* child
        template<typename F>
        void IterateChildrenRecursiveBreadth(entt::entity node, F&& callback);

        //iterates the children of a given node recursively, depth first
        //callback is in the form SceneComponent* child
        template<typename F>
        void IterateChildrenRecursiveDepth(entt::entity node, F&& callback);

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

        friend struct ECS::Components::Transform2D;
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
    struct DirtyTransform2D
    {
    public:
        u64 dirtyFrame = 0;
    };

    //Transform component for handling entity positions/rotations/scale
    //Can be used without a scenenode connection, in that case the transform will work in world space and recalculate the matrix on-demand
    //The components are not directly accessible. Use the transform system to modify them so that matrix refreshing and scenenode hierarchy is updated properly
    struct Transform2D
    {
        friend struct ECS::Transform2DSystem;
        friend class Editor::Inspector;
    public:
        // We are using Unitys Right Handed coordinate system
        // +X = right
        // +Y = up
        static const vec2 WORLD_RIGHT;
        static const vec2 WORLD_UP;

        //makes the component use pointer stable references in entt. do not remove
        static constexpr auto in_place_delete = true;

        vec3 GetLocalRight() const
        {
            return glm::toMat4(rotation) * vec4(WORLD_RIGHT, 0.0f, 0.0f);
        }

        vec3 GetLocalUp() const
        {
            return glm::toMat4(rotation) * vec4(WORLD_UP, 0.0f, 0.0f);
        }

        mat4x4 GetMatrix() const;

        mat4a GetLocalMatrix() const
        {
            vec2 relativePointOffset = relativePoint * size;
            vec2 anchorOffset = vec2(0, 0);

            Transform2D* parentTransform = GetParentTransform();
            if (parentTransform)
            {
                anchorOffset = anchor * parentTransform->size;
            }

            vec2 finalPosition = (position - relativePointOffset) + anchorOffset;
            return Math::AffineMatrix::TransformMatrix(vec3(finalPosition, 0.0f), rotation, vec3(scale, 1.0f));
        }

        void SetDirty(ECS::Transform2DSystem& dirtyQueue, entt::entity ownerEntity)
        {
            if (ownerEntity != entt::null)
            {
                dirtyQueue.elements.enqueue({ ownerEntity });
            }
        }

        const vec2& GetLocalPosition() const
        {
            return position;
        }

        const vec2 GetWorldPosition() const;
        const quat GetWorldRotation() const;

        const quat& GetLocalRotation() const
        {
            return rotation;
        }
        const vec2& GetLocalScale() const
        {
            return scale;
        }

        u32 GetLayer() const
        {
            return layer;
        }
        const vec2& GetSize() const
        {
            return size;
        }
        const vec2& GetAnchor() const
        {
            return anchor;
        }
        const vec2& GetRelativePoint() const
        {
            return relativePoint;
        }

        Transform2D* GetParentTransform() const;
        u32 GetHierarchyDepth() const;

        struct SceneNode2D* ownerNode{ nullptr };

    private:
        vec2 position = vec2(0.0f, 0.0f);
        quat rotation = quat(1.0f, 0.0f, 0.0f, 0.0f);
        vec2 scale = vec2(1.0f, 1.0f);

        u32 layer = 0;
        vec2 size = vec2(1.0f, 1.0f);
        vec2 anchor = vec2(0.0f, 0.0f); // This is the point on the parent widget that we will anchor to
        vec2 relativePoint = vec2(0.0f, 0.0f); // This is the point on this widget that we will anchor to the parent
    };

    //scene node component that holds the information for parenting and children of a given entity or node
    // avoid using it directly, instead using the functions on TransformSystem to handle parenting and iterating children
    //it builds an intrusive double linked list for the siblings of the same parent. 
    //in the case a parent has only 1 child, the sibling list will point to the same object.
    //a scene node cant work on its own, it must have a valid pointer to a Transform component. 
    //when a transform component is connected to a scene node, it uses the scenenode matrix to hold the world matrix of the object.
    //the matrix must be refreshed with RefreshMatrix every time the transform component updates its values
    struct SceneNode2D
    {
        friend struct ECS::Transform2DSystem;
        friend struct Transform2D;
        friend class Editor::Inspector;

    public:
        //makes the component use pointer stable references in entt. do not remove
        static constexpr auto in_place_delete = true;

        SceneNode2D(Transform2D* tf, entt::entity owner)
        {
            transform = tf;
            tf->ownerNode = this;
            ownerEntity = owner;
        }
        ~SceneNode2D()
        {
            //separate from parent
            DetachParent();

            SceneNode2D* c = firstChild;
            //unparent children
            while (c)
            {
                SceneNode2D* next = c->nextSibling;
                c->nextSibling = nullptr;
                c->prevSibling = nullptr;
                c->parent = nullptr;
                c = next;
            }

            if (transform)
                transform->ownerNode = nullptr;

            transform = nullptr;
            ownerEntity = entt::null;

            firstChild = nullptr;
            nextSibling = nullptr;
            prevSibling = nullptr;
            children = 0;
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

                    if (parent->firstChild == this)
                        parent->firstChild = prevSibling;
                }

                nextSibling = nullptr;
                prevSibling = nullptr;
                parent = nullptr;
            }
        }

        void SetParent(SceneNode2D* newParent)
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
        inline void PropagateMatrixToChildren(Transform2DSystem* dirtyQueue)
        {
            SceneNode2D* c = firstChild;
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

        entt::entity GetOwner() const
        {
            return ownerEntity;
        }

        SceneNode2D* GetParent() const
        {
            return parent;
        }

    private:
        mat4a matrix = mat4a(1.0f);
        Transform2D* transform{};
        entt::entity ownerEntity;

        SceneNode2D* parent{};
        SceneNode2D* firstChild{};
        SceneNode2D* nextSibling{};
        SceneNode2D* prevSibling{};
        i32 children{ 0 };
    };
}

inline mat4x4 ECS::Components::Transform2D::GetMatrix() const
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

inline const vec2 ECS::Components::Transform2D::GetWorldPosition() const
{
    return ownerNode ? vec2(ownerNode->matrix[3]) : GetLocalPosition();
}
inline const quat ECS::Components::Transform2D::GetWorldRotation() const
{
    if (ownerNode && ownerNode->parent && ownerNode->parent->transform)
    {
        //finding the rotation from the matrix is not reliable, we need to check up the parenting stack to build the correct rotation operation
        // TODO: find a way to get rotation in reliable way from parent matrix?
        return ownerNode->parent->transform->GetWorldRotation() * GetLocalRotation();
    }
    else
    {
        return GetLocalRotation();
    }
}

template<typename F>
void ECS::Transform2DSystem::IterateChildren(entt::entity entity, F&& callback)
{
    ECS::Components::SceneNode2D* node = owner->try_get<ECS::Components::SceneNode2D>(entity);
    if (!node) return;

    ECS::Components::SceneNode2D* c = node->firstChild;
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

template<typename F>
void ECS::Transform2DSystem::IterateChildrenRecursiveBreadth(entt::entity entity, F&& callback)
{
    ECS::Components::SceneNode2D* rootNode = owner->try_get<ECS::Components::SceneNode2D>(entity);
    if (!rootNode) return;

    std::queue<ECS::Components::SceneNode2D*> nodeQueue;
    nodeQueue.push(rootNode);

    while (!nodeQueue.empty())
    {
        ECS::Components::SceneNode2D* currentNode = nodeQueue.front();
        nodeQueue.pop();

        ECS::Components::SceneNode2D* c = currentNode->firstChild;
        if (c)
        {
            do
            {
                // Apply the callback to the current child node
                callback(c->ownerEntity);

                // Enqueue this child node for further processing
                nodeQueue.push(c);

                // Move to the next sibling
                c = c->nextSibling;
            } while (c != nullptr && c != currentNode->firstChild);
        }
    }
}

// The callback returns a boolean value indicating whether to continue processing the children of the current node
template<typename F>
void ECS::Transform2DSystem::IterateChildrenRecursiveDepth(entt::entity entity, F&& callback)
{
    // Get the current node associated with the entity
    ECS::Components::SceneNode2D* node = owner->try_get<ECS::Components::SceneNode2D>(entity);
    if (!node) return;

    // Process the current entity
    bool continueChildren = callback(entity);

    // Recursively process each child
    ECS::Components::SceneNode2D* child = node->firstChild;
    if (continueChildren && child)
    {
        do
        {
            IterateChildrenRecursiveDepth(child->ownerEntity, callback); // Recurse into the child
            child = child->nextSibling; // Move to the next sibling
        } while (child != nullptr && child != node->firstChild);
    }
}

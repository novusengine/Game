#pragma once
#include <Base/Types.h>
#include <Base/Util/Reflection.h>
#include <Base/Container/ConcurrentQueue.h>

#include "entt/entity/entity.hpp"

namespace ECS::Components
{
	struct DirtyTransform 
	{
	public:
		u64 dirtyFrame = 0;
	};

	struct DirtyTransformQueue {
		struct TransformQueueItem {
			entt::entity et;
		};

		moodycamel::ConcurrentQueue<TransformQueueItem> elements;

		template<typename F>
		void ProcessQueue(F&& fn) {
			TransformQueueItem item;
			while (elements.try_dequeue(item)) {
				fn(item.et);
			}
		}
	};

	struct Transform
	{
	public:
	
		vec3 position = vec3(0.0f, 0.0f, 0.0f);
		quat rotation = quat(0.0f, 0.0f, 0.0f, 1.0f);
		vec3 scale = vec3(1.0f, 1.0f, 1.0f);

		vec3 forward = vec3(0.0f, 0.0f, 1.0f);
		vec3 right = vec3(1.0f, 0.0f, 0.0f);
		vec3 up = vec3(0.0f, 1.0f, 0.0f);


		//makes the component use pointer stable references in entt. do not remove
		static constexpr auto in_place_delete = true;

		mat4x4 GetMatrix() {
			mat4x4 rotationMatrix = glm::toMat4(rotation);
			mat4x4 scaleMatrix = glm::scale(mat4x4(1.0f), scale);
			return glm::translate(mat4x4(1.0f), position) * rotationMatrix * scaleMatrix;
		}

		void SetDirty(DirtyTransformQueue* dirtyQueue,entt::entity ownerEntity) {

			if (ownerEntity != entt::null) {
				dirtyQueue->elements.enqueue({ ownerEntity });
			}
		}

		mat4x4 matrix = mat4x4(1.0f);

		// We are using Unitys Right Handed coordinate system
		// +X = right
		// +Y = up
		// +Z = forward
		static const vec3 WORLD_FORWARD;
		static const vec3 WORLD_RIGHT;
		static const vec3 WORLD_UP;
	};
}

REFL_TYPE(ECS::Components::Transform)
	REFL_FIELD(position)
	REFL_FIELD(rotation)
	REFL_FIELD(scale, Reflection::DragSpeed(0.1f))

	REFL_FIELD(forward, Reflection::Hidden())
	REFL_FIELD(right, Reflection::Hidden())
	REFL_FIELD(up, Reflection::Hidden())
	REFL_FIELD(matrix, Reflection::Hidden())
REFL_END
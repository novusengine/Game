#pragma once
#include <Base/Types.h>
#include <Base/Util/Reflection.h>

namespace ECS::Components
{
	struct Model
	{
	public:
		u32 modelID;
		u32 instanceID;
	};
}

REFL_TYPE(ECS::Components::Model)
	REFL_FIELD(modelID, Reflection::ReadOnly())
	REFL_FIELD(instanceID, Reflection::ReadOnly())
REFL_END
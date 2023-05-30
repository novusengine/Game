#pragma once
#include <Base/Types.h>
#include <Base/Util/Reflection.h>

namespace ECS::Components
{
	struct Name
	{
	public:
		std::string name;
		std::string fullName;
		u32 nameHash;
	};
}

REFL_TYPE(ECS::Components::Name)
	REFL_FIELD(name, Reflection::ReadOnly())
	REFL_FIELD(fullName, Reflection::ReadOnly())
	REFL_FIELD(nameHash, Reflection::Hidden())
REFL_END
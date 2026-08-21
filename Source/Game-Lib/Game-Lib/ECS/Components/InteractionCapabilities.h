#pragma once

#include <MetaGen/Shared/Interaction/Interaction.h>

namespace ECS::Components
{
    struct InteractionCapabilities
    {
    public:
        MetaGen::Shared::Interaction::InteractionCapabilityMaskEnum value = MetaGen::Shared::Interaction::InteractionCapabilityMaskEnum::None;
    };
}

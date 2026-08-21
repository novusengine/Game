#pragma once

#include <Gameplay/GameDefine.h>

#include <MetaGen/Shared/Interaction/Interaction.h>

#include <optional>
#include <string>
#include <vector>

namespace ECS::Singletons
{
    struct InteractionOptionState
    {
    public:
        u64 token = 0;
        u16 icon = 0;
        bool enabled = false;
        std::string text;
        std::string disabledReason;
    };

    struct InteractionSessionState
    {
    public:
        u64 id = 0;
        u32 revision = 0;
        ObjectGUID sourceGUID = ObjectGUID::Empty;
        MetaGen::Shared::Interaction::InteractionSurfaceTypeEnum surfaceType = MetaGen::Shared::Interaction::InteractionSurfaceTypeEnum::Gossip;
        std::string greeting;
        std::vector<InteractionOptionState> options;
    };

    struct InteractionState
    {
    public:
        void Reset()
        {
            activeSession.reset();
            lastClosedSessionID = 0;
            lastCloseReason = MetaGen::Shared::Interaction::InteractionCloseReasonEnum::Count;
            lastResultSessionID = 0;
            lastResultRevision = 0;
            lastResult = MetaGen::Shared::Interaction::InteractionResultEnum::Count;
            Touch();
        }

        void Touch()
        {
            changeVersion++;
            if (changeVersion == 0)
                changeVersion = 1;
        }

    public:
        u64 changeVersion = 1;
        std::optional<InteractionSessionState> activeSession;
        u64 lastClosedSessionID = 0;
        MetaGen::Shared::Interaction::InteractionCloseReasonEnum lastCloseReason = MetaGen::Shared::Interaction::InteractionCloseReasonEnum::Count;
        u64 lastResultSessionID = 0;
        u32 lastResultRevision = 0;
        MetaGen::Shared::Interaction::InteractionResultEnum lastResult = MetaGen::Shared::Interaction::InteractionResultEnum::Count;
    };
}

#pragma once
#include "Game-Lib/Gameplay/Attachment/Defines.h"

#include <Base/Types.h>

#include <robinhood/robinhood.h>

namespace ECS
{
    namespace Components
    {
        struct AttachmentInstance
        {
        public:
            u64 lastUpdatedFrame;
            entt::entity entity;
            mat4x4 matrix;
        };

        struct AttachmentData
        {
        public:
            robin_hood::unordered_map<::Attachment::Defines::Type, AttachmentInstance> attachmentToInstance;
        };
    }
}
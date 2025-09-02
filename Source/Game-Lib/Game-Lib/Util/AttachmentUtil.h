#pragma once
#include "Game-Lib/Gameplay/Attachment/Defines.h"

#include <Base/Types.h>

#include <entt/fwd.hpp>

namespace ECS::Components
{
    struct AnimationData;
    struct AttachmentData;
    struct Model;
}

namespace Model
{
    struct ComplexModel;
}

namespace Util::Attachment
{
    i16 GetAttachmentIndexFromAttachmentID(const Model::ComplexModel* modelInfo, ::Attachment::Defines::Type attachment);

    bool CanUseAttachment(const Model::ComplexModel* modelInfo, ::Attachment::Defines::Type attachment, u16& attachmentIndex);
    bool HasActiveAttachment(const Model::ComplexModel* modelInfo, ::ECS::Components::AttachmentData& attachmentData, ::Attachment::Defines::Type attachment);
    bool GetAttachmentEntity(const Model::ComplexModel* modelInfo, ::ECS::Components::AttachmentData& attachmentData, ::Attachment::Defines::Type attachment, entt::entity& entity);
    bool EnableAttachment(entt::entity parent, const ECS::Components::Model& model, ::ECS::Components::AttachmentData& attachmentData, ::Attachment::Defines::Type attachment);
    void CalculateAttachmentMatrix(const Model::ComplexModel* modelInfo, const ECS::Components::AnimationData& animationData, ::Attachment::Defines::Type attachment, ECS::Components::AttachmentInstance& attachmentInstance);
    const mat4x4* GetAttachmentMatrix(const ECS::Components::Model& model, const ECS::Components::AnimationData& animationData, ::ECS::Components::AttachmentData& attachmentData, ::Attachment::Defines::Type attachment);
}
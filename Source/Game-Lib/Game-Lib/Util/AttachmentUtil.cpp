#include "AnimationUtil.h"
#include "Game-Lib/ECS/Components/AABB.h"
#include "Game-Lib/ECS/Components/AttachmentData.h"
#include "Game-Lib/ECS/Components/Model.h"
#include "Game-Lib/ECS/Components/Name.h"
#include "Game-Lib/ECS/Util/Transforms.h"
#include "Game-Lib/Rendering/GameRenderer.h"
#include "Game-Lib/Rendering/Model/ModelLoader.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <Base/Util/StringUtils.h>

#include <FileFormat/Novus/ClientDB/Definitions.h>

#include <entt/entt.hpp>
#include "AttachmentUtil.h"

namespace Util::Attachment
{
    i16 GetAttachmentIndexFromAttachmentID(const Model::ComplexModel* modelInfo, ::Attachment::Defines::Type attachment)
    {
        u32 numAttachments = static_cast<u32>(modelInfo->attachments.size());
        if (numAttachments == 0 || attachment <= ::Attachment::Defines::Type::Invalid || attachment >= ::Attachment::Defines::Type::Count)
            return ::Attachment::Defines::InvalidAttachmentIndex;

        if (!modelInfo->attachmentIDToIndex.contains((i16)attachment))
            return ::Attachment::Defines::InvalidAttachmentIndex;

        i16 attachmentIndex = modelInfo->attachmentIDToIndex.at((i16)attachment);
        if (attachmentIndex >= static_cast<i16>(numAttachments))
            return ::Attachment::Defines::InvalidAttachmentIndex;

        return attachmentIndex;
    }
    bool CanUseAttachment(const Model::ComplexModel* modelInfo, ::ECS::Components::AttachmentData& attachmentData, ::Attachment::Defines::Type attachment, u16& attachmentIndex)
    {
        attachmentIndex = Util::Attachment::GetAttachmentIndexFromAttachmentID(modelInfo, attachment);

        bool canUseAttachment = attachmentIndex != ::Attachment::Defines::InvalidAttachmentIndex;
        return canUseAttachment;
    }

    bool HasActiveAttachment(::ECS::Components::AttachmentData& attachmentData, ::Attachment::Defines::Type attachment)
    {
        return attachmentData.attachmentToInstance.contains(attachment);
    }

    bool GetAttachmentEntity(::ECS::Components::AttachmentData& attachmentData, ::Attachment::Defines::Type attachment, entt::entity& entity)
    {
        if (!HasActiveAttachment(attachmentData, attachment))
            return false;

        ECS::Components::AttachmentInstance& attachmentInstance = attachmentData.attachmentToInstance[attachment];
        entity = attachmentInstance.entity;

        return true;
    }

    bool EnableAttachment(entt::entity parent, const ECS::Components::Model& model, ::ECS::Components::AttachmentData& attachmentData, ::Attachment::Defines::Type attachment)
    {
        ModelLoader* modelLoader = ServiceLocator::GetGameRenderer()->GetModelLoader();

        const auto* modelInfo = modelLoader->GetModelInfo(model.modelHash);
        if (!modelInfo)
            return false;

        u16 attachmentIndex = ::Attachment::Defines::InvalidAttachmentIndex;
        if (!CanUseAttachment(modelInfo, attachmentData, attachment, attachmentIndex))
            return false;

        if (HasActiveAttachment(attachmentData, attachment))
            return true;

        entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
        entt::registry::context& ctx = registry->ctx();
        auto& transformSystem = ctx.get<ECS::TransformSystem>();

        entt::entity entity = registry->create();

        auto& name = registry->emplace<ECS::Components::Name>(entity);
        name.name = ::Attachment::Defines::TypeNames[(u16)attachment];
        name.fullName = name.name;
        name.nameHash = StringUtils::fnv1a_32(name.name.c_str(), name.name.size());

        registry->emplace<ECS::Components::AABB>(entity);
        registry->emplace<ECS::Components::Transform>(entity);
        registry->emplace<ECS::Components::Model>(entity);

        transformSystem.ParentEntityTo(parent, entity);
        attachmentData.attachmentToInstance[attachment] = { entity, mat4x4(1.0f) };
        return true;
    }

    const mat4x4* GetAttachmentMatrix(::ECS::Components::AttachmentData& attachmentData, ::Attachment::Defines::Type attachment)
    {
        if (!HasActiveAttachment(attachmentData, attachment))
            return nullptr;

        const auto& attachmentInstance = attachmentData.attachmentToInstance[attachment];
        return &attachmentInstance.matrix;
    }
}

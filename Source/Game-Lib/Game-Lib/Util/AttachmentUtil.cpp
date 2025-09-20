#include "AnimationUtil.h"
#include "Game-Lib/ECS/Components/AABB.h"
#include "Game-Lib/ECS/Components/AnimationData.h"
#include "Game-Lib/ECS/Components/AttachmentData.h"
#include "Game-Lib/ECS/Components/Model.h"
#include "Game-Lib/ECS/Components/Name.h"
#include "Game-Lib/ECS/Util/Transforms.h"
#include "Game-Lib/Rendering/GameRenderer.h"
#include "Game-Lib/Rendering/Model/ModelLoader.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <Base/Util/StringUtils.h>

#include <entt/entt.hpp>
#include "AttachmentUtil.h"
#include <Game-Lib/ECS/Singletons/RenderState.h>
#include <glm/gtx/matrix_decompose.hpp>

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

    bool CanUseAttachment(const Model::ComplexModel* modelInfo, ::Attachment::Defines::Type attachment, u16& attachmentIndex)
    {
        attachmentIndex = Util::Attachment::GetAttachmentIndexFromAttachmentID(modelInfo, attachment);

        bool canUseAttachment = attachmentIndex != ::Attachment::Defines::InvalidAttachmentIndex;
        return canUseAttachment;
    }

    bool IsAttachmentActive(::ECS::Components::AttachmentData& attachmentData, ::Attachment::Defines::Type attachment)
    {
        bool isActive = attachmentData.attachmentToInstance.contains(attachment);
        return isActive;
    }

    bool HasActiveAttachment(const Model::ComplexModel* modelInfo, ::ECS::Components::AttachmentData& attachmentData, ::Attachment::Defines::Type attachment)
    {
        u16 attachmentIndex = ::Attachment::Defines::InvalidAttachmentIndex;
        return CanUseAttachment(modelInfo, attachment, attachmentIndex) && IsAttachmentActive(attachmentData, attachment);
    }

    bool GetAttachmentEntity(const Model::ComplexModel* modelInfo, ::ECS::Components::AttachmentData& attachmentData, ::Attachment::Defines::Type attachment, entt::entity& entity)
    {
        if (!HasActiveAttachment(modelInfo, attachmentData, attachment))
            return false;

        ECS::Components::AttachmentInstance& attachmentInstance = attachmentData.attachmentToInstance[attachment];
        entity = attachmentInstance.entity;

        return true;
    }

    bool EnableAttachment(entt::entity parent, const ECS::Components::Model& model, ::ECS::Components::AttachmentData& attachmentData, ::ECS::Components::AnimationData& animationData, ::Attachment::Defines::Type attachment)
    {
        ModelLoader* modelLoader = ServiceLocator::GetGameRenderer()->GetModelLoader();

        const auto* modelInfo = modelLoader->GetModelInfo(model.modelHash);
        if (!modelInfo)
            return false;

        u16 attachmentIndex = ::Attachment::Defines::InvalidAttachmentIndex;
        if (!CanUseAttachment(modelInfo, attachment, attachmentIndex))
            return false;

        if (IsAttachmentActive(attachmentData, attachment))
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

        auto& attachmentInfo = modelInfo->attachments[attachmentIndex];
        auto& attachmentBone = animationData.boneInstances[attachmentInfo.bone];
        attachmentBone.flags.Transformed = true;

        attachmentData.attachmentToInstance[attachment] = { 0, entity, mat4x4(1.0f) };
        return true;
    }

    static glm::mat4 mul(const glm::mat4& matrix1, const glm::mat4& matrix2)
    {
        return matrix2 * matrix1;
    }

    static mat4x4 CalculateBaseAttachmentMatrix(const Model::ComplexModel::Attachment& attachment, f32 scale = 1.0f)
    {
        mat4x4 translationMatrix = glm::translate(mat4x4(1.0f), attachment.position * scale);
        mat4x4 rotationMatrix = glm::toMat4(quat(1.0f, 0.0f, 0.0f, 0.0f));
        mat4x4 scaleMatrix = glm::scale(mat4x4(1.0f), vec3(1.0f));

        mat4x4 attachmentMatrix = mat4x4(1.0f);
        attachmentMatrix = mul(translationMatrix, attachmentMatrix);
        attachmentMatrix = mul(rotationMatrix, attachmentMatrix);
        attachmentMatrix = mul(scaleMatrix, attachmentMatrix);

        return attachmentMatrix;
    }

    void CalculateAttachmentMatrix(const Model::ComplexModel* modelInfo, const ECS::Components::AnimationData& animationData, ::Attachment::Defines::Type attachment, ECS::Components::AttachmentInstance& attachmentInstance, f32 scaleMod)
    {
        entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
        ECS::Singletons::RenderState& renderState = registry->ctx().get<ECS::Singletons::RenderState>();

        if (attachmentInstance.lastUpdatedFrame == renderState.frameNumber)
            return;

        attachmentInstance.lastUpdatedFrame = renderState.frameNumber;

        u16 attachmentIndex = ::Attachment::Defines::InvalidAttachmentIndex;
        if (!::Util::Attachment::CanUseAttachment(modelInfo, attachment, attachmentIndex))
            return;

        const Model::ComplexModel::Attachment& skeletonAttachment = modelInfo->attachments[attachmentIndex];
        u32 numBones = static_cast<u32>(modelInfo->bones.size());

        u32 boneIndex = 0;
        if (skeletonAttachment.bone < numBones)
            boneIndex = skeletonAttachment.bone;

        const mat4x4& parentBoneMatrix = animationData.boneTransforms[boneIndex];
        mat4x4 attachmentMatrix = CalculateBaseAttachmentMatrix(skeletonAttachment, scaleMod);
        attachmentInstance.matrix = mul(attachmentMatrix, parentBoneMatrix);

        vec3 scale;
        quat rotation;
        vec3 translation;
        vec3 skew;
        vec4 perspective;
        if (!glm::decompose(attachmentInstance.matrix, scale, rotation, translation, skew, perspective))
            return;

        auto& transformSystem = registry->ctx().get<ECS::TransformSystem>();
        transformSystem.SetLocalTransform(attachmentInstance.entity, translation, rotation, scale);
    }

    const mat4x4* GetAttachmentMatrix(const ECS::Components::Model& model, const ECS::Components::AnimationData& animationData, ::ECS::Components::AttachmentData& attachmentData, ::Attachment::Defines::Type attachment)
    {
        ModelLoader* modelLoader = ServiceLocator::GetGameRenderer()->GetModelLoader();

        const auto* modelInfo = modelLoader->GetModelInfo(model.modelHash);
        if (!modelInfo)
            return nullptr;

        if (!HasActiveAttachment(modelInfo, attachmentData, attachment))
            return nullptr;

        entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
        ECS::Singletons::RenderState& renderState = registry->ctx().get<ECS::Singletons::RenderState>();

        auto& attachmentInstance = attachmentData.attachmentToInstance[attachment];
        CalculateAttachmentMatrix(modelInfo, animationData, attachment, attachmentInstance, model.scale);

        return &attachmentInstance.matrix;
    }
}

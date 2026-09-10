#pragma once
#include <Base/Types.h>

#include <Scripting/Defines.h>
#include <Scripting/LuaMethodTable.h>

#include <entt/fwd.hpp>

#include <string>

namespace Scripting::Asset
{
    // Backs the dev-only "Asset" Lua table: filesystem enumeration under Data/ and model
    // spawning, for the Lua Asset Browser.
    class AssetHandler : public LuaHandlerBase
    {
    public:
        void Register(Zenith* zenith);
        void Clear(Zenith* zenith);

        void PostLoad(Zenith* zenith) {}
        void Update(Zenith* zenith, f32 deltaTime) {}

        static i32 ListDir(Zenith* zenith);
        static i32 SpawnModel(Zenith* zenith);
        static i32 SpawnModelAtWorldOrigin(Zenith* zenith);
        static i32 ApplyCharacterAppearance(Zenith* zenith);
        static i32 BindRestPose(Zenith* zenith);
        static i32 BindAnimation(Zenith* zenith);
        static i32 ShareAnimationPose(Zenith* zenith);
        static i32 AnimationPlay(Zenith* zenith);
        static i32 AnimationPause(Zenith* zenith);
        static i32 AnimationStop(Zenith* zenith);
        static i32 AnimationSeek(Zenith* zenith);
        static i32 AnimationSetLooping(Zenith* zenith);
        static i32 AnimationSetPlaybackRate(Zenith* zenith);
        static i32 GetAnimationState(Zenith* zenith);
        static i32 BindAnimationController(Zenith* zenith);
        static i32 StepAnimationControllers(Zenith* zenith);
        static i32 ResolveAnimationParameter(Zenith* zenith);
        static i32 AnimationSetFloatParameter(Zenith* zenith);
        static i32 AnimationSetFloat3Parameter(Zenith* zenith);
        static i32 AnimationSetBoolParameter(Zenith* zenith);
        static i32 AnimationTriggerParameter(Zenith* zenith);
        static i32 AnimationQueueAction(Zenith* zenith);
        static i32 BindModelAttachment(Zenith* zenith);
        static i32 SetModelAttachmentTransform(Zenith* zenith);
        static i32 GetModelAttachmentPoint(Zenith* zenith);
        static i32 GetModelAttachmentLocalPoint(Zenith* zenith);
        static i32 GetModelAttachmentGripDiagnostics(Zenith* zenith);
        static i32 GetAnimationAttachmentPoint(Zenith* zenith);
        static i32 GetAnimationControllerState(Zenith* zenith);
        static i32 GetAnimationControllerGraph(Zenith* zenith);
        static i32 AnimationGraphDocument(Zenith* zenith);
        static i32 AnimationGraphEditField(Zenith* zenith);
        static i32 AnimationGraphCommand(Zenith* zenith);
        static i32 AnimationAssetDocument(Zenith* zenith);
        static i32 GetAnimationJointScreenPositions(Zenith* zenith);
        static i32 AnimationAssetEditField(Zenith* zenith);
        static i32 AnimationAssetCommand(Zenith* zenith);
        static i32 AnimationDump(Zenith* zenith);
        static i32 DestroyAnimatedModel(Zenith* zenith);
        static i32 BeginDragSpawn(Zenith* zenith);
        static i32 LoadRenderModel(Zenith* zenith);
        static i32 GetRenderAssetStats(Zenith* zenith);

        // Creates an entity for the model at `dataRelativePath` (path relative to Data/) at the
        // given world position and kicks off its load. Shared by SpawnModel and the drag-spawn
        // editor flow. Returns entt::null on failure.
        static entt::entity CreateModelAtPosition(const std::string& dataRelativePath, const vec3& position);
    };

    static LuaRegister<> assetGlobalMethods[] =
    {
        { "ListDir",        AssetHandler::ListDir,        Scripting::LuaMethodFlags::DeveloperOnly },
        { "SpawnModel",     AssetHandler::SpawnModel,     Scripting::LuaMethodFlags::DeveloperOnly },
        { "SpawnModelAtWorldOrigin", AssetHandler::SpawnModelAtWorldOrigin, Scripting::LuaMethodFlags::DeveloperOnly },
        { "ApplyCharacterAppearance", AssetHandler::ApplyCharacterAppearance, Scripting::LuaMethodFlags::DeveloperOnly },
        { "BindRestPose",    AssetHandler::BindRestPose,    Scripting::LuaMethodFlags::DeveloperOnly },
        { "BindAnimation", AssetHandler::BindAnimation, Scripting::LuaMethodFlags::DeveloperOnly },
        { "ShareAnimationPose", AssetHandler::ShareAnimationPose, Scripting::LuaMethodFlags::DeveloperOnly },
        { "AnimationPlay", AssetHandler::AnimationPlay, Scripting::LuaMethodFlags::DeveloperOnly },
        { "AnimationPause", AssetHandler::AnimationPause, Scripting::LuaMethodFlags::DeveloperOnly },
        { "AnimationStop", AssetHandler::AnimationStop, Scripting::LuaMethodFlags::DeveloperOnly },
        { "AnimationSeek", AssetHandler::AnimationSeek, Scripting::LuaMethodFlags::DeveloperOnly },
        { "AnimationSetLooping", AssetHandler::AnimationSetLooping, Scripting::LuaMethodFlags::DeveloperOnly },
        { "AnimationSetPlaybackRate", AssetHandler::AnimationSetPlaybackRate, Scripting::LuaMethodFlags::DeveloperOnly },
        { "GetAnimationState", AssetHandler::GetAnimationState, Scripting::LuaMethodFlags::DeveloperOnly },
        { "BindAnimationController", AssetHandler::BindAnimationController, Scripting::LuaMethodFlags::DeveloperOnly },
        { "StepAnimationControllers", AssetHandler::StepAnimationControllers, Scripting::LuaMethodFlags::DeveloperOnly },
        { "ResolveAnimationParameter", AssetHandler::ResolveAnimationParameter, Scripting::LuaMethodFlags::DeveloperOnly },
        { "AnimationSetFloatParameter", AssetHandler::AnimationSetFloatParameter, Scripting::LuaMethodFlags::DeveloperOnly },
        { "AnimationSetFloat3Parameter", AssetHandler::AnimationSetFloat3Parameter, Scripting::LuaMethodFlags::DeveloperOnly },
        { "AnimationSetBoolParameter", AssetHandler::AnimationSetBoolParameter, Scripting::LuaMethodFlags::DeveloperOnly },
        { "AnimationTriggerParameter", AssetHandler::AnimationTriggerParameter, Scripting::LuaMethodFlags::DeveloperOnly },
        { "AnimationQueueAction", AssetHandler::AnimationQueueAction, Scripting::LuaMethodFlags::DeveloperOnly },
        { "BindModelAttachment", AssetHandler::BindModelAttachment, Scripting::LuaMethodFlags::DeveloperOnly },
        { "SetModelAttachmentTransform", AssetHandler::SetModelAttachmentTransform, Scripting::LuaMethodFlags::DeveloperOnly },
        { "GetModelAttachmentPoint", AssetHandler::GetModelAttachmentPoint, Scripting::LuaMethodFlags::DeveloperOnly },
        { "GetModelAttachmentLocalPoint", AssetHandler::GetModelAttachmentLocalPoint, Scripting::LuaMethodFlags::DeveloperOnly },
        { "GetModelAttachmentGripDiagnostics", AssetHandler::GetModelAttachmentGripDiagnostics, Scripting::LuaMethodFlags::DeveloperOnly },
        { "GetAnimationAttachmentPoint", AssetHandler::GetAnimationAttachmentPoint, Scripting::LuaMethodFlags::DeveloperOnly },
        { "GetAnimationControllerState", AssetHandler::GetAnimationControllerState, Scripting::LuaMethodFlags::DeveloperOnly },
        { "GetAnimationControllerGraph", AssetHandler::GetAnimationControllerGraph, Scripting::LuaMethodFlags::DeveloperOnly },
        { "AnimationGraphDocument", AssetHandler::AnimationGraphDocument, Scripting::LuaMethodFlags::DeveloperOnly },
        { "AnimationGraphEditField", AssetHandler::AnimationGraphEditField, Scripting::LuaMethodFlags::DeveloperOnly },
        { "AnimationGraphCommand", AssetHandler::AnimationGraphCommand, Scripting::LuaMethodFlags::DeveloperOnly },
        { "AnimationAssetDocument", AssetHandler::AnimationAssetDocument, Scripting::LuaMethodFlags::DeveloperOnly },
        { "GetAnimationJointScreenPositions", AssetHandler::GetAnimationJointScreenPositions, Scripting::LuaMethodFlags::DeveloperOnly },
        { "AnimationAssetEditField", AssetHandler::AnimationAssetEditField, Scripting::LuaMethodFlags::DeveloperOnly },
        { "AnimationAssetCommand", AssetHandler::AnimationAssetCommand, Scripting::LuaMethodFlags::DeveloperOnly },
        { "AnimationDump", AssetHandler::AnimationDump, Scripting::LuaMethodFlags::DeveloperOnly },
        { "DestroyAnimatedModel", AssetHandler::DestroyAnimatedModel, Scripting::LuaMethodFlags::DeveloperOnly },
        { "BeginDragSpawn", AssetHandler::BeginDragSpawn, Scripting::LuaMethodFlags::DeveloperOnly },
        { "LoadRenderModel", AssetHandler::LoadRenderModel, Scripting::LuaMethodFlags::DeveloperOnly },
        { "GetRenderAssetStats", AssetHandler::GetRenderAssetStats, Scripting::LuaMethodFlags::DeveloperOnly },
    };
}

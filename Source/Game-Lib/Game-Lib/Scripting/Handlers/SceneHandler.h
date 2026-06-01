#pragma once
#include <Base/Types.h>

#include <Scripting/Defines.h>
#include <Scripting/LuaMethodTable.h>

namespace Scripting::Scene
{
    // Backs the dev-only "Scene" Lua table: entity enumeration and per-component get/set
    // for the Lua Hierarchy and Inspector. Mirrors the data the C++ Editor::Inspector reads.
    class SceneHandler : public LuaHandlerBase
    {
    public:
        void Register(Zenith* zenith);
        void Clear(Zenith* zenith) {}

        void PostLoad(Zenith* zenith) {}
        void Update(Zenith* zenith, f32 deltaTime) {}

        static i32 GetEntities(Zenith* zenith);
        static i32 CenterCameraOnEntity(Zenith* zenith);

        static i32 GetName(Zenith* zenith);
        static i32 SetName(Zenith* zenith);

        static i32 GetTransform(Zenith* zenith);
        static i32 SetTransform(Zenith* zenith);

        static i32 GetSceneNode(Zenith* zenith);

        static i32 GetAABB(Zenith* zenith);
        static i32 SetAABB(Zenith* zenith);

        static i32 GetModel(Zenith* zenith);
        static i32 SetModelVisible(Zenith* zenith);
        static i32 SetModelTransparent(Zenith* zenith);

        static i32 GetDecal(Zenith* zenith);
        static i32 SetDecal(Zenith* zenith);

        static i32 GetUnit(Zenith* zenith);
    };

    static LuaRegister<> sceneGlobalMethods[] =
    {
        { "GetEntities",          SceneHandler::GetEntities,          Scripting::LuaMethodFlags::DeveloperOnly },
        { "CenterCameraOnEntity", SceneHandler::CenterCameraOnEntity, Scripting::LuaMethodFlags::DeveloperOnly },
        { "GetName",              SceneHandler::GetName,              Scripting::LuaMethodFlags::DeveloperOnly },
        { "SetName",              SceneHandler::SetName,              Scripting::LuaMethodFlags::DeveloperOnly },
        { "GetTransform",         SceneHandler::GetTransform,         Scripting::LuaMethodFlags::DeveloperOnly },
        { "SetTransform",         SceneHandler::SetTransform,         Scripting::LuaMethodFlags::DeveloperOnly },
        { "GetSceneNode",         SceneHandler::GetSceneNode,         Scripting::LuaMethodFlags::DeveloperOnly },
        { "GetAABB",              SceneHandler::GetAABB,              Scripting::LuaMethodFlags::DeveloperOnly },
        { "SetAABB",              SceneHandler::SetAABB,              Scripting::LuaMethodFlags::DeveloperOnly },
        { "GetModel",             SceneHandler::GetModel,             Scripting::LuaMethodFlags::DeveloperOnly },
        { "SetModelVisible",      SceneHandler::SetModelVisible,      Scripting::LuaMethodFlags::DeveloperOnly },
        { "SetModelTransparent",  SceneHandler::SetModelTransparent,  Scripting::LuaMethodFlags::DeveloperOnly },
        { "GetDecal",             SceneHandler::GetDecal,             Scripting::LuaMethodFlags::DeveloperOnly },
        { "SetDecal",             SceneHandler::SetDecal,             Scripting::LuaMethodFlags::DeveloperOnly },
        { "GetUnit",              SceneHandler::GetUnit,              Scripting::LuaMethodFlags::DeveloperOnly },
    };
}

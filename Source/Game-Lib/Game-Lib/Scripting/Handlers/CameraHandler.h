#pragma once
#include <Base/Types.h>

#include <Scripting/Defines.h>
#include <Scripting/LuaMethodTable.h>

namespace Scripting::Camera
{
    class CameraHandler : public LuaHandlerBase
    {
    public:
        void Register(Zenith* zenith);
        void Clear(Zenith* zenith);

        void PostLoad(Zenith* zenith) {}
        void Update(Zenith* zenith, f32 deltaTime);

        static i32 GetPosition(Zenith* zenith);
        static i32 GetPitch(Zenith* zenith);
        static i32 GetYaw(Zenith* zenith);
        static i32 GetRoll(Zenith* zenith);
        static i32 GetForward(Zenith* zenith);
        static i32 GetRight(Zenith* zenith);
        static i32 GetUp(Zenith* zenith);
        static i32 GetFOV(Zenith* zenith);
        static i32 GetNearClip(Zenith* zenith);
        static i32 GetFarClip(Zenith* zenith);
        static i32 GetSpeed(Zenith* zenith);
        static i32 SetSpeed(Zenith* zenith);
        static i32 GetMapInfo(Zenith* zenith);
        static i32 GetSaveNames(Zenith* zenith);
        static i32 HasSave(Zenith* zenith);
        static i32 LoadSave(Zenith* zenith);
        static i32 AddSave(Zenith* zenith);
        static i32 DeleteSave(Zenith* zenith);
        static i32 GetStartupSave(Zenith* zenith);
        static i32 SetStartupSave(Zenith* zenith);
        static i32 SetOnTransformChanged(Zenith* zenith);
        static i32 SetOnSavesChanged(Zenith* zenith);

        void OnCameraTransformChanged(Zenith* zenith);
        void OnCameraSavesChanged(Zenith* zenith);

    private:
        i32 _onTransformChangedRef = LUA_NOREF;
        i32 _onSavesChangedRef = LUA_NOREF;

        // One-shot guard for auto-loading the configured startup camera save once at launch.
        bool _appliedStartupSave = false;
    };

    static LuaRegister<> cameraGlobalMethods[] =
    {
        { "GetPosition",           CameraHandler::GetPosition },
        { "GetPitch",              CameraHandler::GetPitch },
        { "GetYaw",                CameraHandler::GetYaw },
        { "GetRoll",               CameraHandler::GetRoll },
        { "GetForward",            CameraHandler::GetForward },
        { "GetRight",              CameraHandler::GetRight },
        { "GetUp",                 CameraHandler::GetUp },
        { "GetFOV",                CameraHandler::GetFOV },
        { "GetNearClip",           CameraHandler::GetNearClip },
        { "GetFarClip",            CameraHandler::GetFarClip },
        { "GetSpeed",              CameraHandler::GetSpeed },
        { "SetSpeed",              CameraHandler::SetSpeed,             Scripting::LuaMethodFlags::DeveloperOnly },
        { "GetMapInfo",            CameraHandler::GetMapInfo },
        { "GetSaveNames",          CameraHandler::GetSaveNames },
        { "HasSave",               CameraHandler::HasSave },
        { "LoadSave",              CameraHandler::LoadSave,             Scripting::LuaMethodFlags::DeveloperOnly },
        { "AddSave",               CameraHandler::AddSave,              Scripting::LuaMethodFlags::DeveloperOnly },
        { "DeleteSave",            CameraHandler::DeleteSave,           Scripting::LuaMethodFlags::DeveloperOnly },
        { "GetStartupSave",        CameraHandler::GetStartupSave,       Scripting::LuaMethodFlags::DeveloperOnly },
        { "SetStartupSave",        CameraHandler::SetStartupSave,       Scripting::LuaMethodFlags::DeveloperOnly },
        { "SetOnTransformChanged", CameraHandler::SetOnTransformChanged },
        { "SetOnSavesChanged",     CameraHandler::SetOnSavesChanged },
    };
}

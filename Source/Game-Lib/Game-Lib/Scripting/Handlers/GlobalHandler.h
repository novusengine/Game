#pragma once
#include <Base/Types.h>

#include <Scripting/Defines.h>
#include <Scripting/LuaMethodTable.h>

namespace Scripting
{
    class GlobalHandler : public LuaHandlerBase
    {
    private:
        void Register(Zenith* zenith);
        void Clear(Zenith* zenith) {}

        void PostLoad(Zenith* zenith);
        void Update(Zenith* zenith, f32 deltaTime);

    public: // Registered Functions
        static i32 AddCursor(Zenith* zenith);
        static i32 SetCursor(Zenith* zenith);
        static i32 GetCurrentMap(Zenith* zenith);
        static i32 LoadMap(Zenith* zenith);
        static i32 GetMapLoadingProgress(Zenith* zenith);
        static i32 EquipItem(Zenith* zenith);
        static i32 UnEquipItem(Zenith* zenith);
        static i32 GetEquippedItem(Zenith* zenith);
        static i32 ExecCmd(Zenith* zenith);
        static i32 SendChatMessage(Zenith* zenith);
    };

    static LuaRegister<> globalMethods[] =
    {
        { "AddCursor",		        GlobalHandler::AddCursor },
        { "SetCursor",		        GlobalHandler::SetCursor },
        { "GetCurrentMap",	        GlobalHandler::GetCurrentMap },
        { "LoadMap",		        GlobalHandler::LoadMap },
        { "GetMapLoadingProgress",  GlobalHandler::GetMapLoadingProgress },
        { "EquipItem",		        GlobalHandler::EquipItem },
        { "UnEquipItem",	        GlobalHandler::UnEquipItem },
        { "GetEquippedItem",        GlobalHandler::GetEquippedItem },
        { "ExecCmd",                GlobalHandler::ExecCmd },
        { "SendChatMessage",        GlobalHandler::SendChatMessage }
    };
}
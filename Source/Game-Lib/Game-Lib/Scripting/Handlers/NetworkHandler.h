#pragma once
#include <Base/Types.h>

#include <Scripting/Defines.h>
#include <Scripting/LuaMethodTable.h>

namespace Scripting::Network
{
    class NetworkHandler : public LuaHandlerBase
    {
    public:
        void Register(Zenith* zenith);
        void Clear(Zenith* zenith) {}

        void PostLoad(Zenith* zenith) {}
        void Update(Zenith* zenith, f32 deltaTime) {}

        static i32 IsConnected(Zenith* zenith);
        static i32 GetPingInfo(Zenith* zenith);
        static i32 Connect(Zenith* zenith);
        static i32 Disconnect(Zenith* zenith);

        static i32 GetConnectIP(Zenith* zenith);
        static i32 SetConnectIP(Zenith* zenith);
        static i32 GetAccountName(Zenith* zenith);
        static i32 SetAccountName(Zenith* zenith);
        static i32 GetDrawTargetAABB(Zenith* zenith);
        static i32 SetDrawTargetAABB(Zenith* zenith);

        static i32 GetCharacterInfo(Zenith* zenith);
        static i32 GetMoverUnit(Zenith* zenith);
        static i32 GetTargetUnit(Zenith* zenith);
    };

    static LuaRegister<> networkGlobalMethods[] =
    {
        { "IsConnected",        NetworkHandler::IsConnected,       Scripting::LuaMethodFlags::DeveloperOnly },
        { "GetPingInfo",        NetworkHandler::GetPingInfo,       Scripting::LuaMethodFlags::DeveloperOnly },
        { "Connect",            NetworkHandler::Connect,           Scripting::LuaMethodFlags::DeveloperOnly },
        { "Disconnect",         NetworkHandler::Disconnect,        Scripting::LuaMethodFlags::DeveloperOnly },
        { "GetConnectIP",       NetworkHandler::GetConnectIP,      Scripting::LuaMethodFlags::DeveloperOnly },
        { "SetConnectIP",       NetworkHandler::SetConnectIP,      Scripting::LuaMethodFlags::DeveloperOnly },
        { "GetAccountName",     NetworkHandler::GetAccountName,    Scripting::LuaMethodFlags::DeveloperOnly },
        { "SetAccountName",     NetworkHandler::SetAccountName,    Scripting::LuaMethodFlags::DeveloperOnly },
        { "GetDrawTargetAABB",  NetworkHandler::GetDrawTargetAABB, Scripting::LuaMethodFlags::DeveloperOnly },
        { "SetDrawTargetAABB",  NetworkHandler::SetDrawTargetAABB, Scripting::LuaMethodFlags::DeveloperOnly },
        { "GetCharacterInfo",   NetworkHandler::GetCharacterInfo,  Scripting::LuaMethodFlags::DeveloperOnly },
        { "GetMoverUnit",       NetworkHandler::GetMoverUnit,      Scripting::LuaMethodFlags::DeveloperOnly },
        { "GetTargetUnit",      NetworkHandler::GetTargetUnit,     Scripting::LuaMethodFlags::DeveloperOnly },
    };
}

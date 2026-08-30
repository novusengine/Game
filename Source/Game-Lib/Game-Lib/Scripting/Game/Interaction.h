#pragma once

#include <Base/Types.h>

#include <Scripting/LuaMethodTable.h>

namespace Scripting::Game
{
    struct Interaction
    {
    public:
        static void Register(Zenith* zenith);
    };

    namespace InteractionMethods
    {
        i32 GetVersion(Zenith* zenith);
        i32 GetState(Zenith* zenith);
        i32 Open(Zenith* zenith);
        i32 Select(Zenith* zenith);
        i32 Purchase(Zenith* zenith);
        i32 ToggleReward(Zenith* zenith);
        i32 CompleteQuest(Zenith* zenith);
        i32 AbandonQuest(Zenith* zenith);
        i32 Close(Zenith* zenith);
    }

    static LuaRegister<> interactionGlobalFunctions[] =
    {
        { "GetVersion", InteractionMethods::GetVersion },
        { "GetState", InteractionMethods::GetState },
        { "Open", InteractionMethods::Open },
        { "Select", InteractionMethods::Select },
        { "Purchase", InteractionMethods::Purchase },
        { "ToggleReward", InteractionMethods::ToggleReward },
        { "CompleteQuest", InteractionMethods::CompleteQuest },
        { "AbandonQuest", InteractionMethods::AbandonQuest },
        { "Close", InteractionMethods::Close }
    };
}

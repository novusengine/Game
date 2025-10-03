#include "EventHandler.h"

#include <Meta/Generated/Game/LuaEvent.h>
#include <Meta/Generated/Shared/PacketList.h>

#include <Scripting/LuaManager.h>
#include <Scripting/Zenith.h>

#include <lualib.h>

namespace Scripting
{
    void EventHandler::Register(Zenith* zenith)
    {
        // Register Functions
        {
            LuaMethodTable::Set(zenith, EventHandlerGlobalMethods);
        }

        CreateEventTables(zenith);
    }

    i32 EventHandler::RegisterEventHandler(Zenith* zenith)
    {
        u32 numArgs = zenith->GetTop();
        if (numArgs != 2)
            return 0;

        u32 packedEventID = zenith->CheckVal<u32>(1);
        if (packedEventID == std::numeric_limits<u16>().max())
            return 0;

        u32 variant = 0;
        i32 funcRef = 0;

        if (numArgs == 2)
        {
            if (zenith->IsFunction(2))
                funcRef = zenith->GetRef(2);
        }
        else
        {
            variant = zenith->CheckVal<u32>(2);

            if (zenith->IsFunction(3))
                funcRef = zenith->GetRef(3);
        }

        if (funcRef == 0)
            return 0;

        u16 eventTypeID = static_cast<u16>(packedEventID >> 16);
        u16 eventID = static_cast<u16>(packedEventID & 0xFFFF);
        u16 variantID = static_cast<u16>(variant);

        zenith->RegisterEventCallbackRaw(eventTypeID, eventID, variantID, funcRef);

        return 0;
    }

    void EventHandler::CreateEventTables(Zenith* zenith)
    {
        zenith->RegisterEventType<Generated::LuaGameEventEnum>();
        zenith->RegisterEventTypeID<Generated::LuaGameEventDataLoaded>(Generated::LuaGameEventEnum::Loaded);
        zenith->RegisterEventTypeID<Generated::LuaGameEventDataUpdated>(Generated::LuaGameEventEnum::Updated);
        zenith->RegisterEventTypeID<Generated::LuaGameEventDataMapLoading>(Generated::LuaGameEventEnum::MapLoading);
        zenith->RegisterEventTypeID<Generated::LuaGameEventDataChatMessageReceived>(Generated::LuaGameEventEnum::ChatMessageReceived);
        zenith->RegisterEventTypeID<Generated::LuaGameEventDataLocalMoverChanged>(Generated::LuaGameEventEnum::LocalMoverChanged);

        zenith->RegisterEventType<Generated::LuaUnitEventEnum>();
        zenith->RegisterEventTypeID<Generated::LuaUnitEventDataAdd>(Generated::LuaUnitEventEnum::Add);
        zenith->RegisterEventTypeID<Generated::LuaUnitEventDataRemove>(Generated::LuaUnitEventEnum::Remove);
        zenith->RegisterEventTypeID<Generated::LuaUnitEventDataTargetChanged>(Generated::LuaUnitEventEnum::TargetChanged);
        zenith->RegisterEventTypeID<Generated::LuaUnitEventDataPowerUpdate>(Generated::LuaUnitEventEnum::PowerUpdate);
        zenith->RegisterEventTypeID<Generated::LuaUnitEventDataResistanceUpdate>(Generated::LuaUnitEventEnum::ResistanceUpdate);
        zenith->RegisterEventTypeID<Generated::LuaUnitEventDataStatUpdate>(Generated::LuaUnitEventEnum::StatUpdate);
        zenith->RegisterEventTypeID<Generated::LuaUnitEventDataAuraAdd>(Generated::LuaUnitEventEnum::AuraAdd);
        zenith->RegisterEventTypeID<Generated::LuaUnitEventDataAuraUpdate>(Generated::LuaUnitEventEnum::AuraUpdate);
        zenith->RegisterEventTypeID<Generated::LuaUnitEventDataAuraRemove>(Generated::LuaUnitEventEnum::AuraRemove);

        zenith->RegisterEventType<Generated::LuaContainerEventEnum>();
        zenith->RegisterEventTypeID<Generated::LuaContainerEventDataAdd>(Generated::LuaContainerEventEnum::Add);
        zenith->RegisterEventTypeID<Generated::LuaContainerEventDataAddToSlot>(Generated::LuaContainerEventEnum::AddToSlot);
        zenith->RegisterEventTypeID<Generated::LuaContainerEventDataRemoveFromSlot>(Generated::LuaContainerEventEnum::RemoveFromSlot);
        zenith->RegisterEventTypeID<Generated::LuaContainerEventDataSwapSlots>(Generated::LuaContainerEventEnum::SwapSlots);

        zenith->RegisterEventType<Generated::LuaTriggerEventEnum>();
        zenith->RegisterEventTypeID<Generated::LuaTriggerEventDataOnEnter>(Generated::LuaTriggerEventEnum::OnEnter);
        zenith->RegisterEventTypeID<Generated::LuaTriggerEventDataOnExit>(Generated::LuaTriggerEventEnum::OnExit);
        zenith->RegisterEventTypeID<Generated::LuaTriggerEventDataOnStay>(Generated::LuaTriggerEventEnum::OnStay);

        zenith->RegisterEventType<Generated::PacketListEnum>();
        for (const auto& pair : Generated::PacketListEnumMeta::EnumList)
        {
            zenith->RegisterEventTypeIDRaw(Generated::PacketListEnumMeta::EnumID, pair.second, 0);
        }
    }
}

#include "EventHandler.h"

#include <MetaGen/EnumTraits.h>
#include <MetaGen/PacketList.h>
#include <MetaGen/Game/Lua/Lua.h>

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
        zenith->RegisterEventType<MetaGen::Game::Lua::GameEvent>();
        zenith->RegisterEventTypeID<MetaGen::Game::Lua::GameEventDataLoaded>(MetaGen::Game::Lua::GameEvent::Loaded);
        zenith->RegisterEventTypeID<MetaGen::Game::Lua::GameEventDataUpdated>(MetaGen::Game::Lua::GameEvent::Updated);
        zenith->RegisterEventTypeID<MetaGen::Game::Lua::GameEventDataCharacterListChanged>(MetaGen::Game::Lua::GameEvent::CharacterListChanged);
        zenith->RegisterEventTypeID<MetaGen::Game::Lua::GameEventDataMapLoading>(MetaGen::Game::Lua::GameEvent::MapLoading);
        zenith->RegisterEventTypeID<MetaGen::Game::Lua::GameEventDataChatMessageReceived>(MetaGen::Game::Lua::GameEvent::ChatMessageReceived);
        zenith->RegisterEventTypeID<MetaGen::Game::Lua::GameEventDataLocalMoverChanged>(MetaGen::Game::Lua::GameEvent::LocalMoverChanged);

        zenith->RegisterEventType<MetaGen::Game::Lua::UnitEvent>();
        zenith->RegisterEventTypeID<MetaGen::Game::Lua::UnitEventDataAdd>(MetaGen::Game::Lua::UnitEvent::Add);
        zenith->RegisterEventTypeID<MetaGen::Game::Lua::UnitEventDataRemove>(MetaGen::Game::Lua::UnitEvent::Remove);
        zenith->RegisterEventTypeID<MetaGen::Game::Lua::UnitEventDataTargetChanged>(MetaGen::Game::Lua::UnitEvent::TargetChanged);
        zenith->RegisterEventTypeID<MetaGen::Game::Lua::UnitEventDataPowerUpdate>(MetaGen::Game::Lua::UnitEvent::PowerUpdate);
        zenith->RegisterEventTypeID<MetaGen::Game::Lua::UnitEventDataResistanceUpdate>(MetaGen::Game::Lua::UnitEvent::ResistanceUpdate);
        zenith->RegisterEventTypeID<MetaGen::Game::Lua::UnitEventDataStatUpdate>(MetaGen::Game::Lua::UnitEvent::StatUpdate);
        zenith->RegisterEventTypeID<MetaGen::Game::Lua::UnitEventDataAuraAdd>(MetaGen::Game::Lua::UnitEvent::AuraAdd);
        zenith->RegisterEventTypeID<MetaGen::Game::Lua::UnitEventDataAuraUpdate>(MetaGen::Game::Lua::UnitEvent::AuraUpdate);
        zenith->RegisterEventTypeID<MetaGen::Game::Lua::UnitEventDataAuraRemove>(MetaGen::Game::Lua::UnitEvent::AuraRemove);

        zenith->RegisterEventType<MetaGen::Game::Lua::ContainerEvent>();
        zenith->RegisterEventTypeID<MetaGen::Game::Lua::ContainerEventDataAdd>(MetaGen::Game::Lua::ContainerEvent::Add);
        zenith->RegisterEventTypeID<MetaGen::Game::Lua::ContainerEventDataAddToSlot>(MetaGen::Game::Lua::ContainerEvent::AddToSlot);
        zenith->RegisterEventTypeID<MetaGen::Game::Lua::ContainerEventDataRemoveFromSlot>(MetaGen::Game::Lua::ContainerEvent::RemoveFromSlot);
        zenith->RegisterEventTypeID<MetaGen::Game::Lua::ContainerEventDataSwapSlots>(MetaGen::Game::Lua::ContainerEvent::SwapSlots);

        zenith->RegisterEventType<MetaGen::Game::Lua::TriggerEvent>();
        zenith->RegisterEventTypeID<MetaGen::Game::Lua::TriggerEventDataOnEnter>(MetaGen::Game::Lua::TriggerEvent::OnEnter);
        zenith->RegisterEventTypeID<MetaGen::Game::Lua::TriggerEventDataOnExit>(MetaGen::Game::Lua::TriggerEvent::OnExit);
        zenith->RegisterEventTypeID<MetaGen::Game::Lua::TriggerEventDataOnStay>(MetaGen::Game::Lua::TriggerEvent::OnStay);

        zenith->RegisterEventType<MetaGen::PacketListEnum>();
        for (const auto& pair : MetaGen::PacketListEnumMeta::ENUM_FIELD_LIST)
        {
            zenith->RegisterEventTypeIDRaw(MetaGen::PacketListEnumMeta::ENUM_ID, pair.second, 0);
        }
    }
}

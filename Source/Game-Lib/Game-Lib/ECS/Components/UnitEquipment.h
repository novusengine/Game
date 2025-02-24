#pragma once
#include "Game-Lib/Gameplay/Database/Item.h"

#include <Base/Types.h>

#include <entt/fwd.hpp>

#include <robinhood/robinhood.h>

namespace ECS
{
    namespace Components
    {
        struct UnitEquipment
        {
        public:
            std::array<u32, (u32)Database::Item::ItemEquipSlot::Count> equipmentSlotToItemID;
            robin_hood::unordered_set<Database::Item::ItemEquipSlot> dirtyEquipmentSlots;
        };

        struct UnitEquipmentDirty { };
    }
}
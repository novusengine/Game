#pragma once
#include <Base/Types.h>
#include <Base/Util/DebugHandler.h>

#include <Gameplay/GameDefine.h>

namespace ECS::Components
{
    struct Container
    {
    public:
        GameDefine::ObjectGuid GetItem(u8 slotIndex) const
        {
            u8 numSlots = this->numSlots;
#if defined(NC_DEBUG)
            NC_ASSERT(slotIndex < numSlots, "Container::GetItem - Called with slotIndex ({0}) when the container can only hold {1} items", slotIndex, numSlots);
#endif

            if (slotIndex >= numSlots)
                return GameDefine::ObjectGuid::Empty;

            return items[slotIndex];
        }

        bool AddToSlot(u8 slotIndex, GameDefine::ObjectGuid item)
        {
            u8 numSlots = this->numSlots;
#if defined(NC_DEBUG)
            NC_ASSERT(slotIndex < numSlots, "Container::AddToSlot - Called with slotIndex ({0}) when the container can only hold {1} items", slotIndex, numSlots);
#endif

            if (slotIndex >= numSlots)
                return false;

            if (!items[slotIndex].IsValid())
                --numFreeSlots;

            items[slotIndex] = item;
            return true;
        }

        bool RemoveFromSlot(u8 slotIndex)
        {
            u8 numSlots = this->numSlots;
#if defined(NC_DEBUG)
            NC_ASSERT(slotIndex < numSlots, "Container::RemoveFromSlot - Called with slotIndex ({0}) when the container can only hold {1} items", slotIndex, numSlots);
#endif

            if (slotIndex >= numSlots)
                return false;

            if (items[slotIndex].IsValid())
                ++numFreeSlots;

            items[slotIndex] = GameDefine::ObjectGuid::Empty;
            return true;
        }

        bool SwapSlots(Container& destContainer, u8 srcSlotIndex, u8 destSlotIndex)
        {
            u8 numSrcSlots = this->numSlots;
            u8 numDestSlots = destContainer.numSlots;

#if defined(NC_DEBUG)
            NC_ASSERT(srcSlotIndex < numSrcSlots, "Container::SwapItems - Called with srcSlotIndex ({0}) when the container can only hold {1} items", srcSlotIndex, numSrcSlots);
            NC_ASSERT(destSlotIndex < numDestSlots, "Container::SwapItems - Called with destSlotIndex ({0}) when the container can only hold {1} items", destSlotIndex, numDestSlots);
#endif
            if (srcSlotIndex >= numSrcSlots || destSlotIndex >= numDestSlots)
                return false;

            bool srcSlotEmpty = !items[srcSlotIndex].IsValid();
            bool destSlotEmpty = !destContainer.items[destSlotIndex].IsValid();
            std::swap(items[srcSlotIndex], destContainer.items[destSlotIndex]);

            if (srcSlotEmpty && !destSlotEmpty)
            {
                --numFreeSlots;
                ++destContainer.numFreeSlots;
            }
            else if (!srcSlotEmpty && destSlotEmpty)
            {
                ++numFreeSlots;
                --destContainer.numFreeSlots;
            }

            return true;
        }

    public:
        u32 itemID = 0;
        u8 numSlots = 0;
        u8 numFreeSlots = 0;
        std::vector<GameDefine::ObjectGuid> items;
    };
}
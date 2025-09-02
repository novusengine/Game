#pragma once
#include <Base/Types.h>
#include <Base/Util/DebugHandler.h>

#include <Gameplay/GameDefine.h>

namespace ECS::Components
{
    struct Container
    {
    public:
        ObjectGUID GetItem(u16 slotIndex) const
        {
            u16 numSlots = this->numSlots;
#if defined(NC_DEBUG)
            NC_ASSERT(slotIndex < numSlots, "Container::GetItem - Called with slotIndex ({0}) when the container can only hold {1} items", slotIndex, numSlots);
#endif

            if (slotIndex >= numSlots)
                return ObjectGUID::Empty;

            return items[slotIndex];
        }

        bool AddToSlot(u16 slotIndex, ObjectGUID guid)
        {
            u16 numSlots = this->numSlots;
#if defined(NC_DEBUG)
            NC_ASSERT(slotIndex < numSlots, "Container::AddToSlot - Called with slotIndex ({0}) when the container can only hold {1} items", slotIndex, numSlots);
#endif

            if (slotIndex >= numSlots)
                return false;

            if (!items[slotIndex].IsValid())
                --numFreeSlots;

            items[slotIndex] = guid;
            return true;
        }

        bool RemoveFromSlot(u16 slotIndex)
        {
            u16 numSlots = this->numSlots;
#if defined(NC_DEBUG)
            NC_ASSERT(slotIndex < numSlots, "Container::RemoveFromSlot - Called with slotIndex ({0}) when the container can only hold {1} items", slotIndex, numSlots);
#endif

            if (slotIndex >= numSlots)
                return false;

            if (items[slotIndex].IsValid())
                ++numFreeSlots;

            items[slotIndex] = ObjectGUID::Empty;
            return true;
        }

        bool SwapSlots(Container& destContainer, u16 srcSlotIndex, u16 destSlotIndex)
        {
            u16 numSrcSlots = this->numSlots;
            u16 numDestSlots = destContainer.numSlots;

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
        u16 numSlots = 0;
        u16 numFreeSlots = 0;
        std::vector<ObjectGUID> items;
    };
}
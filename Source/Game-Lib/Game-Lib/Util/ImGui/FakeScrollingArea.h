#pragma once
#include <Base/Types.h>

/*
This little class just handle all items that aren't visible in a scrolling area
all the remaining zone need to be handled by hand, you can look bellow for the template
Items in the scrolling area need to be all the same.

// itemSize: is the calculated size of a single item
// totalItems: is the total items in the scrolling area

FakeScrollingArea scrollingArea(itemSize, totalItems);
if (scrollingArea.Before())
{
    auto firstItem = scrollingArea.GetFirstVisibleItem();
    auto lastItem = scrollingArea.GetLastVisibleItem();

    // do your stuff here

    scrollingArea.After();
}
*/

class FakeScrollingArea
{
public:
    FakeScrollingArea(vec2 itemSize, i32 totalItems)
        : _itemSize(itemSize)
        , _totalItems(totalItems)
        , _beginIsHandled(false)
    {}

    [[nodiscard]] bool Begin();
    void End();

    [[nodiscard]] i32 GetFirstVisibleItem() const;
    [[nodiscard]] i32 GetLastVisibleItem() const;

private:
    bool _beginIsHandled;

    vec2 _itemSize;
    i32 _totalItems;

    i32 _totalRows = -1;
    i32 _firstVisibleItem = -1;
    i32 _lastVisibleItem = -1;
    i32 _lastVisibleRow = -1;
};


#pragma once
#include <imgui/imgui.h>

#include <vector>
#include <iostream>
#include <functional>

/*
 		This little class just handle all items that aren't visible in a scrolling area
		all the remaining zone need to be handled by hand, you can look bellow for the template
	 	Items in the scrolling area need to be all the same.

	~~ TEMPLATE ~~

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

 	~~ END TEMPLATE ~~

*/

class FakeScrollingArea
{
public:
    FakeScrollingArea(ImVec2 itemSize, i32 totalItems)
        : _itemSize(itemSize), _totalItems(totalItems),
          _beforeIsHandled(false)
    {}

    [[nodiscard]] bool Before();
    void After();

    [[nodiscard]] i32 GetFirstVisibleItem() const { return _firstVisibleItem; }
    [[nodiscard]] i32 GetLastVisibleItem() const { return _lastVisibleItem; }

private:
    bool _beforeIsHandled;

    ImVec2 _itemSize;
    i32 _totalItems;

    i32 _totalRows = -1;
    i32 _firstVisibleItem = -1;
    i32 _lastVisibleItem = -1;
    i32 _lastVisibleRow = -1;
};


#include "FakeScrollingArea.h"

#include <Base/Util/DebugHandler.h>

#include <imgui/imgui.h>

bool FakeScrollingArea::Begin()
{
    const f32 windowWidth = ImGui::GetWindowContentRegionWidth();
    const i32 itemsPerRow = static_cast<i32>(windowWidth / _itemSize.x);
    _totalRows = (_totalItems + itemsPerRow - 1) / itemsPerRow;

    f32 scrollPosition = ImGui::GetScrollY();
    f32 viewportHeight = ImGui::GetWindowHeight();

    i32 firstVisibleRow = static_cast<i32>(scrollPosition / _itemSize.y);
    _lastVisibleRow = static_cast<i32>((scrollPosition + viewportHeight) / _itemSize.y);

    firstVisibleRow = std::max(0, firstVisibleRow - 1);
    _lastVisibleRow = std::min(_totalRows - 1, _lastVisibleRow);

    _firstVisibleItem = firstVisibleRow * itemsPerRow;
    _lastVisibleItem = std::min(_totalItems - 1, (_lastVisibleRow + 1) * itemsPerRow - 1);

    if (_firstVisibleItem > 0)
    {
        ImGui::Dummy(ImVec2(0, (f32)(firstVisibleRow) * _itemSize.y));
    }

    if (_firstVisibleItem <= _totalItems)
        _beginIsHandled = true;

    return _beginIsHandled;
}

void FakeScrollingArea::End()
{
    if (!_beginIsHandled)
        return;

    if (_lastVisibleItem < _totalItems - 1)
    {
        i32 remainingRows = _totalRows - _lastVisibleRow - 1;
        ImGui::Dummy(ImVec2(0, (f32)(remainingRows) * _itemSize.y));
    }
    else
    {
        // little padding at the end
        ImGui::Dummy(ImVec2(0, _itemSize.y * 2.f));
    }
}

i32 FakeScrollingArea::GetFirstVisibleItem() const
{
    DebugHandler::Assert(_beginIsHandled, "FakeScrollingArea::GetFirstVisibleItem() called before Begin()");

    return _firstVisibleItem;
}

i32 FakeScrollingArea::GetLastVisibleItem() const
{
    DebugHandler::Assert(_beginIsHandled, "FakeScrollingArea::GetLastVisibleItem() called before Begin()");

    return _lastVisibleItem;
}

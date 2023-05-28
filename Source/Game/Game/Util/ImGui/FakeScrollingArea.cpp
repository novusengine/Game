#include "FakeScrollingArea.h"

bool FakeScrollingArea::Before()
{
    const float windowWidth = ImGui::GetWindowContentRegionWidth();
    const int itemsPerRow = static_cast<int>(windowWidth / _itemSize.x);
    _totalRows = (_totalItems + itemsPerRow - 1) / itemsPerRow;

    float scrollPosition = ImGui::GetScrollY();
    float viewportHeight = ImGui::GetWindowHeight();

    int firstVisibleRow = static_cast<int>(scrollPosition / _itemSize.y);
    _lastVisibleRow = static_cast<int>((scrollPosition + viewportHeight) / _itemSize.y);

    firstVisibleRow = std::max(0, firstVisibleRow);
    _lastVisibleRow = std::min(_totalRows - 1, _lastVisibleRow);

    _firstVisibleItem = firstVisibleRow * itemsPerRow;
    _lastVisibleItem = std::min(_totalItems - 1, (_lastVisibleRow + 1) * itemsPerRow - 1);

    if (_firstVisibleItem > 0)
    {
        ImGui::Dummy(ImVec2(0, (float)firstVisibleRow * _itemSize.y));
    }

    if (_firstVisibleItem <= _totalItems)
        _beforeIsHandled = true;

    return _beforeIsHandled;
}

void FakeScrollingArea::After()
{
    if (!_beforeIsHandled)
        return;

    if (_lastVisibleItem < _totalItems - 1)
    {
        int remainingRows = _totalRows - _lastVisibleRow - 1;
        ImGui::Dummy(ImVec2(0, (float)(remainingRows + 2) * _itemSize.y));
    }
}
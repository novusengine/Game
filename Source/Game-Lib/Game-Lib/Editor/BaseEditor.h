#pragma once
#include <Base/Types.h>

namespace Editor
{
    enum BaseEditorFlags : u32
    {
        BaseEditorFlags_None = 0,
        BaseEditorFlags_DefaultVisible = 1 << 0,
        BaseEditorFlags_HideInMenuBar = 1 << 1,
        BaseEditorFlags_EditorOnly = 1 << 2
    };
    typedef std::underlying_type_t<BaseEditorFlags> BaseEditorFlags_t;

    class BaseEditor
    {
    public:
        BaseEditor(const char* name, BaseEditorFlags_t flags = BaseEditorFlags_None);

        void UpdateVisibility();

        virtual const char* GetName() = 0;
        virtual void Show();
        virtual void Hide();

        virtual void Update(f32 deltaTime) {};
        virtual void OnModeUpdate(bool mode) {};

        virtual void BeginImGui() {};

        virtual void DrawImGuiMenuBar() {};
        virtual void DrawImGuiSubMenuBar() {};
        virtual void DrawImGui() {};

        virtual void EndImGui() {};

        bool OpenMenu(const char* title);
        void CloseMenu();

        bool OpenRightClickMenu();
        bool IsMouseInsideWindow();

        bool IsHiddenInMenuBar();
        bool IsEditorOnly();
        bool IsHorizontal();
        bool& IsVisible();
        void SetIsVisible(bool isVisible);
        void Reset();

    protected:
        bool _isVisible;
        bool _lastIsVisible;
        BaseEditorFlags_t _flags;
    };
}
#pragma once
#include <Base/Types.h>
#include <Input/InputManager.h>

namespace Editor
{
    class BaseEditor;
    class Viewport;
    class Inspector;
    class Hierarchy;
    class AssetBrowser;
    class ActionStackEditor;

    class EditorHandler
    {
    public:
        EditorHandler();

        void NewFrame();
        void Update(f32 deltaTime);

        void BeginImGui();
        void BeginEditor();

        void DrawImGuiMenuBar(f32 deltaTime);
        void DrawImGui();

        void EndEditor();
        void EndImGui();

        Viewport* GetViewport() { return _viewport; }
        ActionStackEditor* GetActionStackEditor() { return _actionStackEditor; }
        Inspector* GetInspector() { return _inspector; }

    private:
        void ResetLayoutToDefault();

        void LoadLayouts();
        void SaveLayout();
        void RestoreLayout();
        
    private:
        bool _editorMode = false;
        std::vector<BaseEditor*> _editors;

        Viewport* _viewport = nullptr;
        Inspector* _inspector = nullptr;
        Hierarchy* _hierarchy = nullptr;
        AssetBrowser* _assetBrowser = nullptr;
        ActionStackEditor* _actionStackEditor = nullptr;
        
        const f32 LAYOUT_SAVE_INTERVAL = 5.0f;
        f32 _timeSinceLayoutSave = 0.0f;
        std::string _gameLayout = "";
        std::string _editorLayout = "";
        bool _loadedStartupLayout = false;

        u32 _mainDockID;
    };
}
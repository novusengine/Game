#pragma once
#include <Base/Types.h>
#include <Input/InputManager.h>

namespace Editor
{
    class BaseEditor;
    class Viewport;
    class Inspector;
    class SceneHierarchy;
    class ActionStackEditor;

    class EditorHandler
    {
    public:
        EditorHandler();

        void Update(f32 deltaTime);

        void BeginImGui();
        void BeginEditor();

        void DrawImGuiMenuBar(f32 deltaTime);
        void DrawImGui();

        void EndEditor();
        void EndImGui();

        ActionStackEditor* GetActionStackEditor() { return _actionStackEditor; }
        Inspector* GetInspector() { return _inspector; }

    private:
        void ResetLayout();
        
    private:
        bool _editorMode = false;
        std::vector<BaseEditor*> _editors;

        ActionStackEditor* _actionStackEditor;
        Inspector* _inspector;

        u32 _mainDockID;
    };
}
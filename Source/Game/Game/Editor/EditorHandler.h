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

    private:
        void ResetLayout();
        
    private:
        bool _editorMode = false;
        std::vector<BaseEditor*> _editors;

        u32 _mainDockID;
    };
}
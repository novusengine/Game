#pragma once
#include "BaseEditor.h"

#include <Audio/AudioManager.h>

#include <Game-Lib/Rendering/GameRenderer.h>
#include <Game-Lib/Util/ServiceLocator.h>

#include <entt/fwd.hpp>
#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include <imgui/ImGuiNotify.hpp>

#include <filesystem>
#include <utility>
#include <map>

namespace fs = std::filesystem;

namespace Editor
{
    class AssetBrowser : public BaseEditor
    {
        constexpr static const ImVec2 _defaultDisplaySize = ImVec2(80.f, 80.f);
        constexpr static const f32 _minDisplayScale = 0.5f;
        constexpr static const f32 _maxDisplaySize = 2.5f;
        constexpr static const f32 _defaultDisplayValue = 1.f;

        struct TreeNode
        {
            fs::path FolderPath;
            std::vector<TreeNode*> Folders;
            std::vector<fs::path> Files;

            explicit TreeNode(fs::path path)
                : FolderPath(std::move(path))
            {}

            ~TreeNode()
            {
                for (auto& folder : Folders)
                    delete folder;
            }

            [[nodiscard]] std::string ProcessPath(const std::string& topPath) const
            {
                return ("." + FolderPath.string().substr(topPath.size()));
            }
        };

        struct [[maybe_unused]] FileDragInformation
        {
            fs::path path;
        };

    public:
        AssetBrowser();

        virtual const char *GetName() override
        {
            return "Asset Browser";
        }

        virtual void DrawImGui() override;

    private:
        virtual void OnModeUpdate(bool mode) override;

        void CalculateAverageFontWidth();
        void ProcessTree(TreeNode* sourceProcess);
        void BuildTreeView(TreeNode* node, bool expand = false);

        void DrawFolderSection(f32 heightConstraint);
        void DrawFileSection(f32 heightConstraint);

        void RowFileMode(const fs::path& item);
        void DisplayFileMode(ImTextureID imageTexture, ImVec2 size, f32 scale, const fs::path& item, f32 fontWidth);
        void ProcessDisplayRendering(const fs::path& item, i32 i);

        static void CalculateAspectRatio(ImVec2& size, ImVec2 ref);
        static bool CanDisplayMore(ImVec2 size);

        static bool IsTexture(const std::string& ref);
        static std::string EllipsisText(const char* ref, f32 width, f32 fontWidth);
        void ProcessFileSearch(const std::string& search);

        // Handle action on item
        void ItemClicked(const fs::path& item);
        void ItemHovered(const fs::path& item);

    private:
        AudioManager* _audioManager = nullptr;

        GameRenderer* _gameRenderer = nullptr;
        Renderer::Renderer* _renderer = nullptr;

        TreeNode* _tree = nullptr;
        TreeNode* _currentNode = nullptr;
        bool _needToResetScroll = false;

        fs::path _topPath;
        fs::path _modelTopPath;
        std::string _searchedString;
        std::vector<fs::path> _searchedFiles;

        std::map<int, void*> _images;
        std::map<int, vec2> _imagesSize;

        void* _defaultImageHandle = nullptr;
        void* _currentImage = nullptr;
        vec2 _currentSize;

        f32 _averageFontWidth = -1.f;
    };
}
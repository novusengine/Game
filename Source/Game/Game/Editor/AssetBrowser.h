#pragma once
#include "BaseEditor.h"

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include <imgui/imgui_notify.h>

#include <filesystem>
#include <utility>

#include <Game/Util/ServiceLocator.h>
#include <Game/Rendering/GameRenderer.h>

namespace fs = std::filesystem;

namespace Editor
{
    class AssetBrowser : public BaseEditor
    {
        constexpr static const ImVec2 _defaultDisplaySize = ImVec2(80.f, 80.f);
        constexpr static const float _minDisplayScale = 0.5f;
        constexpr static const float _maxDisplaySize = 2.5f;
        constexpr static const float _defaultDisplayValue = 1.f;

        struct TreeNode
        {
            fs::path FolderPath;
            std::vector<TreeNode *> Folders;
            std::vector<fs::path> Files;

            explicit TreeNode(fs::path path)
                : FolderPath(std::move(path))
            {}

            ~TreeNode()
            {
                for (auto &folder : Folders)
                    delete folder;
            }

            [[nodiscard]] std::string ProcessPath(const std::string &topPath) const
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
        { return "Asset Browser"; }

        virtual void DrawImGui() override;

    private:
        void CalculateAverageFontWidth();
        void ProcessTree(TreeNode *sourceProcess);
        void BuildTreeView(TreeNode *node, bool expand = false);

        void DrawFolderSection(float heightConstraint);
        void DrawFileSection(float heightConstraint);

        void RowFileMode(const fs::path &item);
        void DisplayFileMode(ImTextureID imageTexture, ImVec2 size, float scale, const fs::path &item, float fontWidth);
        void ProcessDisplayRendering(const fs::path &item, i32 i);

        static void CalculateAspectRatio(ImVec2 &size, ImVec2 ref);
        static bool CanDisplayMore(ImVec2 size);

        static bool SearchString(const std::string &ref, const std::string &key, bool insensitive);
        static bool IsTexture(const std::string &ref);
        static std::string EllipsisText(const char *ref, float width, float fontWidth);
        void ProcessFileSearch(const std::string &search);

        // Handle action on item
        void ItemClicked(const fs::path &item);
        void ItemHovered(const fs::path &item);

    private:
        GameRenderer *_gameRenderer = nullptr;
        Renderer::Renderer *_renderer = nullptr;

        TreeNode *_tree{};
        TreeNode *_currentNode = nullptr;
        bool _needToResetScroll = false;

        fs::path _topPath;
        std::string _searchedString;
        std::vector<fs::path> _searchedFiles;

        std::map<int, void *> _images;
        std::map<int, ImVec2> _imagesSize;

        void *_defaultImageHandle = nullptr;
        void *_currentImage = nullptr;
        ImVec2 _currentSize;

        float _averageFontWidth = -1.f;
    };
}
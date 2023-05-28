#include "AssetBrowser.h"

#include <Base/Util/DebugHandler.h>
#include <Game/Util/ImGui/FakeScrollingArea.h>

namespace Editor
{
    AssetBrowser::AssetBrowser()
        : BaseEditor(GetName(), true),
          _tree(nullptr), _currentNode(nullptr)
    {
        fs::path relativeDataPath = fs::absolute("Data");
        if (!fs::is_directory(relativeDataPath))
        {
            DebugHandler::PrintError("Failed to find Data/ folder from ({0})", relativeDataPath.string());
            return;
        }

        _topPath = relativeDataPath;
        _tree = new TreeNode(_topPath);
        ProcessTree(_tree);
        _currentNode = _tree;
    }

    void AssetBrowser::DrawImGui()
    {
        _gameRenderer = ServiceLocator::GetGameRenderer();
        _renderer = _gameRenderer->GetRenderer();

        if (_averageFontWidth < 0.f)
            CalculateAverageFontWidth();

        if (ImGui::Begin(GetName()))
        {

            if (!_defaultImageHandle)
            {
                Renderer::TextureDesc textureDesc;
                textureDesc.path = "Data/Texture/interface/icons/ability_gouge.dds"; // need to have a default image ??
                auto textureID = _renderer->LoadTexture(textureDesc);
                _defaultImageHandle = _renderer->GetImguiImageHandle(textureID);
            }

            ImGui::Text("Actual data folder : %s", _topPath.string().c_str());
            if (_currentNode)
            {
                if (IsHorizontal())
                    ImGui::SameLine();

                ImGui::Text("%s", _currentNode->ProcessPath(_topPath.string()).c_str());
            }
            ImGui::Separator();

            // docked from bottom / top / default
            if (IsHorizontal())
            {
                f32 maxHeight = ImGui::GetContentRegionAvail().y * 0.9f;
                ImGui::Columns(2, "AssetBrowserColumn", true);
                ImGui::SetColumnWidth(0, ImGui::GetWindowWidth() * 0.25f);
                ImGui::SetColumnWidth(1, ImGui::GetWindowWidth() * 0.75f);
                DrawFolderSection(maxHeight);
                ImGui::NextColumn();
                DrawFileSection(maxHeight);
                ImGui::Columns(1);
            }
            else
            {
                f32 maxFolderHeight = ImGui::GetContentRegionAvail().y * 0.3f;
                f32 maxFileHeight = ImGui::GetContentRegionAvail().y * 0.6f;
                DrawFolderSection(maxFolderHeight);
                ImGui::Separator();
                DrawFileSection(maxFileHeight);
            }
        }
        ImGui::End();
    }

    void AssetBrowser::DrawFolderSection(float heightConstraint)
    {
        ImGui::Text("Directory");

        ImGui::SetNextWindowSizeConstraints(ImVec2(-1, -1), ImVec2(-1, heightConstraint));
        if (ImGui::BeginChild(
            "##DirectoryScrolling", ImVec2(0, heightConstraint), true, ImGuiWindowFlags_HorizontalScrollbar))
        {
            BuildTreeView(_tree, true);
            ImGui::EndChild();
        }
    }

    void AssetBrowser::DrawFileSection(float heightConstraint)
    {
        if (_currentNode)
        {
            if (_needToResetScroll)
            {
                _needToResetScroll = false;
                ImGui::SetScrollY(0.f);
            }

            static bool useFileRow = true;
            if (ImGui::RadioButton("Row", useFileRow))
            {
                useFileRow = true;
            }
            ImGui::SameLine();
            if (ImGui::RadioButton("Display", !useFileRow))
            {
                useFileRow = false;
            }
            ImGui::SameLine();

            ImGuiSliderFlags_ sliderFlag = ImGuiSliderFlags_None;
            if (useFileRow)
            {
                ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.3f);
                sliderFlag = ImGuiSliderFlags_NoInput;
            }

            static float displaySize = _defaultDisplayValue;
            ImGui::PushItemWidth(150.f);
            ImGui::SliderFloat("##", &displaySize, _minDisplayScale, _maxDisplaySize, "Size x%.2f", sliderFlag);
            ImGui::PopItemWidth();
            ImVec2 realDisplaySize(_defaultDisplaySize.x * displaySize, _defaultDisplaySize.y * displaySize);

            if (useFileRow)
                ImGui::PopStyleVar();

            if (IsHorizontal())
                ImGui::SameLine();

            static char search[16];
            ImGui::PushItemWidth(350.f);
            ImGui::InputText("Search File", search, sizeof(search));
            ImGui::PopItemWidth();

            ImGui::SetNextWindowSizeConstraints(ImVec2(-1, -1), ImVec2(-1, heightConstraint));
            if (ImGui::BeginChild(
                "##FileScrolling", ImVec2(0, heightConstraint), true, ImGuiWindowFlags_HorizontalScrollbar))
            {
                if (!_currentNode->Files.empty())
                {
                    ProcessFileSearch(search);

                    if (useFileRow)
                    {
                        auto itemSize = ImVec2(
                            ImGui::GetWindowContentRegionWidth(),
                            ImGui::GetStyle().FramePadding.y * 2 + ImGui::GetTextLineHeightWithSpacing());

                        FakeScrollingArea scrollingArea(itemSize, (int)_searchedFiles.size());
                        if (scrollingArea.Before())
                        {
                            auto firstItem = scrollingArea.GetFirstVisibleItem();
                            auto lastItem = scrollingArea.GetLastVisibleItem();

                            auto ite = _searchedFiles.begin();
                            std::advance(ite, firstItem);

                            for (int i = firstItem; i <= lastItem; i++, ite++)
                            {
                                RowFileMode(*ite);
                            }

                            scrollingArea.After();
                        }
                    }
                    else
                    {
                        auto itemSize = ImVec2(
                            realDisplaySize.x,
                            (realDisplaySize.y + ImGui::CalcTextSize("DUMMY").y) * 1.1f);

                        FakeScrollingArea scrollingArea(itemSize, (int)_searchedFiles.size());
                        if (scrollingArea.Before())
                        {
                            auto firstItem = scrollingArea.GetFirstVisibleItem();
                            auto lastItem = scrollingArea.GetLastVisibleItem();

                            auto ite = _searchedFiles.begin();
                            std::advance(ite, firstItem);

                            for (int i = firstItem; i <= lastItem; i++, ite++)
                            {
                                const fs::path &item = *ite;
                                ProcessDisplayRendering(item, i);
                                DisplayFileMode(_currentImage, _currentSize, displaySize, item, _averageFontWidth);
                            }

                            scrollingArea.After();
                        }
                    }
                }

                ImGui::EndChild();
            }
        }
    }

    void AssetBrowser::ItemClicked(const fs::path &/*item*/)
    {

    }

    void AssetBrowser::ItemHovered(const fs::path &/*item*/)
    {

    }

    void AssetBrowser::CalculateAverageFontWidth()
    {
        const ImWchar *glyphRanges = ImGui::GetIO().Fonts->GetGlyphRangesDefault();

        float totalWidth = 0.0f;
        int glyphCount = 0;

        ImFont *font = ImGui::GetFont();
        for (int i = 0; i < 65536; i++)
        {
            if (glyphRanges[i / 64] & (1 << (i % 64)))
            {
                const ImFontGlyph *glyph = font->FindGlyph((ImWchar)i);
                if (glyph)
                {
                    totalWidth += glyph->AdvanceX;
                    glyphCount++;
                }
            }
        }

        _averageFontWidth = (glyphCount > 0) ? (totalWidth / (float)glyphCount) : 1.f;
    }

    void AssetBrowser::ProcessTree(TreeNode *sourceProcess)
    {
        if (sourceProcess->FolderPath.empty())
            return;

        for (const auto &item : fs::directory_iterator(sourceProcess->FolderPath))
        {
            const auto &itemPath = item.path();
            if (fs::is_directory(itemPath))
            {
                auto *Child = new TreeNode(itemPath);
                sourceProcess->Folders.push_back(Child);
                ProcessTree(Child);
            }
            else
            {
                sourceProcess->Files.push_back(itemPath);
            }
        }
    }

    void AssetBrowser::BuildTreeView(TreeNode *node, bool expand)
    {
        std::string nodeName = node->FolderPath.filename().string();
        // display how many files a folder have
        if (!node->Files.empty())
        {
            nodeName += " - (" + std::to_string(node->Files.size()) + ")";
        }

        ImGuiTreeNodeFlags_ nodeFlags = ImGuiTreeNodeFlags_OpenOnArrow;
        if (expand) nodeFlags = static_cast<ImGuiTreeNodeFlags_>(nodeFlags | ImGuiTreeNodeFlags_DefaultOpen);
        if (node->Folders.empty()) nodeFlags = static_cast<ImGuiTreeNodeFlags_>(nodeFlags | ImGuiTreeNodeFlags_Leaf);

        if (ImGui::TreeNodeEx(nodeName.c_str(), nodeFlags))
        {
            if (ImGui::IsItemClicked() && !node->Files.empty())
            {
                _currentNode = node;
                _needToResetScroll = true;

                _images.clear();
                _imagesSize.clear();
            }

            for (const auto &folder : node->Folders)
            {
                BuildTreeView(folder);
            }
            ImGui::TreePop();
        }
    }

    bool AssetBrowser::IsHorizontal()
    {
        return (ImGui::GetWindowWidth() >= ImGui::GetWindowHeight());
    }

    bool AssetBrowser::CanDisplayMore(ImVec2 size)
    {
        float posX = ImGui::GetCursorPosX();
        float availableX = ImGui::GetWindowContentRegionWidth();

        return ((availableX - posX) > size.x);
    }

    void AssetBrowser::RowFileMode(const fs::path &item)
    {
        if (ImGui::Button(item.filename().string().c_str()))
        {
            ItemClicked(item);
        }
        if (ImGui::IsItemHovered())
        {
            ItemHovered(item);
        }
    }

    void
    AssetBrowser::DisplayFileMode(ImTextureID imageTexture, ImVec2 size, float scale, const fs::path &item, float fontWidth)
    {
        const ImVec2 defaultSize(_defaultDisplaySize.x * scale, _defaultDisplaySize.y * scale); // need to be a square !
        ImGui::BeginGroup();

        auto itemName = item.filename().string();
        auto ellipsedText = EllipsisText(itemName.c_str(), defaultSize.x, fontWidth);
        float textHeight = ImGui::CalcTextSize(ellipsedText.c_str()).y;
        ImVec2 buttonSize = ImVec2(defaultSize.x * 1.1f, (defaultSize.y + textHeight) * 1.1f);

        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
        auto cursorPos = ImGui::GetCursorPos();
        if (ImGui::Button("##", buttonSize)) // background button
        {
            ItemClicked(item);
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("%s", itemName.c_str());
            ItemHovered(item);
        }
        auto endCursorPos = ImGui::GetCursorPos();
        ImGui::PopStyleVar();

        CalculateAspectRatio(size, defaultSize);

        ImVec2 imagePos = ImVec2((buttonSize.x - size.x) * 0.5f + cursorPos.x,
            ((buttonSize.y - textHeight) - size.y) * 0.5f + cursorPos.y);
        ImGui::SetCursorPos(imagePos);
        ImGui::Image(imageTexture, size);
        ImGui::SetCursorPosX((buttonSize.x - defaultSize.x) * 0.5f + cursorPos.x);
        ImGui::SetCursorPosY(defaultSize.y + cursorPos.y + ImGui::GetStyle().FramePadding.y * 2.5f * scale);
        ImGui::Text("%s", ellipsedText.c_str());

        ImGui::EndGroup();

        ImGui::SetCursorPos(endCursorPos);
        ImGui::SameLine();
        if (!CanDisplayMore(buttonSize))
            ImGui::NewLine();
    }

    void AssetBrowser::ProcessDisplayRendering(const fs::path &item, i32 i)
    {
        auto ext = item.extension().string();

        if (IsTexture(ext))
        {
            if (!_images[i])
            {
                Renderer::TextureDesc textureDesc;
                textureDesc.path = item.string();
                auto textureID = _renderer->LoadTexture(textureDesc);
                _images[i] = _renderer->GetImguiImageHandle(textureID);
                _imagesSize[i] = ImVec2((float)_renderer->GetTextureWidth(textureID),
                    (float)_renderer->GetTextureHeight(textureID));
            }

            _currentImage = _images[i];
            _currentSize = _imagesSize[i];
            return;
        }

        _currentImage = _defaultImageHandle;
        _currentSize = _defaultDisplaySize;
    }

    void AssetBrowser::CalculateAspectRatio(ImVec2 &size, const ImVec2 ref)
    {
        bool inverse = true;
        float max = size.y;
        float min = size.x;

        if (size.x >= size.y)
        {
            max = size.x;
            min = size.y;
            inverse = false;
        }

        float ratio = max / ref.x;

        // if the texture is too small,
        // the resize ratio is blocked at x2.5
        // to not make it too blurry
        if (ratio < 0.4f)
            ratio = 0.4f;

        max /= ratio;
        min /= ratio;

        if (inverse)
        {
            size.x = min;
            size.y = max;
        }
        else
        {
            size.x = max;
            size.y = min;
        }
    }

    bool AssetBrowser::SearchString(const std::string &ref, const std::string &key, bool insensitive)
    {
        std::string str = ref;
        std::string substr = key;

        if (insensitive)
        {
            std::transform(
                str.begin(), str.end(), str.begin(), [](unsigned char c)
                { return std::tolower(c); });
            std::transform(
                substr.begin(), substr.end(), substr.begin(), [](unsigned char c)
                { return std::tolower(c); });
        }

        return (str.find(substr) != std::string::npos);
    }

    bool AssetBrowser::IsTexture(const std::string &ref)
    {
        return (ref == ".dds");
    }

    std::string AssetBrowser::EllipsisText(const char *ref, float width, float fontWidth)
    {
        ImVec2 textSize = ImGui::CalcTextSize(ref);
        if (textSize.x > width)
        {
            float ellipsisWidth = ImGui::CalcTextSize("...").x;
            int maxCharacters = static_cast<int>((width - ellipsisWidth) / (fontWidth * 1.1f));
            return (std::string(ref).substr(0, maxCharacters - 1) + "...");
        }

        return ref;
    }

    void AssetBrowser::ProcessFileSearch(const std::string &search)
    {
        if (_searchedString != search)
        {
            _images.clear();
            _imagesSize.clear();

            if (search.size() > _searchedString.size() && !_searchedFiles.empty())
            {
                const auto temp = _searchedFiles;
                _searchedFiles.clear();
                _searchedFiles.shrink_to_fit();
                _searchedFiles.reserve(temp.size());

                _searchedString = search;
                for (auto &file : temp)
                {
                    if (SearchString(file.filename().string(), _searchedString, true))
                    {
                        _searchedFiles.emplace_back(file);
                    }
                }
            }
            else
            {
                _searchedFiles.clear();
                _searchedFiles.shrink_to_fit();
                _searchedFiles.reserve(_currentNode->Files.size());

                _searchedString = search;
                for (auto &file : _currentNode->Files)
                {
                    if (SearchString(file.filename().string(), _searchedString, true))
                    {
                        _searchedFiles.emplace_back(file);
                    }
                }
            }
        }

        if (_searchedString.empty())
            _searchedFiles = _currentNode->Files;
    }
}
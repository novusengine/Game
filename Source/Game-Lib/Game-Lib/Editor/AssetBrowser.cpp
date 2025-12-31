#include "AssetBrowser.h"

#include "Game-Lib/ECS/Singletons/ActiveCamera.h"
#include "Game-Lib/ECS/Util/Transforms.h"
#include "Game-Lib/Rendering/Model/ModelLoader.h"
#include "Game-Lib/Util/ImGui/FakeScrollingArea.h"

#include <Base/Util/DebugHandler.h>
#include <Base/Util/StringUtils.h>

#include <FileFormat/Novus/Model/ComplexModel.h>

#include <entt/entt.hpp>
#include <Game-Lib/ECS/Components/Name.h>
#include <Game-Lib/ECS/Components/Model.h>

namespace Editor
{
    AssetBrowser::AssetBrowser()
        : BaseEditor(GetName(), BaseEditorFlags_DefaultVisible | BaseEditorFlags_EditorOnly),
          _tree(nullptr),
          _currentNode(nullptr)
    {
        fs::path relativeDataPath = fs::absolute("Data");
        if (!fs::is_directory(relativeDataPath))
        {
            std::string pathStr = relativeDataPath.string();
            NC_LOG_ERROR("Failed to find Data/ folder from ({0})", pathStr);
            return;
        }

        _topPath = relativeDataPath;
        _modelTopPath = _topPath / "ComplexModel";

        _tree = new TreeNode(_topPath);
        ProcessTree(_tree);
        _currentNode = _tree;

        _gameRenderer = ServiceLocator::GetGameRenderer();
        _renderer = _gameRenderer->GetRenderer();

        Renderer::TextureDesc textureDesc;
        textureDesc.path = "Data/Texture/interface/icons/ability_gouge.dds";
        Renderer::TextureID textureID = _renderer->LoadTexture(textureDesc);
        _defaultImageHandle = _renderer->GetImguiTextureID(textureID);
    }

    void AssetBrowser::DrawImGui()
    {
        if (ImGui::Begin(GetName(), &IsVisible()))
        {
            if (_averageFontWidth < 0.f)
                CalculateAverageFontWidth();

            ImGui::Text("Actual data folder : %s", _topPath.string().c_str());
            if (_currentNode)
            {
                if (IsHorizontal())
                    ImGui::SameLine();

                ImGui::Text("%s", _currentNode->ProcessPath(_topPath.string()).c_str());
            }
            ImGui::Separator();

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

    void AssetBrowser::DrawFolderSection(f32 heightConstraint)
    {
        ImGui::Text("Directory");

        ImGui::SetNextWindowSizeConstraints(ImVec2(-1.f, -1.f), ImVec2(-1, heightConstraint));
        if (ImGui::BeginChild("##DirectoryScrolling", ImVec2(0, heightConstraint), true, ImGuiWindowFlags_HorizontalScrollbar))
        {
            BuildTreeView(_tree, true);
            ImGui::EndChild();
        }
    }

    void AssetBrowser::DrawFileSection(f32 heightConstraint)
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

            static f32 displaySize = _defaultDisplayValue;
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
            if (ImGui::BeginChild("##FileScrolling", ImVec2(0, heightConstraint), true, ImGuiWindowFlags_HorizontalScrollbar))
            {
                if (!_currentNode->Files.empty())
                {
                    ProcessFileSearch(search);

                    if (useFileRow)
                    {
                       
                        ImVec2 itemSize(ImGui::GetWindowContentRegionMax().x,
                            ImGui::GetStyle().FramePadding.y * 2 + ImGui::GetTextLineHeightWithSpacing());

                        FakeScrollingArea scrollingArea(itemSize, static_cast<i32>(_searchedFiles.size()));
                        if (scrollingArea.Begin())
                        {
                            i32 firstItem = scrollingArea.GetFirstVisibleItem();
                            i32 lastItem = scrollingArea.GetLastVisibleItem();

                            auto ite = _searchedFiles.begin();
                            std::advance(ite, firstItem);

                            for (i32 i = firstItem; i <= lastItem; i++, ite++)
                            {
                                RowFileMode(*ite);
                            }

                            scrollingArea.End();
                        }
                    }
                    else
                    {
                        ImVec2 itemSize(realDisplaySize.x, (realDisplaySize.y + ImGui::CalcTextSize("DUMMY").y) * 1.1f);
                        FakeScrollingArea scrollingArea(itemSize, static_cast<i32>(_searchedFiles.size()));
                        if (scrollingArea.Begin())
                        {
                            i32 firstItem = scrollingArea.GetFirstVisibleItem();
                            i32 lastItem = scrollingArea.GetLastVisibleItem();

                            auto ite = _searchedFiles.begin();
                            std::advance(ite, firstItem);

                            for (i32 i = firstItem; i <= lastItem; i++, ite++)
                            {
                                const fs::path &item = *ite;
                                ProcessDisplayRendering(item, i);
                                DisplayFileMode(_currentImage, _currentSize, displaySize, item, _averageFontWidth);
                            }

                            scrollingArea.End();
                        }
                    }
                }

                ImGui::EndChild();
            }
        }
    }

    void AssetBrowser::ItemClicked(const fs::path& /*item*/)
    {
    }

    void AssetBrowser::ItemHovered(const fs::path& item)
    {
        if (item.extension() == Model::FILE_EXTENSION)
        {
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Right))
            {
                // Spawn model at camera position

                EnttRegistries* registries = ServiceLocator::GetEnttRegistries();
                entt::registry& registry = *registries->gameRegistry;
                entt::registry::context& ctx = registry.ctx();

                auto& tSystem = ECS::TransformSystem::Get(registry);
                auto& activeCamera = ctx.get<ECS::Singletons::ActiveCamera>();

                if (activeCamera.entity == entt::null)
                    return;

                auto& cameraTransform = registry.get<ECS::Components::Transform>(activeCamera.entity);

                entt::entity entity = registry.create();
                registry.emplace<ECS::Components::Name>(entity);
                auto& model = registry.emplace<ECS::Components::Model>(entity);
                registry.emplace<ECS::Components::AABB>(entity);
                registry.emplace<ECS::Components::Transform>(entity);

                tSystem.SetLocalTransform(entity, cameraTransform.GetWorldPosition(), quat(1.0f, 0.0f, 0.0f, 0.0f), vec3(1.0f, 1.0f, 1.0f));

                std::string relativePath = fs::relative(item, _modelTopPath).string();
                std::replace(relativePath.begin(), relativePath.end(), '\\', '/');

                u32 modelPathHash = _gameRenderer->GetModelLoader()->GetModelHashFromModelPath(relativePath);
                _gameRenderer->GetModelLoader()->LoadModelForEntity(entity, model, modelPathHash);
            }
        }
    }

    void AssetBrowser::OnModeUpdate(bool mode)
    {
        SetIsVisible(mode);
    }

    void AssetBrowser::CalculateAverageFontWidth()
    {
        const ImWchar* glyphRanges = ImGui::GetIO().Fonts->GetGlyphRangesDefault();

        f32 totalWidth = 0.0f;
        i32 glyphCount = 0;

        ImFont* font = ImGui::GetFont();
        for (i32 i = 0; i < 65536; i++)
        {
            if (glyphRanges[i / 64] & (1 << (i % 64)))
            {
                const ImFontGlyph* glyph = font->FindGlyph(static_cast<ImWchar>(i));
                if (glyph)
                {
                    totalWidth += glyph->AdvanceX;
                    glyphCount++;
                }
            }
        }

        _averageFontWidth = (glyphCount > 0) ? (totalWidth / static_cast<f32>(glyphCount)) : 1.f;
    }

    void AssetBrowser::ProcessTree(TreeNode* sourceProcess)
    {
        if (sourceProcess->FolderPath.empty())
            return;

        for (const auto& item : fs::directory_iterator(sourceProcess->FolderPath))
        {
            const auto& itemPath = item.path();
            if (fs::is_directory(itemPath))
            {
                auto* child = new TreeNode(itemPath);
                sourceProcess->Folders.push_back(child);
                ProcessTree(child);
            }
            else
            {
                sourceProcess->Files.push_back(itemPath);
            }
        }
    }

    void AssetBrowser::BuildTreeView(TreeNode* node, bool expand)
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

            for (const auto& folder : node->Folders)
            {
                BuildTreeView(folder);
            }
            ImGui::TreePop();
        }
    }

    bool AssetBrowser::CanDisplayMore(ImVec2 size)
    {
        f32 posX = ImGui::GetCursorPosX();
        f32 availableX = ImGui::GetWindowContentRegionMax().x;

        return ((availableX - posX) > size.x);
    }

    void AssetBrowser::RowFileMode(const fs::path& item)
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

    void AssetBrowser::DisplayFileMode(ImTextureID imageTexture, ImVec2 size, f32 scale, const fs::path& item, f32 fontWidth)
    {
        const ImVec2 defaultSize(_defaultDisplaySize.x * scale, _defaultDisplaySize.y * scale); // need to be a square !
        std::string itemName = item.filename().string();
        std::string ellipsedText = EllipsisText(itemName.c_str(), defaultSize.x, fontWidth);
        f32 textHeight = ImGui::CalcTextSize(ellipsedText.c_str()).y;
        ImVec2 buttonSize = ImVec2(defaultSize.x * 1.1f, (defaultSize.y + textHeight) * 1.1f);
        ImVec2 cursorPos, endCursorPos;

        ImGui::BeginGroup();
        {
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
            cursorPos = ImGui::GetCursorPos();
            if (ImGui::Button("##", buttonSize)) // background button
            {
                ItemClicked(item);
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("%s", itemName.c_str());
                ItemHovered(item);
            }
            endCursorPos = ImGui::GetCursorPos();
            ImGui::PopStyleVar();

            CalculateAspectRatio(size, defaultSize);

            ImVec2 imagePos = ImVec2((buttonSize.x - size.x) * 0.5f + cursorPos.x,
                ((buttonSize.y - textHeight) - size.y) * 0.5f + cursorPos.y);

            ImGui::SetCursorPos(imagePos);
            ImGui::Image(imageTexture, size);
            ImGui::SetCursorPosX((buttonSize.x - defaultSize.x) * 0.5f + cursorPos.x);
            ImGui::SetCursorPosY(defaultSize.y + cursorPos.y + ImGui::GetStyle().FramePadding.y * 2.5f * scale);
            ImGui::Text("%s", ellipsedText.c_str());
        }
        ImGui::EndGroup();

        ImGui::SetCursorPos(endCursorPos);
        ImGui::SameLine();
        if (!CanDisplayMore(buttonSize))
            ImGui::NewLine();
    }

    void AssetBrowser::ProcessDisplayRendering(const fs::path& item, i32 i)
    {
        std::string extension = item.extension().string();

        if (IsTexture(extension))
        {
            if (!_images[i])
            {
                Renderer::TextureDesc textureDesc;
                textureDesc.path = item.string();
                Renderer::TextureID textureID = _renderer->LoadTexture(textureDesc);
                _images[i] = _renderer->GetImguiTextureID(textureID);

                Renderer::TextureBaseDesc textureBaseDesc = _renderer->GetDesc(textureID);

                _imagesSize[i] = ImVec2(static_cast<f32>(textureBaseDesc.width),
                    static_cast<f32>(textureBaseDesc.height));
            }

            _currentImage = _images[i];
            _currentSize = _imagesSize[i];
            return;
        }

        _currentImage = _defaultImageHandle;
        _currentSize = _defaultDisplaySize;
    }

    void AssetBrowser::CalculateAspectRatio(ImVec2& size, const ImVec2 ref)
    {
        bool inverse = true;
        f32 max = size.y;
        f32 min = size.x;

        if (size.x >= size.y)
        {
            max = size.x;
            min = size.y;
            inverse = false;
        }

        f32 ratio = max / ref.x;

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

    bool AssetBrowser::IsTexture(const std::string& ref)
    {
        return StringUtils::SearchString(ref, ".dds", true);
    }

    std::string AssetBrowser::EllipsisText(const char* ref, f32 width, f32 fontWidth)
    {
        ImVec2 textSize = ImGui::CalcTextSize(ref);
        if (textSize.x > width)
        {
            f32 ellipsisWidth = ImGui::CalcTextSize("...").x;
            int maxCharacters = static_cast<int>((width - ellipsisWidth) / (fontWidth * 1.1f));
            return (std::string(ref).substr(0, maxCharacters - 1) + "...");
        }

        return ref;
    }

    void AssetBrowser::ProcessFileSearch(const std::string& search)
    {
        if (_searchedString != search)
        {
            _images.clear();
            _imagesSize.clear();

            if (search.size() > _searchedString.size() && !_searchedFiles.empty())
            {
                const std::vector<fs::path>& temp = _searchedFiles;
                _searchedFiles.clear();
                _searchedFiles.reserve(temp.size());

                _searchedString = search;
                for (const fs::path& file : temp)
                {
                    if (StringUtils::SearchString(file.filename().string(), _searchedString, true))
                    {
                        _searchedFiles.emplace_back(file);
                    }
                }
            }
            else
            {
                _searchedFiles.clear();
                _searchedFiles.reserve(_currentNode->Files.size());

                _searchedString = search;
                for (auto& file : _currentNode->Files)
                {
                    if (StringUtils::SearchString(file.filename().string(), _searchedString, true))
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
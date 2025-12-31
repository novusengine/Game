#include "GameConsole.h"
#include "GameConsoleCommandHandler.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <imgui/imgui.h>
#include <imgui/backends/imgui_impl_vulkan.h>
#include <imgui/backends/imgui_impl_glfw.h>
#include <imgui/misc/cpp/imgui_stdlib.h>
#include <imgui/implot.h>

#include <Input/InputManager.h>
#include <Renderer/RenderSettings.h>

#include <GLFW/glfw3.h>
#include <imgui_internal.h>
#include <tracy/Tracy.hpp>

AutoCVar_Int CVAR_GameConsoleEnabled(CVarCategory::Client, "consoleEnabled", "enable game console", 1, CVarFlags::Hidden | CVarFlags::DoNotSave);
AutoCVar_Int CVAR_GameConsoleDuplicateToTerminal(CVarCategory::Client, "consoleDuplicateToTerminal", "enable printing to terminal", 1, CVarFlags::EditCheckbox);

GameConsole::GameConsole()
{
    _lines.push_back("Welcome to Game Console. You may enter 'help' for more information about usage.");

    InputManager* inputManager = ServiceLocator::GetInputManager();
    KeybindGroup* keybindGroup = inputManager->GetKeybindGroupByHash("Debug"_h);
    keybindGroup->AddKeyboardCallback("Enable Game Console", GLFW_KEY_BACKSLASH, KeybindAction::Release, KeybindModifier::Any, [this](i32 key, KeybindAction action, KeybindModifier modifier) -> bool
    {
        Toggle();
        return true;
    });

    Disable();
    _commandHandler = new GameConsoleCommandHandler();
    _commandHistory.reserve(CommandHistoryMaxSize);
}

GameConsole::~GameConsole()
{
    delete _commandHandler;
}

bool MatchesCommand(const std::string& command, const std::string& _searchText)
{
    if (_searchText.empty())
        return true; // Show all if empty

    // Case-insensitive comparison
    auto caseInsensitiveFind = [](const std::string& str, const std::string& sub) -> bool
    {
        return std::search(str.begin(), str.end(), sub.begin(), sub.end(),
            [](char ch1, char ch2) { return std::tolower(ch1) == std::tolower(ch2); }) != str.end();
    };

    // Prioritize prefix match
    if (command.size() >= _searchText.size() &&
        std::equal(_searchText.begin(), _searchText.end(), command.begin(),
            [](char ch1, char ch2) { return std::tolower(ch1) == std::tolower(ch2); }))
    {
        return true;
    }

    // Fallback to case-insensitive substring match
    return caseInsensitiveFind(command, _searchText);
}

void GameConsole::Render(f32 deltaTime)
{
    ZoneScoped;
    i32* isGameConsoleEnabled = CVAR_GameConsoleEnabled.GetPtr();
    if (*isGameConsoleEnabled == 0)
        return;

    _visibleProgressTimer = glm::min(_visibleProgressTimer + (5.0f * deltaTime), 1.0f);

    std::string lineToAppend;
    while (_linesToAppend.try_dequeue(lineToAppend))
    {
        _lines.push_back(lineToAppend);
    }

    f32 height = glm::mix(0.0f, 300.f, _visibleProgressTimer);

    const ImGuiViewport* viewport = ImGui::GetMainViewport();

    ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x, viewport->Pos.y));
    ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x, height));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.14f, 0.14f, 0.14f, 0.85f));
    if (ImGui::Begin("GameConsole", reinterpret_cast<bool*>(isGameConsoleEnabled), ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove))
    {
        ImGuiWindow* gameConsoleWindow = ImGui::GetCurrentWindow();

        if (ImGui::IsKeyReleased(ImGuiKey_Escape))
        {
            Disable();
        }
        else if (ImGui::IsKeyReleased(ImGuiKey_Tab))
        {
            ImGuiID inputFieldID = gameConsoleWindow->GetID("##ConsoleInputField");
            ImGui::ActivateItemByID(inputFieldID);
        }

        ImGui::SetWindowFontScale(1.0f);

        const f32 reservedHeight = ImGui::GetStyle().ItemSpacing.y + ImGui::GetFrameHeightWithSpacing();
        ImGui::BeginChild("TextRegion", ImVec2(0, -reservedHeight), false, ImGuiWindowFlags_HorizontalScrollbar);
        {
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 1));

            for (u32 i = 0; i < _lines.size(); i++)
            {
                const std::string& line = _lines[i];
                ImGui::TextWrapped("%s", line.c_str());
            }

            ImGui::PopStyleVar();
        }
        ImGui::EndChild();
        ImGui::Separator();

        ImGui::PushItemWidth(Renderer::Settings::SCREEN_WIDTH);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.50f, 0.50f, 0.50f, 0.50f));

        if (_textFieldHasFocus && !_commandHistory.empty())
        {
            bool wasModified = false;

            if (ImGui::IsKeyDown(ImGuiKey_LeftCtrl))
            {
                if (ImGui::IsKeyReleased(ImGuiKey_UpArrow))
                {
                    i32 numCommandsInHistory = static_cast<i32>(_commandHistory.size());
                    if (_commandHistoryIndex == -1)
                    {
                        _commandHistoryIndex = _commandHistoryDefaultIndex;
                    }
                    else if (--_commandHistoryIndex < 0)
                    {
                        _commandHistoryIndex = numCommandsInHistory - 1;
                    }

                    _searchText = _commandHistory[_commandHistoryIndex];
                    wasModified = true;
                }
                else if (ImGui::IsKeyReleased(ImGuiKey_DownArrow))
                {
                    if (_commandHistoryIndex == -1)
                        _commandHistoryIndex = _commandHistoryDefaultIndex;

                    i32 numCommandsInHistory = static_cast<i32>(_commandHistory.size());
                    if (++_commandHistoryIndex >= numCommandsInHistory)
                        _commandHistoryIndex = 0;

                    _searchText = _commandHistory[_commandHistoryIndex];
                    wasModified = true;
                }
            }

            if (wasModified)
            {
                ImGuiWindow* window = ImGui::GetCurrentWindow();
                ImGuiID inputFieldID = window->GetID("##ConsoleInputField");
                ImGui::GetInputTextState(inputFieldID)->ReloadUserBufAndMoveToEnd();
            }
        }

        u32 searchTextLength = static_cast<u32>(_searchText.length());
        bool handleInput = ImGui::InputText("##ConsoleInputField", &_searchText, ImGuiInputTextFlags_EnterReturnsTrue);

        ImGui::PopStyleColor();
        ImGui::PopItemWidth();

        if (handleInput && searchTextLength > 0)
        {
            bool searchTextIsOnlySpaces = std::all_of(_searchText.begin(), _searchText.end(), [](unsigned char ch) { return std::isspace(ch); });
            if (!searchTextIsOnlySpaces)
            {
                u32 numCommandsInHistory = static_cast<u32>(_commandHistory.size());
                if (numCommandsInHistory < CommandHistoryMaxSize)
                {
                    if (numCommandsInHistory == 0 || _commandHistory.back() != _searchText)
                    {
                        _commandHistoryIndex = -1;
                        _commandHistoryDefaultIndex = numCommandsInHistory;
                        _commandHistory.push_back(_searchText);
                    }
                }
                else
                {
                    _commandHistoryIndex = -1;
                    _commandHistoryDefaultIndex = _commandHistoryUpdateIndex;
                    _commandHistory[_commandHistoryUpdateIndex++] = _searchText;

                    if (_commandHistoryUpdateIndex >= CommandHistoryMaxSize)
                        _commandHistoryUpdateIndex = 0;
                }

                _commandHandler->HandleCommand(this, _searchText);

                _lines.push_back(_searchText);
            }

            _searchText = "";
            ImGuiID inputFieldID = gameConsoleWindow->GetID("##ConsoleInputField");
            ImGui::GetInputTextState(inputFieldID)->ReloadUserBufAndMoveToEnd();
        }

        // Auto-completion suggestions based on the current input
        if (_searchText.empty())
        {
            _lastSearchTextHash = std::numeric_limits<u32>().max();
            _suggestionSelectedIndex = 0;
            _commandHashSuggestions.clear();
        }
        else
        {
            const auto& commands = _commandHandler->GetCommandEntries();

            u32 searchHash = StringUtils::fnv1a_32(_searchText.c_str(), _searchText.length());
            if (searchHash != _lastSearchTextHash)
            {
                _commandHashSuggestions.clear();

                if (!commands.contains(searchHash))
                {
                    for (const auto& command : commands)
                    {
                        if (MatchesCommand(command.second.nameWithAliases, _searchText))
                        {
                            _commandHashSuggestions.push_back(command.first);
                        }
                    }

                    auto EditDistance = [&](const std::string_view a, const std::string_view b) -> i32
                    {
                        size_t n = a.size(), m = b.size();
                        std::vector<i32> dp(m + 1);
                        std::iota(dp.begin(), dp.end(), 0);

                        for (size_t i = 1; i <= n; ++i)
                        {
                            i32 prev = dp[0];
                            dp[0] = static_cast<i32>(i);

                            for (size_t j = 1; j <= m; ++j)
                            {
                                i32 temp = dp[j];
                                if (std::tolower(a[i - 1]) == std::tolower(b[j - 1]))
                                    dp[j] = prev;
                                else
                                    dp[j] = std::min({ dp[j - 1], dp[j], prev }) + 1;

                                prev = temp;
                            }
                        }

                        return dp[m];
                    };

                    auto scoreOne = [&](std::string_view text)
                    {
                        // exact match
                        if (text == _searchText)
                            return std::tuple{ 0, 0, static_cast<i32>(text.size()) };

                        // prefix match
                        if (text.size() >= _searchText.size() &&
                            std::equal(_searchText.begin(), _searchText.end(), text.begin(),
                                [](char a, char b) { return a == b; }))
                        {
                            return std::tuple{ 1, 0, static_cast<int>(text.size()) };
                        }

                        // substring match
                        auto pos = text.find(_searchText);
                        if (pos != std::string::npos)
                        {
                            bool leftBoundary = (pos == 0) || (text[pos - 1] == ' ');
                            bool rightBoundary = (pos + _searchText.size() == text.size()) ||
                                (text[pos + _searchText.size()] == ' ');

                            i32 boundaryScore = (leftBoundary && rightBoundary) ? 0 : 1;

                            return std::tuple{ 2, boundaryScore * 1000 + static_cast<i32>(pos), static_cast<i32>(text.size()) };
                        }

                        // fuzzy fallback
                        i32 dist = EditDistance(text, _searchText);
                        return std::tuple{ 3, dist, static_cast<i32>(text.size()) };
                    };

                    auto score = [&](const GameConsoleCommandEntry& cmd)
                    {
                        auto best = scoreOne(cmd.name);
                        for (auto alias : cmd.aliases)
                        {
                            auto s = scoreOne(alias);
                            if (s < best)
                                best = s;
                        }

                        return best;
                    };

                    std::sort(_commandHashSuggestions.begin(), _commandHashSuggestions.end(), [&](u32 a, u32 b)
                    {
                        const auto& commandA = commands.at(a);
                        const auto& commandB = commands.at(b);

                        return score(commandA) < score(commandB);
                    });
                }

                _lastSearchTextHash = searchHash;
                _suggestionSelectedIndex = 0;
            }
            
            if (!_commandHashSuggestions.empty())
            {
                // Position the popup right below the input field.
                ImVec2 inputPos = ImGui::GetItemRectMin();
                ImVec2 inputSize = ImGui::GetItemRectSize();
                ImVec2 popupPos = ImVec2(inputPos.x, (inputPos.y + inputSize.y));
                ImGui::SetNextWindowPos(popupPos, ImGuiCond_Always);

                bool tabReleased = ImGui::IsKeyReleased(ImGuiKey_Tab);

                if (ImGui::BeginPopup("SuggestionsPopup",
                    ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoMove |
                    ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize))
                {
                    u32 numSuggestions = static_cast<u32>(_commandHashSuggestions.size());

                    if (!ImGui::IsKeyDown(ImGuiKey_LeftCtrl))
                    {
                        if (ImGui::IsKeyReleased(ImGuiKey_UpArrow))
                        {
                            if (--_suggestionSelectedIndex < 0)
                                _suggestionSelectedIndex = static_cast<i32>(numSuggestions) - 1;
                        }
                        else if (ImGui::IsKeyReleased(ImGuiKey_DownArrow))
                        {
                            if (++_suggestionSelectedIndex >= static_cast<i32>(numSuggestions))
                                _suggestionSelectedIndex = 0;
                        }
                    }

                    for (u32 suggestionIndex = 0; suggestionIndex < numSuggestions; suggestionIndex++)
                    {
                        const auto suggestion = _commandHashSuggestions[suggestionIndex];
                        const auto& command = commands.at(suggestion);

                        bool isSelected = suggestionIndex == _suggestionSelectedIndex;
                        if (ImGui::Selectable(command.nameWithAliases.data()) || (isSelected && tabReleased))
                        {
                            _searchText = command.name;
                            if (command.hasParameters)
                                _searchText += " ";

                            ImGuiID inputFieldID = gameConsoleWindow->GetID("##ConsoleInputField");
                            ImGui::GetInputTextState(inputFieldID)->ReloadUserBufAndMoveToEnd();

                            ImGui::CloseCurrentPopup();
                            break;
                        }
                        ImVec2 min = ImGui::GetItemRectMin();

                        if (command.help.length() > 0)
                        {
                            ImGui::SameLine();
                            ImGui::TextDisabled("- %s", command.help.data());
                        }

                        if (isSelected)
                        {
                            static u32 hoveredColor = ImGui::GetColorU32(ImVec4(0.7f, 0.8f, 1.0f, 0.3f));

                            ImVec2 max = ImGui::GetItemRectMax();
                            ImGui::GetWindowDrawList()->AddRectFilled(min, max, hoveredColor);
                        }
                        
                        // Insert a tiny line under each suggestion except the last
                        if (suggestionIndex < numSuggestions - 1)
                            ImGui::Separator();
                    }
                    ImGui::EndPopup();
                }
                ImGui::OpenPopup("SuggestionsPopup");
            }
        }

        if (handleInput)
        {
            ImGui::SetKeyboardFocusHere(-1);
        }

        _textFieldHasFocus = ImGui::IsItemActive();
    }
    ImGui::End();
    ImGui::PopStyleColor();
}

void GameConsole::Clear()
{
    _lines.clear();

    std::string dummy;
    while (_linesToAppend.try_dequeue(dummy)) {}
}

void GameConsole::Toggle()
{
    if (IsEnabled())
    {
        Disable();
    }
    else
    {
        Enable();
    }
}

bool GameConsole::IsEnabled()
{
    return CVAR_GameConsoleEnabled.Get();
}

void GameConsole::Enable()
{
    CVAR_GameConsoleEnabled.Set(1);
    _visibleProgressTimer = 0.0f;
}

void GameConsole::Disable()
{
    CVAR_GameConsoleEnabled.Set(0);
}

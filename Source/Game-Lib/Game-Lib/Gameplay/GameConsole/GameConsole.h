#pragma once
#include <Base/Types.h>
#include <Base/Container/ConcurrentQueue.h>
#include <Base/CVarSystem/CVarSystem.h>
#include <Base/Util/DebugHandler.h>

class GameConsoleCommandHandler;
class GameConsole
{
public:
    GameConsole();
    ~GameConsole();

    void Render(f32 deltaTime);
    void Clear();
    void Toggle();

public:
    template <typename... Args>
    void Print(const char* fmt, Args... args)
    {
        char buffer[FormatBufferSize];
        i32 length = StringUtils::FormatString(buffer, FormatBufferSize, fmt, args...);

        std::string result = std::string(buffer, length);
        _linesToAppend.enqueue(result);

        if (*CVarSystem::Get()->GetIntCVar(CVarCategory::Client, "consoleDuplicateToTerminal"_h))
        {
            NC_LOG_INFO("{0]", result.c_str());
        }
    }

    template <typename... Args>
    void PrintSuccess(const char* fmt, Args... args)
    {
        char buffer[FormatBufferSize];
        i32 length = StringUtils::FormatString(buffer, FormatBufferSize, fmt, args...);

        if (length == 0)
            return;

        std::string result = std::string(buffer, length);
        _linesToAppend.enqueue("[Success] : " + result);

        if (*CVarSystem::Get()->GetIntCVar(CVarCategory::Client, "consoleDuplicateToTerminal"_h))
        {
            NC_LOG_INFO("{0}", result.c_str());
        }
    }

    template <typename... Args>
    void PrintWarning(const char* fmt, Args... args)
    {
        char buffer[FormatBufferSize];
        i32 length = StringUtils::FormatString(buffer, FormatBufferSize, fmt, args...);

        std::string result = std::string(buffer, length);
        _linesToAppend.enqueue("[Warning] : " + std::string(buffer, length));

        if (*CVarSystem::Get()->GetIntCVar(CVarCategory::Client, "consoleDuplicateToTerminal"_h))
        {
            NC_LOG_WARNING("{0}", result.c_str());
        }
    }

    template <typename... Args>
    void PrintError(const char* fmt, Args... args)
    {
        char buffer[FormatBufferSize];
        i32 length = StringUtils::FormatString(buffer, FormatBufferSize, fmt, args...);

        std::string result = std::string(buffer, length);
        _linesToAppend.enqueue("[Error] : " + std::string(buffer, length));

        if (*CVarSystem::Get()->GetIntCVar(CVarCategory::Client, "consoleDuplicateToTerminal"_h))
        {
            NC_LOG_ERROR("{0}", result.c_str());
        }
    }

    template <typename... Args>
    void PrintFatal(const char* fmt, Args... args)
    {
        char buffer[FormatBufferSize];
        i32 length = StringUtils::FormatString(buffer, FormatBufferSize, fmt, args...);

        std::string result = std::string(buffer, length);
        _linesToAppend.enqueue("[Fatal] : " + std::string(buffer, length));

        if (*CVarSystem::Get()->GetIntCVar(CVarCategory::Client, "consoleDuplicateToTerminal"_h))
        {
            NC_LOG_CRITICAL("{0}", result.c_str());
        }
        else
        {
            ReleaseModeBreakpoint();
        }
    }

private:
    void Enable();
    void Disable();

private:
    static constexpr size_t FormatBufferSize = 256;
    f32 _visibleProgressTimer = 0;

    std::string _searchText;
    std::vector<std::string> _lines;
    moodycamel::ConcurrentQueue<std::string> _linesToAppend;

    GameConsoleCommandHandler* _commandHandler = nullptr;
};

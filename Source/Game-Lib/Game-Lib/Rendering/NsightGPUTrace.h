#pragma once

#include <filesystem>
#include <string>

class NsightGPUTrace
{
public:
    enum class State
    {
        Unavailable,
        Inactive,
        Ready,
        Capturing,
        Draining,
        Errored
    };

    bool Initialize(const std::filesystem::path& installationPath, std::string& error);
    bool IsAvailable() const { return _initialized; }
    bool IsReady() const;
    State GetState() const;
    bool Start(std::string& error);
    bool Stop(std::string& error);

    static const char* StateName(State state);

private:
    bool _initialized = false;
};

#pragma once
#include <Base/Types.h>
#include <Base/Container/ConcurrentQueue.h>

#include <atomic>

class Application;
class Bytebuffer;

using IOLoadedCallback = std::function<void(bool loaded, std::shared_ptr<Bytebuffer> buffer, const std::string& path, u64 userdata)>;
struct IOLoadRequest
{
public:
    u64 userdata = 0;
    std::string path = "";

    IOLoadedCallback callback = nullptr;
};

class IOLoader
{
public:
    IOLoader() { }
    ~IOLoader() { }

    void RequestLoad(const IOLoadRequest& request);

    bool IsRunning() { return _isRunning; }

private:
    friend class Application;

    void SetRunning(bool running) { _isRunning = running; }
    bool Tick();

private:
    std::atomic<bool> _isRunning = false;
    moodycamel::ConcurrentQueue<IOLoadRequest> _loadRequests;
};
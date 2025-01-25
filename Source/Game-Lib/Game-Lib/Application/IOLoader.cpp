#include "IOLoader.h"

#include <Base/CVarSystem/CVarSystem.h>
#include <Base/Memory/Bytebuffer.h>
#include <Base/Memory/FileReader.h>

#include <filesystem>

namespace fs = std::filesystem;

AutoCVar_Int CVAR_IOLoaderLoadsPerTick(CVarCategory::Client, "ioLoaderLoadsPerTick", "upper limit for number of files to load per tick", 500);

void IOLoader::RequestLoad(const IOLoadRequest& request)
{
    _loadRequests.enqueue(request);
}

bool IOLoader::Tick()
{
    if (!_isRunning)
        return false;

    IOLoadRequest request;
    i32 maxLoads = CVAR_IOLoaderLoadsPerTick.Get();

    for (i32 j = 0; j < maxLoads; ++j)
    {
        if (!_loadRequests.try_dequeue(request))
            break;

        FileReader reader(request.path);
        if (reader.Open())
        {
            size_t bufferSize = reader.Length();

            std::shared_ptr<Bytebuffer> result = Bytebuffer::BorrowRuntime(bufferSize);
            reader.Read(result.get(), bufferSize);

            if (request.callback)
            {
                request.callback(true, std::move(result), request.path, request.userdata);
            }
        }
        else
        {
            if (request.callback)
            {
                request.callback(false, nullptr, request.path, request.userdata);
            }
        }
    }

    return true;
}

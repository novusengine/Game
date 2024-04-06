#include <Base/Types.h>
#include <Base/Util/DebugHandler.h>
#include <Base/Util/StringUtils.h>

#include <ShaderCooker/ShaderCompiler.h>
#include <ShaderCooker/ShaderCache.h>

#include <fstream>
#include <filesystem>
#include <algorithm>
#include <execution>

i32 main(int argc, char* argv[])
{
    if (argc != 3)
    {
        DebugHandler::PrintError("Expected two parameters, got {0}. Usage: <shader_source_dir> <shader_bin_dir>", argc);
        return -1;
    }

    std::string sourceDir = argv[1];
    std::string binDir = argv[2];
    std::chrono::system_clock::time_point startTime = std::chrono::system_clock::now();

    std::filesystem::path shaderCachePath = std::filesystem::path(binDir) / "_shaders.cache";
    shaderCachePath = std::filesystem::absolute(shaderCachePath).make_preferred();

    ShaderCooker::ShaderCache shaderCache;

    const bool debugSkipCache = false;
    if (!debugSkipCache)
    {
        std::string shaderCachePathStr = shaderCachePath.string();

        if (shaderCache.Load(shaderCachePath))
        {
            DebugHandler::Print("Loaded shadercache from: {0}", shaderCachePathStr);
        }
        else
        {
            DebugHandler::Print("Creating shadercache at: {0}", shaderCachePathStr);
        }
    }
    else
    {
        DebugHandler::Print("Skipped loading shadercache due to debugSkipCache being true");
    }

    ShaderCooker::ShaderCompiler compiler;

    // Find all shader files in the source directory
    std::vector<std::filesystem::path> shadersToCompile;
    u32 numNonIncludeShaders = 0;

    for (auto& dirEntry : std::filesystem::recursive_directory_iterator(sourceDir))
    {
        std::filesystem::path path = dirEntry.path();
        path = path.make_preferred();

        // Skip non files
        if (!dirEntry.is_regular_file())
            continue;

        // Skip non .hlsl files
        if (!StringUtils::EndsWith(path.filename().string(), ".hlsl"))
            continue;

        if (!StringUtils::EndsWith(path.filename().string(), ".inc.hlsl"))
            numNonIncludeShaders++;

        // Add this file to our list of shaders to compile
        shadersToCompile.push_back(path);
    }

    compiler.SetShaderCache(&shaderCache);
    compiler.SetSourceDirPath(sourceDir);
    compiler.SetBinDirPath(binDir);
    compiler.Start();
    compiler.AddPaths(shadersToCompile);
    compiler.Process();

    while (compiler.GetStage() != ShaderCooker::ShaderCompiler::Stage::STOPPED)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    // Save our updated shader cache
    u32 numFailedShaders = compiler.GetNumFailedShaders();
    if (numFailedShaders == 0)
    {
        shaderCache.Save(shaderCachePath);
    }
    else
    {
        DebugHandler::PrintError("Failed to compile {0} shaders", numFailedShaders);
    }

    std::chrono::system_clock::time_point endTime = std::chrono::system_clock::now();
    std::chrono::duration<double> duration = endTime - startTime;

    u32 numCompiledShaders = compiler.GetNumCompiledShaders();
    u32 numSkippedShaders = numNonIncludeShaders - numCompiledShaders;
    DebugHandler::Print("Compiled {0} shaders ({1} up to date) in {2}s", numCompiledShaders, numSkippedShaders, duration.count());
    return 0;
}
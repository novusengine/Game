#include <Base/Types.h>
#include <Base/Util/DebugHandler.h>
#include <Base/Util/StringUtils.h>

#include <ShaderCooker/ShaderCompiler.h>
#include <ShaderCooker/ShaderCache.h>

#include <quill/Backend.h>

#include <fstream>
#include <filesystem>
#include <algorithm>
#include <execution>

i32 main(int argc, char* argv[])
{
    quill::Backend::start();

    quill::ConsoleColours colors;
    auto console_sink = quill::Frontend::create_or_get_sink<quill::ConsoleSink>("console_sink_1", colors, "stderr");
    quill::Logger* logger = quill::Frontend::create_or_get_logger("root", std::move(console_sink), "%(time:<16) LOG_%(log_level:<11) %(message)", "%H:%M:%S.%Qms", quill::Timezone::LocalTime, quill::ClockSourceType::System);

    int argIndex = 1;

    // Handle force flag
    bool debugSkipCache = false;
    if (argc > 1 && std::string(argv[1]) == "-f")
    {
        debugSkipCache = true;
        argIndex++;
    }

    // Handle debug SPV output flag
    bool debugOutputSpv = false;
    if (argc > argIndex && std::string(argv[argIndex]) == "-d")
    {
        debugOutputSpv = true;
        argIndex++;
    }

    if (argc - argIndex != 2)
    {
        NC_LOG_ERROR("Expected two parameters, got {}. Usage: [-f] [-d] <shader_source_dir> <shader_bin_dir>", argc - 1);
        return -1;
    }

    std::string sourceDir = argv[argIndex];
    std::string binDir = argv[argIndex + 1];
    std::chrono::system_clock::time_point startTime = std::chrono::system_clock::now();

    std::filesystem::path shaderCachePath = std::filesystem::path(binDir) / "_shaders.cache";
    shaderCachePath = std::filesystem::absolute(shaderCachePath).make_preferred();

    ShaderCooker::ShaderCache shaderCache;

    if (!debugSkipCache)
    {
        std::string shaderCachePathStr = shaderCachePath.string();

        if (shaderCache.Load(shaderCachePath))
        {
            NC_LOG_INFO("Loaded shadercache from: {0}", shaderCachePathStr);
        }
        else
        {
            NC_LOG_INFO("Creating shadercache at: {0}", shaderCachePathStr);
        }
    }
    else
    {
        NC_LOG_INFO("Skipped loading shadercache due to being ran with -f flag");
    }

    ShaderCooker::ShaderCompiler compiler;
    compiler.SetDebugOutputSPV(debugOutputSpv);

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

        // Skip non .slang files
        if (!StringUtils::EndsWith(path.filename().string(), ".slang"))
            continue;

        if (!StringUtils::EndsWith(path.filename().string(), ".inc.slang"))
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
        NC_LOG_ERROR("Failed to compile {0} shaders", numFailedShaders);
    }

    std::chrono::system_clock::time_point endTime = std::chrono::system_clock::now();
    std::chrono::duration<double> duration = endTime - startTime;

    u32 numCompiledShaders = compiler.GetNumCompiledShaders();
    u32 numSkippedShaders = numNonIncludeShaders - numCompiledShaders - numFailedShaders;
    NC_LOG_INFO("Compiled {0} shaders ({1} failed, {2} up to date) in {3}s", numCompiledShaders, numFailedShaders, numSkippedShaders, duration.count());
    return 0;
}
#include <Base/Types.h>
#include <Base/Util/DebugHandler.h>
#include <Base/Util/StringUtils.h>

#include <ShaderCooker/ShaderCompiler.h>
#include <ShaderCooker/ShaderCache.h>
#include <MaterialCooker/AuthoredMaterialShaderGenerator.h>
#include <MaterialCooker/MaterialSourceRegistry.h>
#include <MaterialCooker/MaterialProgramManifest.h>
#include <MaterialCooker/MaterialPackWriter.h>

#include "HeadlessMaterialPipelineMeasurement.h"

#include <quill/Backend.h>

#include <fstream>
#include <filesystem>
#include <algorithm>
#include <execution>
#include <optional>

i32 main(int argc, char* argv[])
{
    quill::Backend::start();

    quill::ConsoleColours colors;
    auto console_sink = quill::Frontend::create_or_get_sink<quill::ConsoleSink>("console_sink_1", colors, "stderr");
    quill::Logger* logger = quill::Frontend::create_or_get_logger("root", std::move(console_sink), "%(time:<16) LOG_%(log_level:<11) %(message)", "%H:%M:%S.%Qms", quill::Timezone::LocalTime, quill::ClockSourceType::System);

    int argIndex = 1;
    bool debugSkipCache = false;
    bool debugOutputSpv = false;
    bool allowIncompleteMaterialSourceSemantics = false;
    std::filesystem::path materialManifestPath;
    std::filesystem::path materialReportPath;
    std::filesystem::path materialPackPath;
    MaterialCooking::MaterialMeasurementMode materialMeasurementMode =
        MaterialCooking::MaterialMeasurementMode::Disabled;
    while (argIndex < argc)
    {
        const std::string_view argument = argv[argIndex];
        if (argument == "-f")
            debugSkipCache = true;
        else if (argument == "-d")
            debugOutputSpv = true;
        else if (argument == "--material-allow-incomplete-source-semantics")
            allowIncompleteMaterialSourceSemantics = true;
        else if (argument == "--material-cook")
        {
            if (argIndex + 2 >= argc)
            {
                NC_LOG_ERROR("--material-cook expects <manifest.json> <report.json>");
                return -1;
            }
            materialManifestPath = argv[++argIndex];
            materialReportPath = argv[++argIndex];
        }
        else if (argument == "--material-measure")
        {
            if (argIndex + 1 >= argc)
            {
                NC_LOG_ERROR("--material-measure expects advisory or required");
                return -1;
            }
            const std::string_view mode = argv[++argIndex];
            if (mode == "advisory")
                materialMeasurementMode = MaterialCooking::MaterialMeasurementMode::Advisory;
            else if (mode == "required")
                materialMeasurementMode = MaterialCooking::MaterialMeasurementMode::Required;
            else
            {
                NC_LOG_ERROR("Unsupported Material measurement mode: {}", mode);
                return -1;
            }
        }
        else if (argument == "--material-pack")
        {
            if (argIndex + 1 >= argc)
            {
                NC_LOG_ERROR("--material-pack expects <output.matpack>");
                return -1;
            }
            materialPackPath = argv[++argIndex];
        }
        else
            break;
        ++argIndex;
    }

    if (argc - argIndex != 2)
    {
        NC_LOG_ERROR("Expected two parameters, got {}. Usage: [-f] [-d] "
                     "[--material-cook <manifest.json> <report.json>] "
                     "[--material-allow-incomplete-source-semantics] "
                     "[--material-measure <advisory|required>] "
                     "[--material-pack <output.matpack>] "
                     "<shader_source_dir> <shader_bin_dir>", argc - argIndex);
        return -1;
    }

    std::string sourceDir = argv[argIndex];
    std::string binDir = argv[argIndex + 1];
    std::chrono::system_clock::time_point startTime = std::chrono::system_clock::now();
    if (materialManifestPath.empty() &&
        materialMeasurementMode != MaterialCooking::MaterialMeasurementMode::Disabled)
    {
        NC_LOG_ERROR("--material-measure requires --material-cook");
        return -1;
    }
    if (materialManifestPath.empty() && allowIncompleteMaterialSourceSemantics)
    {
        NC_LOG_ERROR("--material-allow-incomplete-source-semantics requires --material-cook");
        return -1;
    }
    if (materialManifestPath.empty() && !materialPackPath.empty())
    {
        NC_LOG_ERROR("--material-pack requires --material-cook");
        return -1;
    }

    std::optional<MaterialCooking::MaterialCookResult> materialResult;
    if (!materialManifestPath.empty())
    {
        MaterialCooking::MaterialCookManifestLoadResult loaded =
            MaterialCooking::MaterialProgramManifestIO::Load(materialManifestPath);
        if (!loaded)
        {
            NC_LOG_ERROR("Failed to load Material cook manifest: {}", loaded.error);
            return -1;
        }

        const std::filesystem::path shaderSourceDirectory = sourceDir;
        MaterialCooking::MaterialSourceRegistryLoadResult loadedSources =
            MaterialCooking::MaterialSourceRegistryIO::Load(
                shaderSourceDirectory / "Material" / "Authored", shaderSourceDirectory);
        if (!loadedSources)
        {
            NC_LOG_ERROR("Failed to load authored Material sources: {}", loadedSources.error);
            return -1;
        }

        const std::vector<MaterialCooking::AuthoredMaterialProgramView> views =
            MaterialCooking::MaterialProgramManifestIO::MakeViews(loaded.manifest);
        MaterialCooking::MaterialSourceResolveResult resolved =
            MaterialCooking::MaterialSourceRegistryIO::Resolve(loadedSources.registry, views);
        if (!resolved)
        {
            NC_LOG_ERROR("Failed to resolve Material registries: {}", resolved.error);
            return -1;
        }
        MaterialCooking::MaterialCookRequest request;
        request.sourcePrograms = views;
        request.assignments = resolved.programs;
        request.requireCompleteSourceSemantics =
            !allowIncompleteMaterialSourceSemantics;
        materialResult = MaterialCooking::MaterialCooker::Cook(request);
        if (!*materialResult)
        {
            MaterialCooking::MaterialProgramManifestIO::SaveCookReport(
                materialReportPath, *materialResult);
            NC_LOG_ERROR("Material cook failed with {} diagnostics",
                         materialResult->plan.diagnostics.size());
            return -1;
        }
        const std::filesystem::path generatedShaderDirectory =
            std::filesystem::path(sourceDir) / "Generated";
        std::string generatedSourceError;
        if (!MaterialCooking::MaterialSourceRegistryIO::WriteGeneratedSources(
                loadedSources.registry, shaderSourceDirectory, generatedSourceError))
        {
            NC_LOG_ERROR("Failed to generate stripped Material sources: {}",
                         generatedSourceError);
            return -1;
        }
        if (!MaterialCooking::AuthoredMaterialShaderGenerator::Generate(
                generatedShaderDirectory, materialResult->plan))
        {
            NC_LOG_ERROR("Failed to generate authored Material group shaders: {}",
                         generatedShaderDirectory.string());
            return -1;
        }
        NC_LOG_INFO("Material cook produced {} programs: {}",
                    materialResult->plan.programs.size(), materialReportPath.string());
        NC_LOG_INFO("Generated authored Material group shaders: {}", generatedShaderDirectory.string());
    }

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

        if (!StringUtils::EndsWith(path.filename().string(), ".inc.slang") &&
            !StringUtils::EndsWith(path.filename().string(), ".mat.slang"))
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

    if (materialResult)
    {
        if (numFailedShaders == 0)
        {
            const std::filesystem::path shaderPackPath =
                std::filesystem::path(binDir) / "Generated" / "MaterialGroups.cs.shaderpack";
            HeadlessMaterialPipelineMeasurement adapter(shaderPackPath);
            materialResult->measurement = MaterialCooking::MaterialPipelineMeasurement::Run(
                materialMeasurementMode, materialResult->plan.programs,
                materialMeasurementMode == MaterialCooking::MaterialMeasurementMode::Disabled ?
                    nullptr : &adapter);
        }
        else if (materialMeasurementMode != MaterialCooking::MaterialMeasurementMode::Disabled)
        {
            materialResult->measurement.report.status =
                MaterialCooking::MaterialMeasurementStatus::Unavailable;
            materialResult->measurement.report.unavailableReason =
                "generated Material shaders failed to compile";
            materialResult->measurement.measurementRequirementMet =
                materialMeasurementMode != MaterialCooking::MaterialMeasurementMode::Required;
        }

        if (!MaterialCooking::MaterialProgramManifestIO::SaveCookReport(
                materialReportPath, *materialResult))
        {
            NC_LOG_ERROR("Failed to save Material cook report: {}",
                         materialReportPath.string());
            return -1;
        }
        if (numFailedShaders == 0 && !materialPackPath.empty())
        {
            std::string materialPackError;
            if (!MaterialCooking::MaterialPackWriter::Save(
                    materialPackPath, materialResult->plan, materialPackError))
            {
                NC_LOG_ERROR("Failed to write MaterialPack: {}", materialPackError);
                return -1;
            }
            NC_LOG_INFO("Wrote MaterialPack: {}", materialPackPath.string());
        }
        if (!*materialResult)
        {
            NC_LOG_ERROR("Required Material pipeline measurement is unavailable: {}",
                         materialResult->measurement.report.unavailableReason);
            return -1;
        }
    }

    std::chrono::system_clock::time_point endTime = std::chrono::system_clock::now();
    std::chrono::duration<double> duration = endTime - startTime;

    u32 numCompiledShaders = compiler.GetNumCompiledShaders();
    u32 numSkippedShaders = numNonIncludeShaders - numCompiledShaders - numFailedShaders;
    NC_LOG_INFO("Compiled {0} shaders ({1} failed, {2} up to date) in {3}s", numCompiledShaders, numFailedShaders, numSkippedShaders, duration.count());
    return numFailedShaders == 0 ? 0 : -1;
}

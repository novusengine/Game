#include "MaterialProgramManifest.h"
#include "MaterialCookContract.h"

#include <Base/Util/JsonUtils.h>

#include <algorithm>
#include <utility>

namespace
{
    using namespace MaterialCooking;

    const char* MeasurementStatusName(MaterialMeasurementStatus status)
    {
        switch (status)
        {
        case MaterialMeasurementStatus::Disabled: return "disabled";
        case MaterialMeasurementStatus::Available: return "available";
        case MaterialMeasurementStatus::Unavailable: return "unavailable";
        }
        return "unknown";
    }

}

namespace MaterialCooking
{
    MaterialCookManifestLoadResult MaterialProgramManifestIO::Load(
        const std::filesystem::path& path)
    {
        MaterialCookManifestLoadResult result;
        nlohmann::json root;
        if (!JsonUtils::LoadFromPath(root, path))
        {
            result.error = "failed to read Material program manifest";
            return result;
        }

        try
        {
            if (root.at("schemaVersion").get<u32>() != Contract::PROGRAM_MANIFEST_VERSION)
            {
                result.error = "unsupported Material program manifest version";
                return result;
            }
            if (root.at("materialABIVersion").get<u32>() != FileFormat::Material::ABI::VERSION)
            {
                result.error = "unsupported Material ABI version";
                return result;
            }

            const u32 parameterBlockSize = root.at("parameterBlockSize").get<u32>();
            const u32 parameterBlockAlignment = root.at("parameterBlockAlignment").get<u32>();
            const u64 parameterLayoutHash = root.at("parameterLayoutHash").get<u64>();
            std::vector<FileFormat::Material::ParameterDefinition> parameters;
            for (const nlohmann::json& source : root.at("parameters"))
            {
                const u32 type = source.at("type").get<u32>();
                if (type > static_cast<u32>(FileFormat::Material::ParameterType::Sampler))
                {
                    result.error = "invalid Material parameter type";
                    return result;
                }
                FileFormat::Material::ParameterDefinition& parameter = parameters.emplace_back();
                parameter.nameHash = source.at("nameHash").get<u64>();
                parameter.byteOffset = source.at("byteOffset").get<u32>();
                parameter.byteSize = source.at("byteSize").get<u16>();
                parameter.type = static_cast<FileFormat::Material::ParameterType>(type);
                parameter.arrayCount = source.at("arrayCount").get<u8>();
            }
            if (FileFormat::Material::CalculateParameterLayoutHash(
                    parameters, parameterBlockSize) != parameterLayoutHash)
            {
                result.error = "Material parameter layout hash mismatch";
                return result;
            }

            const nlohmann::json& programs = root.at("programs");
            if (programs.size() != root.at("programCount").get<u32>())
            {
                result.error = "Material program count mismatch";
                return result;
            }
            result.manifest.programs.reserve(programs.size());
            for (const nlohmann::json& source : programs)
            {
                MaterialCookInputProgram& program = result.manifest.programs.emplace_back();
                program.canonicalKey = source.at("canonicalKey").get<std::string>();
                program.material.programKey =
                    source.at("programKey").get<FileFormat::Material::MaterialProgramKey>();
                program.material.programID = source.at("programID").get<u32>();
                program.lightingModelID = source.at("lightingModelID").get<u16>();
                const u32 rasterClass = source.at("rasterClass").get<u32>();
                if (rasterClass > static_cast<u32>(FileFormat::Material::RasterClass::Transparent))
                {
                    result.error = "invalid raster class";
                    return result;
                }
                program.rasterClass =
                    static_cast<FileFormat::Material::RasterClass>(rasterClass);
                program.material.flags = source.at("materialFlags").get<u32>();
                program.material.parameterBlockSize = parameterBlockSize;
                program.material.parameterBlockAlignment = parameterBlockAlignment;
                program.material.defaultParameterDataSize = parameterBlockSize;
                program.parameters = parameters;

                const nlohmann::json& units = source.at("units");
                if (units.empty() ||
                    units.size() > FileFormat::Material::ABI::LegacyModel::MAX_UNITS)
                {
                    result.error = "invalid Material program unit count";
                    return result;
                }
                program.unitCount = static_cast<u8>(units.size());
                for (u32 unitIndex = 0; unitIndex < units.size(); ++unitIndex)
                {
                    const nlohmann::json& unit = units[unitIndex];
                    const u32 shaderID = unit.at("authoredShaderID").get<u32>();
                    const u32 textureCount = unit.at("textureCount").get<u32>();
                    const u32 layer = unit.at("layer").get<u32>();
                    const u32 flags = unit.at("flags").get<u32>();
                    if (shaderID > 0xFFFFu ||
                        textureCount > FileFormat::Material::ABI::LegacyModel::MAX_TEXTURES ||
                        layer > 0xFFu || flags > 0xFFu)
                    {
                        result.error = "Material program unit field is out of range";
                        return result;
                    }
                    LegacyModelSourceUnit& sourceUnit = program.sourceUnits.emplace_back();
                    sourceUnit.authoredShaderID = static_cast<u16>(shaderID);
                    sourceUnit.textureCount = static_cast<u8>(textureCount);
                    sourceUnit.layer = static_cast<u8>(layer);
                    sourceUnit.flags = static_cast<u8>(flags);
                    sourceUnit.blendMode = unit.at("blendMode").get<u8>();
                    sourceUnit.sourceMaterialKind = unit.at("sourceMaterialKind").get<u8>();
                    sourceUnit.sourceMaterialFlags = unit.at("sourceMaterialFlags").get<u32>();
                    sourceUnit.sourceBlendMode = unit.at("sourceBlendMode").get<u32>();
                    sourceUnit.semanticFlags = LegacyModelSourceUnitSemanticFlags_Complete;
                    if (unit.at("isUnlit").get<bool>())
                        sourceUnit.semanticFlags |= LegacyModelSourceUnitSemanticFlags_Unlit;
                    if (unit.at("isUnfogged").get<bool>())
                        sourceUnit.semanticFlags |= LegacyModelSourceUnitSemanticFlags_Unfogged;
                    if (unit.at("isTwoSided").get<bool>())
                        sourceUnit.semanticFlags |= LegacyModelSourceUnitSemanticFlags_TwoSided;
                }
            }
        }
        catch (const nlohmann::json::exception& exception)
        {
            result.error = exception.what();
        }
        return result;
    }

    std::vector<AuthoredMaterialProgramView> MaterialProgramManifestIO::MakeViews(
        const MaterialCookManifest& manifest)
    {
        std::vector<AuthoredMaterialProgramView> views;
        views.reserve(manifest.programs.size());
        for (const MaterialCookInputProgram& program : manifest.programs)
            views.push_back({program.canonicalKey, &program.material, program.parameters,
                             program.parameterData, program.sourceUnits,
                             program.lightingModelID, program.rasterClass});
        return views;
    }

    bool MaterialProgramManifestIO::SaveCookReport(const std::filesystem::path& path,
                                                 const MaterialCookResult& result)
    {
        nlohmann::ordered_json root;
        root["schemaVersion"] = Contract::COOK_REPORT_VERSION;
        root["materialABIVersion"] = FileFormat::Material::ABI::VERSION;
        root["functionalCookSucceeded"] = result.functionalCookSucceeded;
        root["sourceManifestFingerprint"] = result.plan.sourceManifestFingerprint;
        root["routingFingerprint"] = result.plan.routingFingerprint;
        root["functionalCookFingerprint"] = result.plan.functionalCookFingerprint;
        root["measurementRequirementMet"] = result.measurement.measurementRequirementMet;
        root["measurementStatus"] = MeasurementStatusName(result.measurement.report.status);
        root["measurementDeviceName"] = result.measurement.report.deviceName;
        root["measurementDriverIdentity"] = result.measurement.report.driverIdentity;
        root["measurementUnavailableReason"] = result.measurement.report.unavailableReason;
        root["measurementStatistics"] = nlohmann::ordered_json::array();
        for (const MaterialPipelineStatistic& statistic : result.measurement.report.statistics)
        {
            root["measurementStatistics"].push_back({
                {"canonicalKey", statistic.canonicalKey},
                {"name", statistic.name},
                {"executionGroupID", statistic.executionGroupID},
                {"groupLocalProgramID", statistic.groupLocalProgramID},
                {"value", statistic.value},
                {"description", statistic.description},
                {"format", statistic.format}
            });
        }
        root["measurementGroupMedians"] = nlohmann::ordered_json::array();
        for (const MaterialPipelineStatisticMedian& median :
             result.measurement.report.groupMedians)
        {
            root["measurementGroupMedians"].push_back({
                {"name", median.name},
                {"executionGroupID", median.executionGroupID},
                {"value", median.value}
            });
        }
        root["measurementOutliers"] = nlohmann::ordered_json::array();
        for (const MaterialPipelineStatisticOutlier& outlier : result.measurement.report.outliers)
        {
            root["measurementOutliers"].push_back({
                {"name", outlier.name},
                {"executionGroupID", outlier.executionGroupID},
                {"value", outlier.value},
                {"crossGroupMedian", outlier.crossGroupMedian},
                {"ratio", outlier.ratio}
            });
        }
        root["measurementNearIdenticalGroups"] = nlohmann::ordered_json::array();
        for (const MaterialPipelineNearIdenticalGroups& groups :
             result.measurement.report.nearIdenticalGroups)
        {
            root["measurementNearIdenticalGroups"].push_back({
                {"firstExecutionGroupID", groups.firstExecutionGroupID},
                {"secondExecutionGroupID", groups.secondExecutionGroupID},
                {"maximumRelativeDifference", groups.maximumRelativeDifference}
            });
        }
        root["measurementBudgetWarnings"] = nlohmann::ordered_json::array();
        for (const MaterialPipelineBudgetWarning& warning :
             result.measurement.report.budgetWarnings)
        {
            root["measurementBudgetWarnings"].push_back({
                {"name", warning.name},
                {"executionGroupID", warning.executionGroupID},
                {"value", warning.value},
                {"budget", warning.budget}
            });
        }
        root["programs"] = nlohmann::ordered_json::array();
        for (const CookedMaterialProgram& cooked : result.plan.programs)
        {
            nlohmann::ordered_json program = {
                {"canonicalKey", cooked.canonicalKey},
                {"materialSource", cooked.materialSource},
                {"materialFunction", cooked.materialFunction},
                {"materialCoverageFunction", cooked.materialCoverageFunction},
                {"executionGroupID", cooked.program.executionGroupID},
                {"groupLocalProgramID", cooked.groupLocalProgramID},
                {"programFamily", cooked.programFamily},
                {"sourceProgramKey", cooked.program.sourceProgramKey},
                {"sourceProgramID", cooked.program.sourceProgramID},
                {"lightingModelID", cooked.program.lightingModelID},
                {"rasterClass", cooked.program.rasterClass},
                {"unitCount", cooked.program.unitCount},
                {"textureCount", cooked.program.textureCount},
                {"flags", cooked.program.flags},
                {"parameterLayoutHash", cooked.program.parameterLayoutHash},
                {"parameterBlockSize", cooked.program.parameterBlockSize},
                {"parameterBlockAlignment", cooked.program.parameterBlockAlignment}
            };
            program["parameters"] = nlohmann::ordered_json::array();
            for (const FileFormat::Material::ParameterDefinition& parameter : cooked.parameters)
            {
                program["parameters"].push_back({
                    {"nameHash", parameter.nameHash},
                    {"byteOffset", parameter.byteOffset},
                    {"byteSize", parameter.byteSize},
                    {"type", static_cast<u8>(parameter.type)},
                    {"arrayCount", parameter.arrayCount}
                });
            }
            program["units"] = nlohmann::ordered_json::array();
            for (u32 unitIndex = 0; unitIndex < cooked.program.unitCount; ++unitIndex)
            {
                const CompiledMaterialUnit& unit = cooked.program.units[unitIndex];
                program["units"].push_back({
                    {"authoredShaderID", unit.authoredShaderID},
                    {"pixelShaderID", unit.pixelShaderID},
                    {"vertexShaderID", unit.vertexShaderID},
                    {"textureOffset", unit.textureOffset},
                    {"textureCount", unit.textureCount},
                    {"layer", unit.layer},
                    {"flags", unit.flags},
                    {"blendMode", unit.blendMode},
                    {"sourceMaterialKind", unit.sourceMaterialKind},
                    {"semanticFlags", unit.semanticFlags},
                    {"sourceMaterialFlags", unit.sourceMaterialFlags},
                    {"sourceBlendMode", unit.sourceBlendMode}
                });
            }
            root["programs"].push_back(std::move(program));
        }
        root["diagnostics"] = nlohmann::ordered_json::array();
        for (const MaterialCookPlanDiagnostic& diagnostic : result.plan.diagnostics)
        {
            root["diagnostics"].push_back({
                {"canonicalKey", diagnostic.canonicalKey},
                {"error", MaterialCookPlanBuilder::Describe(diagnostic.error)},
                {"compileError", LegacyMaterialCompiler::Describe(diagnostic.compileError)},
                {"observed", diagnostic.observed}
            });
        }
        std::error_code error;
        if (!path.parent_path().empty())
            std::filesystem::create_directories(path.parent_path(), error);
        return !error && JsonUtils::SaveToPath(root, path);
    }
}

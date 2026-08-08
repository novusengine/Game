#include "MaterialPackWriter.h"

#include <Base/Memory/Bytebuffer.h>
#include <Base/Util/StringUtils.h>
#include <FileFormat/Novus/Model/MaterialABI.h>
#include <FileFormat/Novus/Model/MaterialPack.h>

#include <algorithm>
#include <array>
#include <fstream>
#include <limits>

namespace
{
    using namespace MaterialCooking;

    u32 ShaderPermutationHash(std::string_view shaderName, u16 executionGroupID,
                              std::string_view extension)
    {
        const std::string path = std::string(shaderName) +
            "-MATERIAL_COOK_GROUP" + std::to_string(executionGroupID) +
            std::string(extension);
        return StringUtils::fnv1a_32(path.c_str(), path.size());
    }
}

namespace MaterialCooking
{
    bool MaterialPackWriter::Save(const std::filesystem::path& path,
                                  const MaterialCookPlan& plan, std::string& error)
    {
        error.clear();
        if (!plan)
        {
            error = "cannot write a MaterialPack from an invalid cook plan";
            return false;
        }
        if (plan.programs.size() > std::numeric_limits<u32>::max())
        {
            error = "MaterialPack program count exceeds the file format";
            return false;
        }

        FileFormat::Material::MaterialPackData data;
        data.programs.reserve(plan.programs.size());
        data.programLookups.reserve(plan.programs.size());

        std::array<u32, FileFormat::Material::ABI::EXECUTION_GROUP_COUNT> groupProgramCounts = {};
        std::array<bool, FileFormat::Material::ABI::EXECUTION_GROUP_COUNT> groupHasCoverage = {};
        for (const CookedMaterialProgram& cooked : plan.programs)
        {
            for (const FileFormat::Material::MaterialProgramRoute& route : cooked.rasterRoutes)
            {
                groupProgramCounts[route.executionGroupID] = std::max(
                    groupProgramCounts[route.executionGroupID],
                    static_cast<u32>(route.groupLocalProgramID) + 1u);
                if (!cooked.materialCoverageFunction.empty())
                    groupHasCoverage[route.executionGroupID] = true;
            }
        }
        for (u16 groupID = 0; groupID < FileFormat::Material::ABI::EXECUTION_GROUP_COUNT; ++groupID)
        {
            if (groupProgramCounts[groupID] == 0)
                continue;
            FileFormat::Material::MaterialExecutionGroup& group =
                data.executionGroups.emplace_back();
            group.executionGroupID = groupID;
            group.numPrograms = groupProgramCounts[groupID];
            group.resolveShaderPermutationHash =
                ShaderPermutationHash("Generated/MaterialResolve", groupID, ".cs");
            group.coverageShaderPermutationHash =
                ShaderPermutationHash("Generated/MaterialGroupsCoverage", groupID, ".ps");
            group.forwardShaderPermutationHash =
                ShaderPermutationHash("Generated/MaterialGroupsForward", groupID, ".ps");
            if ((groupID == 2u || groupID == 3u) && groupHasCoverage[groupID])
                group.flags |= FileFormat::Material::MaterialExecutionGroupFlags_HasCoverageShader;
        }

        for (u32 programIndex = 0; programIndex < plan.programs.size(); ++programIndex)
        {
            const CookedMaterialProgram& cooked = plan.programs[programIndex];
            if (cooked.parameters.size() > std::numeric_limits<u16>::max() ||
                data.parameterDefinitions.size() > std::numeric_limits<u32>::max() -
                    cooked.parameters.size())
            {
                error = "MaterialPack parameter reflection exceeds the file format";
                return false;
            }

            FileFormat::Material::MaterialProgramRecord& record = data.programs.emplace_back();
            record.programKey = cooked.program.sourceProgramKey;
            record.parameterLayoutHash = cooked.program.parameterLayoutHash;
            record.programID = cooked.program.sourceProgramID;
            record.flags = cooked.program.flags;
            record.parameterDefinitionOffset =
                static_cast<u32>(data.parameterDefinitions.size());
            record.parameterBlockSize = cooked.program.parameterBlockSize;
            record.parameterBlockAlignment = cooked.program.parameterBlockAlignment;
            record.numParameterDefinitions = static_cast<u16>(cooked.parameters.size());
            record.rasterRoutes = cooked.rasterRoutes;

            data.parameterDefinitions.insert(data.parameterDefinitions.end(),
                                             cooked.parameters.begin(), cooked.parameters.end());
            data.programLookups.push_back({.programKey = record.programKey,
                                           .programIndex = programIndex});
        }

        std::sort(data.programLookups.begin(), data.programLookups.end(),
                  [](const FileFormat::Material::MaterialProgramLookup& left,
                     const FileFormat::Material::MaterialProgramLookup& right) {
                      return left.programKey < right.programKey;
                  });

        FileFormat::Material::MaterialPack pack;
        pack.materialABIVersion = FileFormat::Material::ABI::VERSION;
        pack.sourceManifestFingerprint = plan.sourceManifestFingerprint;
        pack.routingFingerprint = plan.routingFingerprint;
        pack.functionalCookFingerprint = plan.functionalCookFingerprint;
        std::shared_ptr<Bytebuffer> buffer = Bytebuffer::BorrowRuntime(pack.GetSerializedSize(data));
        if (!pack.Save(buffer, data))
        {
            error = "failed to serialize MaterialPack";
            return false;
        }

        std::error_code filesystemError;
        if (!path.parent_path().empty())
            std::filesystem::create_directories(path.parent_path(), filesystemError);
        if (filesystemError)
        {
            error = "failed to create MaterialPack output directory";
            return false;
        }

        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output.write(reinterpret_cast<const char*>(buffer->GetDataPointer()),
                     static_cast<std::streamsize>(buffer->writtenData));
        if (!output.good())
        {
            error = "failed to write MaterialPack";
            return false;
        }
        return true;
    }
}

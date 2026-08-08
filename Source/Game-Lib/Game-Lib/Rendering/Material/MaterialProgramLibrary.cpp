#include "MaterialProgramLibrary.h"

#include <Base/Util/DebugHandler.h>
#include <Base/Memory/Bytebuffer.h>
#include <FileFormat/Novus/Model/MaterialABI.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>

namespace
{
    template <typename T>
    std::vector<T> CopySection(Bytebuffer& buffer, u32 offset, u32 count)
    {
        if (count == 0)
            return {};
        const T* begin = reinterpret_cast<const T*>(buffer.GetDataPointer() + offset);
        return std::vector<T>(begin, begin + count);
    }
}

namespace MaterialLoading
{
    bool MaterialProgramLibrary::Load(const std::filesystem::path& path, std::string& error)
    {
        error.clear();
        MaterialProgramLibrary candidate;
        std::ifstream input(path, std::ios::binary);
        if (!input)
        {
            error = "failed to open MaterialPack";
            return false;
        }
        const std::vector<u8> bytes{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
        std::shared_ptr<Bytebuffer> buffer = Bytebuffer::BorrowRuntime(bytes.size());
        if (!buffer->PutBytes(bytes.data(), bytes.size()) || !FileFormat::Material::MaterialPack::Read(buffer, candidate._pack))
        {
            error = "invalid MaterialPack FileFormat";
            return false;
        }

        candidate._executionGroups = CopySection<FileFormat::Material::MaterialExecutionGroup>(*buffer, candidate._pack.executionGroupsOffset, candidate._pack.numExecutionGroups);
        candidate._programs = CopySection<FileFormat::Material::MaterialProgramRecord>(*buffer, candidate._pack.programsOffset, candidate._pack.numPrograms);
        candidate._programLookups = CopySection<FileFormat::Material::MaterialProgramLookup>(*buffer, candidate._pack.programLookupsOffset, candidate._pack.numProgramLookups);
        candidate._parameterDefinitions = CopySection<FileFormat::Material::ParameterDefinition>(*buffer, candidate._pack.parameterDefinitionsOffset, candidate._pack.numParameterDefinitions);
        if (!candidate.Validate(error))
            return false;

        _pack = candidate._pack;
        _executionGroups = std::move(candidate._executionGroups);
        _programs = std::move(candidate._programs);
        _programLookups = std::move(candidate._programLookups);
        _parameterDefinitions = std::move(candidate._parameterDefinitions);
        _loaded = true;
        return true;
    }

    const FileFormat::Material::MaterialProgramRecord* MaterialProgramLibrary::Resolve(const MaterialAssetView& material)
    {
        ++_materialResolves;
        const auto lookup = std::lower_bound(_programLookups.begin(), _programLookups.end(), material.root.programKey,
            [](const FileFormat::Material::MaterialProgramLookup& left, FileFormat::Material::MaterialProgramKey key) { return left.programKey < key; });
        if (lookup == _programLookups.end() || lookup->programKey != material.root.programKey)
        {
            ++_missingPrograms;
            NC_LOG_ERROR("MATERIAL_PROGRAM missing key={} diagnostic_id={}", material.root.programKey, material.root.programID);
            return nullptr;
        }

        const FileFormat::Material::MaterialProgramRecord& program = _programs[lookup->programIndex];
        const bool definitionsMatch = program.numParameterDefinitions == material.parameters.size() &&
            (material.parameters.empty() || std::memcmp(_parameterDefinitions.data() + program.parameterDefinitionOffset, material.parameters.data(), material.parameters.size_bytes()) == 0);
        if (program.programID != material.root.programID || program.flags != material.root.flags || program.parameterBlockSize != material.root.parameterBlockSize ||
            program.parameterBlockAlignment != material.root.parameterBlockAlignment ||
            program.parameterLayoutHash != FileFormat::Material::CalculateParameterLayoutHash(material.parameters, material.root.parameterBlockSize) || !definitionsMatch)
        {
            ++_incompatibleMaterials;
            NC_LOG_ERROR("MATERIAL_PROGRAM incompatible key={} diagnostic_id={}/{} flags={}/{} block={}/{} alignment={}/{} definitions={}/{} layout_hash={}/{}", material.root.programKey,
                material.root.programID, program.programID, material.root.flags, program.flags, material.root.parameterBlockSize, program.parameterBlockSize, material.root.parameterBlockAlignment,
                program.parameterBlockAlignment, material.parameters.size(), program.numParameterDefinitions,
                FileFormat::Material::CalculateParameterLayoutHash(material.parameters, material.root.parameterBlockSize), program.parameterLayoutHash);
            return nullptr;
        }
        return &program;
    }

    const FileFormat::Material::MaterialExecutionGroup*
    MaterialProgramLibrary::GetExecutionGroup(u16 executionGroupID) const
    {
        const auto group = std::lower_bound(_executionGroups.begin(), _executionGroups.end(), executionGroupID, [](const FileFormat::Material::MaterialExecutionGroup& left, u16 ID) {
                return left.executionGroupID < ID;
            });
        return group != _executionGroups.end() && group->executionGroupID == executionGroupID ? &*group : nullptr;
    }

    MaterialProgramLibraryStats MaterialProgramLibrary::GetStats() const
    {
        return {.executionGroups = static_cast<u32>(_executionGroups.size()),
                .programs = static_cast<u32>(_programs.size()),
                .parameterDefinitions = static_cast<u32>(_parameterDefinitions.size()),
                .materialResolves = _materialResolves,
                .missingPrograms = _missingPrograms,
                .incompatibleMaterials = _incompatibleMaterials,
                .loaded = _loaded};
    }

    bool MaterialProgramLibrary::Validate(std::string& error) const
    {
        if (_pack.materialABIVersion != FileFormat::Material::ABI::VERSION)
        {
            error = "MaterialPack ABI mismatch";
            return false;
        }
        if (_programLookups.size() != _programs.size())
        {
            error = "MaterialPack lookup count mismatch";
            return false;
        }

        u16 previousGroupID = 0;
        bool hasPreviousGroup = false;
        std::array<const FileFormat::Material::MaterialExecutionGroup*, FileFormat::Material::ABI::EXECUTION_GROUP_COUNT> groups = {};
        std::array<std::vector<bool>, FileFormat::Material::ABI::EXECUTION_GROUP_COUNT> localBehaviors;
        for (const FileFormat::Material::MaterialExecutionGroup& group : _executionGroups)
        {
            if ((hasPreviousGroup && group.executionGroupID <= previousGroupID) || group.executionGroupID >= FileFormat::Material::ABI::EXECUTION_GROUP_COUNT || group.numPrograms == 0 ||
                group.resolveShaderPermutationHash == 0 || group.forwardShaderPermutationHash == 0 || group.numPrograms > std::numeric_limits<u16>::max())
            {
                error = "invalid MaterialPack execution-group table";
                return false;
            }
            groups[group.executionGroupID] = &group;
            localBehaviors[group.executionGroupID].resize(group.numPrograms, false);
            previousGroupID = group.executionGroupID;
            hasPreviousGroup = true;
        }

        for (const FileFormat::Material::MaterialProgramRecord& program : _programs)
        {
            if (program.programKey == FileFormat::Material::INVALID_MATERIAL_PROGRAM_KEY || program.parameterDefinitionOffset > _parameterDefinitions.size() ||
                program.numParameterDefinitions > _parameterDefinitions.size() - program.parameterDefinitionOffset)
            {
                error = "invalid MaterialPack program table";
                return false;
            }
            for (u32 rasterIndex = 0; rasterIndex < program.rasterRoutes.size(); ++rasterIndex)
            {
                const FileFormat::Material::MaterialProgramRoute& route = program.rasterRoutes[rasterIndex];
                if (route.executionGroupID >= FileFormat::Material::ABI::EXECUTION_GROUP_COUNT || static_cast<u32>(FileFormat::Material::ABI::GetExecutionGroupClass(route.executionGroupID)) / 2u != rasterIndex ||
                    !groups[route.executionGroupID] || route.groupLocalProgramID >= groups[route.executionGroupID]->numPrograms)
                {
                    error = "invalid MaterialPack raster route";
                    return false;
                }
                localBehaviors[route.executionGroupID][route.groupLocalProgramID] = true;
            }
        }
        for (const auto& behaviors : localBehaviors)
        {
            if (std::find(behaviors.begin(), behaviors.end(), false) != behaviors.end())
            {
                error = "non-contiguous MaterialPack group-local behavior IDs";
                return false;
            }
        }

        FileFormat::Material::MaterialProgramKey previousKey = 0;
        for (u32 lookupIndex = 0; lookupIndex < _programLookups.size(); ++lookupIndex)
        {
            const FileFormat::Material::MaterialProgramLookup& lookup = _programLookups[lookupIndex];
            if ((lookupIndex > 0 && lookup.programKey <= previousKey) || lookup.programIndex >= _programs.size() || _programs[lookup.programIndex].programKey != lookup.programKey)
            {
                error = "invalid MaterialPack key lookup";
                return false;
            }
            previousKey = lookup.programKey;
        }
        return true;
    }
}

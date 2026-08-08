#include "MaterialCookPlan.h"

#include <Base/Util/DebugHandler.h>

#include <algorithm>
#include <limits>
#include <map>
#include <set>
#include <tuple>
#include <type_traits>

#include <xxhash/xxhash64.h>

namespace
{
    using namespace MaterialCooking;

    MaterialCookPlanDiagnostic Diagnostic(MaterialCookPlanError error, std::string_view canonicalKey,
                                           u32 observed = 0,
                                           MaterialCompileError compileError = MaterialCompileError::None)
    {
        return {error, compileError, std::string(canonicalKey), observed};
    }

    class StableHasher
    {
      public:
        template <typename T>
        void Add(const T& value)
        {
            static_assert(std::is_trivially_copyable_v<T>);
            _hash = XXHash64::hash(&value, sizeof(value), _hash);
        }

        void Add(std::string_view value)
        {
            Add(static_cast<u64>(value.size()));
            _hash = XXHash64::hash(value.data(), value.size(), _hash);
        }

        void Add(std::span<const u8> value)
        {
            Add(static_cast<u64>(value.size()));
            _hash = XXHash64::hash(value.data(), value.size(), _hash);
        }

        u64 Get() const { return _hash; }

      private:
        u64 _hash = 0;
    };

    u64 SourceManifestFingerprint(std::span<const AuthoredMaterialProgramView* const> sources)
    {
        StableHasher hash;
        hash.Add(FileFormat::Material::ABI::VERSION);
        hash.Add(static_cast<u64>(sources.size()));
        for (const AuthoredMaterialProgramView* source : sources)
        {
            hash.Add(source->canonicalKey);
            const bool hasMaterial = source->material != nullptr;
            hash.Add(hasMaterial);
            if (hasMaterial)
            {
                hash.Add(source->material->programKey);
                hash.Add(source->material->programID);
                hash.Add(source->lightingModelID);
                hash.Add(static_cast<u8>(source->rasterClass));
                hash.Add(source->material->flags);
                hash.Add(source->material->parameterBlockSize);
                hash.Add(source->material->parameterBlockAlignment);
            }
            hash.Add(static_cast<u64>(source->parameters.size()));
            for (const FileFormat::Material::ParameterDefinition& parameter : source->parameters)
            {
                hash.Add(parameter.nameHash);
                hash.Add(parameter.byteOffset);
                hash.Add(parameter.byteSize);
                hash.Add(static_cast<u8>(parameter.type));
                hash.Add(parameter.arrayCount);
            }
            hash.Add(source->parameterData);
            hash.Add(static_cast<u64>(source->sourceUnits.size()));
            for (const LegacyModelSourceUnit& unit : source->sourceUnits)
            {
                hash.Add(unit.authoredShaderID);
                hash.Add(unit.textureCount);
                hash.Add(unit.layer);
                hash.Add(unit.flags);
                hash.Add(unit.blendMode);
                hash.Add(unit.sourceMaterialKind);
                hash.Add(unit.semanticFlags);
                hash.Add(unit.sourceMaterialFlags);
                hash.Add(unit.sourceBlendMode);
            }
        }
        return hash.Get();
    }

    u64 AuthoredRoutingFingerprint(
        std::span<const MaterialProgramAssignment* const> assignments)
    {
        StableHasher hash;
        hash.Add(FileFormat::Material::ABI::VERSION);
        hash.Add(static_cast<u64>(assignments.size()));
        for (const MaterialProgramAssignment* assignment : assignments)
        {
            hash.Add(assignment->canonicalKey);
            hash.Add(assignment->programFamily);
            hash.Add(assignment->materialSource);
            hash.Add(assignment->materialFunction);
            hash.Add(assignment->materialCoverageFunction);
            hash.Add(assignment->authoredUnitCount);
            for (u32 unit = 0; unit < assignment->authoredUnitCount; ++unit)
            {
                hash.Add(assignment->unitMaterialSources[unit]);
                hash.Add(assignment->unitMaterialFunctions[unit]);
                hash.Add(assignment->unitCoverageFunctions[unit]);
            }
        }
        return hash.Get();
    }

    bool HasSameBehavior(const CookedMaterialProgram& left,
                         const CookedMaterialProgram& right,
                         bool includeCoverage)
    {
        if (left.programFamily != right.programFamily ||
            left.materialSource != right.materialSource ||
            left.materialFunction != right.materialFunction ||
            (includeCoverage && left.materialCoverageFunction != right.materialCoverageFunction) ||
            left.program.parameterLayoutHash != right.program.parameterLayoutHash ||
            left.authoredUnitCount != right.authoredUnitCount)
            return false;

        for (u32 unit = 0; unit < left.authoredUnitCount; ++unit)
        {
            if (left.unitMaterialSources[unit] != right.unitMaterialSources[unit] ||
                left.unitMaterialFunctions[unit] != right.unitMaterialFunctions[unit] ||
                (includeCoverage && left.unitCoverageFunctions[unit] !=
                                        right.unitCoverageFunctions[unit]) ||
                left.program.units[unit].textureOffset != right.program.units[unit].textureOffset ||
                (unit > 0 && left.program.units[unit].blendMode != right.program.units[unit].blendMode))
                return false;
        }
        return true;
    }

    FileFormat::Material::ABI::ExecutionGroup BaseExecutionGroup(
        FileFormat::Material::RasterClass rasterClass, bool layered)
    {
        const u16 groupClass = static_cast<u16>(rasterClass) * 2u + (layered ? 1u : 0u);
        return static_cast<FileFormat::Material::ABI::ExecutionGroup>(groupClass);
    }
}

namespace MaterialCooking
{
    MaterialCookPlan MaterialCookPlanBuilder::Build(
        std::span<const AuthoredMaterialProgramView> sourcePrograms,
        std::span<const MaterialProgramAssignment> assignments,
        bool requireCompleteSourceSemantics)
    {
        MaterialCookPlan result;

        std::vector<const MaterialProgramAssignment*> orderedAssignments;
        orderedAssignments.reserve(assignments.size());
        for (const MaterialProgramAssignment& assignment : assignments)
            orderedAssignments.push_back(&assignment);
        std::sort(orderedAssignments.begin(), orderedAssignments.end(),
                  [](const auto* left, const auto* right) {
                      return left->canonicalKey < right->canonicalKey;
                  });

        std::map<std::string_view, const MaterialProgramAssignment*> assignmentByKey;
        std::set<std::string_view> programFamilies;
        for (const MaterialProgramAssignment& assignment : assignments)
        {
            if (assignment.canonicalKey.empty())
                result.diagnostics.push_back(
                    Diagnostic(MaterialCookPlanError::EmptyCanonicalKey, assignment.canonicalKey));
            else if (assignment.programFamily.empty())
                result.diagnostics.push_back(
                    Diagnostic(MaterialCookPlanError::MissingProgramFamily, assignment.canonicalKey));
            else if (assignment.materialSource.empty())
                result.diagnostics.push_back(
                    Diagnostic(MaterialCookPlanError::MissingMaterialSource, assignment.canonicalKey));
            else if (!assignment.materialSource.ends_with(".inc.slang"))
                result.diagnostics.push_back(Diagnostic(
                    MaterialCookPlanError::InvalidMaterialSourceExtension,
                    assignment.canonicalKey));
            else if (assignment.materialFunction.empty())
                result.diagnostics.push_back(
                    Diagnostic(MaterialCookPlanError::MissingMaterialFunction, assignment.canonicalKey));
            else if (!assignmentByKey.emplace(assignment.canonicalKey, &assignment).second)
                result.diagnostics.push_back(
                    Diagnostic(MaterialCookPlanError::DuplicateAssignment, assignment.canonicalKey));
            else
                programFamilies.emplace(assignment.programFamily);
        }

        if (programFamilies.size() > FileFormat::Material::ABI::MAX_PROGRAM_FAMILIES)
        {
            result.diagnostics.push_back(Diagnostic(
                MaterialCookPlanError::InvalidExecutionGroup, {},
                static_cast<u32>(programFamilies.size())));
        }
        std::map<std::string_view, u16> familyIndices;
        for (std::string_view family : programFamilies)
            familyIndices.emplace(family, static_cast<u16>(familyIndices.size()));

        std::vector<const AuthoredMaterialProgramView*> orderedSources;
        orderedSources.reserve(sourcePrograms.size());
        for (const AuthoredMaterialProgramView& source : sourcePrograms)
            orderedSources.push_back(&source);
        std::sort(orderedSources.begin(), orderedSources.end(),
                  [](const auto* left, const auto* right) {
                      return left->canonicalKey < right->canonicalKey;
                  });
        result.sourceManifestFingerprint = SourceManifestFingerprint(orderedSources);
        result.routingFingerprint = AuthoredRoutingFingerprint(orderedAssignments);
        StableHasher functionalHash;
        functionalHash.Add(result.sourceManifestFingerprint);
        functionalHash.Add(result.routingFingerprint);
        result.functionalCookFingerprint = functionalHash.Get();

        std::string_view previousKey;
        bool hasPreviousKey = false;
        std::map<FileFormat::Material::MaterialProgramKey, std::string_view> sourceProgramKeys;
        for (const AuthoredMaterialProgramView* source : orderedSources)
        {
            if (source->canonicalKey.empty())
            {
                result.diagnostics.push_back(
                    Diagnostic(MaterialCookPlanError::EmptyCanonicalKey, source->canonicalKey));
                continue;
            }
            if (hasPreviousKey && source->canonicalKey == previousKey)
            {
                result.diagnostics.push_back(
                    Diagnostic(MaterialCookPlanError::DuplicateSourceProgram, source->canonicalKey));
                continue;
            }
            previousKey = source->canonicalKey;
            hasPreviousKey = true;

            const auto assignment = assignmentByKey.find(source->canonicalKey);
            if (assignment == assignmentByKey.end())
            {
                result.diagnostics.push_back(
                    Diagnostic(MaterialCookPlanError::MissingAssignment, source->canonicalKey));
                continue;
            }
            if (source->material == nullptr)
            {
                result.diagnostics.push_back(
                    Diagnostic(MaterialCookPlanError::InvalidSourceProgram, source->canonicalKey));
                continue;
            }
            if (!sourceProgramKeys.emplace(source->material->programKey, source->canonicalKey).second)
            {
                result.diagnostics.push_back(Diagnostic(
                    MaterialCookPlanError::DuplicateSourceProgramKey, source->canonicalKey));
                continue;
            }
            if (source->lightingModelID >=
                static_cast<u16>(FileFormat::Material::ABI::LightingModel::Count))
            {
                result.diagnostics.push_back(Diagnostic(
                    MaterialCookPlanError::UnsupportedLightingModel,
                    source->canonicalKey, source->lightingModelID));
                continue;
            }

            const bool layered = source->sourceUnits.size() > 1;
            const auto groupClass = BaseExecutionGroup(source->rasterClass, layered);
            const MaterialCompileResult compiled = LegacyMaterialCompiler::Compile(
                *source->material, source->parameters, source->sourceUnits,
                source->lightingModelID, source->rasterClass,
                static_cast<u16>(groupClass), requireCompleteSourceSemantics);
            if (!compiled)
            {
                result.diagnostics.push_back(Diagnostic(
                    MaterialCookPlanError::InvalidSourceProgram, source->canonicalKey,
                    compiled.observed, compiled.error));
                continue;
            }

            const MaterialProgramAssignment& authored = *assignment->second;
            if (source->rasterClass == FileFormat::Material::RasterClass::AlphaTest &&
                authored.materialCoverageFunction.empty())
            {
                NC_LOG_WARNING("Material '{}' is used as alpha-tested without a coverage function; coverage defaults to keep",
                               source->canonicalKey);
            }

            CookedMaterialProgram& program = result.programs.emplace_back();
            program.canonicalKey = source->canonicalKey;
            program.programFamily = authored.programFamily;
            program.materialSource = authored.materialSource;
            program.materialFunction = authored.materialFunction;
            program.materialCoverageFunction = authored.materialCoverageFunction;
            program.program = compiled.program;
            const u16 familyIndex = familyIndices.at(authored.programFamily);
            program.program.executionGroupID =
                FileFormat::Material::ABI::MakeExecutionGroup(familyIndex, groupClass);
            program.parameters.assign(source->parameters.begin(), source->parameters.end());
            program.authoredUnitCount = authored.authoredUnitCount;
            for (u32 unit = 0; unit < program.authoredUnitCount; ++unit)
            {
                program.unitMaterialSources[unit] = authored.unitMaterialSources[unit];
                program.unitMaterialFunctions[unit] = authored.unitMaterialFunctions[unit];
                program.unitCoverageFunctions[unit] = authored.unitCoverageFunctions[unit];
            }
        }

        for (u16 familyIndex = 0; familyIndex < familyIndices.size(); ++familyIndex)
        {
            for (u16 groupClass = 0;
                 groupClass < FileFormat::Material::ABI::EXECUTION_GROUP_CLASS_COUNT;
                 ++groupClass)
            {
                const u16 group = FileFormat::Material::ABI::MakeExecutionGroup(
                    familyIndex,
                    static_cast<FileFormat::Material::ABI::ExecutionGroup>(groupClass));
                const bool includeCoverage = groupClass == 2u || groupClass == 3u;
                std::vector<const CookedMaterialProgram*> behaviors;
                for (CookedMaterialProgram& program : result.programs)
                {
                    if (FileFormat::Material::ABI::GetProgramFamily(
                            program.program.executionGroupID) != familyIndex ||
                        (program.program.executionGroupID % 2u) != (groupClass % 2u))
                        continue;

                    const auto existing = std::find_if(
                        behaviors.begin(), behaviors.end(),
                        [&program, includeCoverage](const CookedMaterialProgram* behavior) {
                            return HasSameBehavior(*behavior, program, includeCoverage);
                        });
                    u16 localID = 0;
                    if (existing == behaviors.end())
                    {
                        if (behaviors.size() >= std::numeric_limits<u16>::max())
                        {
                            result.diagnostics.push_back(Diagnostic(
                                MaterialCookPlanError::InvalidExecutionGroup,
                                program.canonicalKey, group));
                            continue;
                        }
                        localID = static_cast<u16>(behaviors.size());
                        behaviors.push_back(&program);
                    }
                    else
                    {
                        localID = static_cast<u16>(
                            std::distance(behaviors.begin(), existing));
                    }
                    const u32 rasterIndex = groupClass / 2u;
                    program.rasterRoutes[rasterIndex] = {group, localID};
                }
            }
        }

        for (CookedMaterialProgram& program : result.programs)
        {
            const u32 rasterIndex = static_cast<u32>(
                FileFormat::Material::ABI::GetExecutionGroupClass(
                    program.program.executionGroupID)) / 2u;
            program.groupLocalProgramID =
                program.rasterRoutes[rasterIndex].groupLocalProgramID;
        }
        std::sort(result.programs.begin(), result.programs.end(),
                  [](const CookedMaterialProgram& left, const CookedMaterialProgram& right) {
                      return left.canonicalKey < right.canonicalKey;
                  });
        return result;
    }

    const char* MaterialCookPlanBuilder::Describe(MaterialCookPlanError error)
    {
        switch (error)
        {
        case MaterialCookPlanError::None: return "none";
        case MaterialCookPlanError::EmptyCanonicalKey: return "empty_canonical_key";
        case MaterialCookPlanError::DuplicateSourceProgram: return "duplicate_source_program";
        case MaterialCookPlanError::DuplicateSourceProgramKey: return "duplicate_source_program_key";
        case MaterialCookPlanError::DuplicateAssignment: return "duplicate_assignment";
        case MaterialCookPlanError::MissingAssignment: return "missing_assignment";
        case MaterialCookPlanError::MissingMaterialSource: return "missing_material_source";
        case MaterialCookPlanError::MissingProgramFamily: return "missing_program_family";
        case MaterialCookPlanError::InvalidMaterialSourceExtension: return "invalid_material_source_extension";
        case MaterialCookPlanError::MissingMaterialFunction: return "missing_material_function";
        case MaterialCookPlanError::MissingMaterialCoverageFunction: return "missing_material_coverage_function";
        case MaterialCookPlanError::UnsupportedLightingModel: return "unsupported_lighting_model";
        case MaterialCookPlanError::InvalidExecutionGroup: return "invalid_execution_group";
        case MaterialCookPlanError::InvalidSourceProgram: return "invalid_source_program";
        }
        return "unknown";
    }
}

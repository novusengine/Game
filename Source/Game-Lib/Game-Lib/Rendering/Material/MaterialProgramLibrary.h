#pragma once

#include "MaterialAssetReader.h"

#include <FileFormat/Novus/Model/MaterialPack.h>

#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace MaterialLoading
{
    struct MaterialProgramLibraryStats
    {
        u32 executionGroups = 0;
        u32 programs = 0;
        u32 parameterDefinitions = 0;
        u32 materialResolves = 0;
        u32 missingPrograms = 0;
        u32 incompatibleMaterials = 0;
        bool loaded = false;
    };

    // Owns the CPU-side baked Material program routing and parameter reflection library.
    // Material loads join their stable program keys here before publishing GPU records.
    class MaterialProgramLibrary
    {
      public:
        bool Load(const std::filesystem::path& path, std::string& error);
        const FileFormat::Material::MaterialProgramRecord* Resolve(
            const MaterialAssetView& material);
        const FileFormat::Material::MaterialExecutionGroup* GetExecutionGroup(
            u16 executionGroupID) const;
        MaterialProgramLibraryStats GetStats() const;

      private:
        bool Validate(std::string& error) const;

        FileFormat::Material::MaterialPack _pack;
        std::vector<FileFormat::Material::MaterialExecutionGroup> _executionGroups;
        std::vector<FileFormat::Material::MaterialProgramRecord> _programs;
        std::vector<FileFormat::Material::MaterialProgramLookup> _programLookups;
        std::vector<FileFormat::Material::ParameterDefinition> _parameterDefinitions;
        u32 _materialResolves = 0;
        u32 _missingPrograms = 0;
        u32 _incompatibleMaterials = 0;
        bool _loaded = false;
    };
}

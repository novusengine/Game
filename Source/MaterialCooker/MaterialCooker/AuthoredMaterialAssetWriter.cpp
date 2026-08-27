#include "AuthoredMaterialAssetWriter.h"

#include <Base/Memory/Bytebuffer.h>
#include <FileFormat/Novus/Model/Material.h>

#include <algorithm>
#include <fstream>

#include <xxhash/xxhash64.h>

namespace
{
    bool IsSafeAssetPath(std::string_view path, std::string_view extension)
    {
        const std::filesystem::path parsed(path);
        return !path.empty() && !parsed.is_absolute() && parsed.extension() == extension && parsed.lexically_normal().generic_string() == path && path.find("..") == std::string_view::npos;
    }

    bool WriteBuffer(const std::filesystem::path& path, const std::shared_ptr<Bytebuffer>& buffer, std::string& error)
    {
        std::error_code filesystemError;
        std::filesystem::create_directories(path.parent_path(), filesystemError);
        if (filesystemError)
        {
            error = "failed to create authored Material asset directory: " + path.parent_path().string();
            return false;
        }
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output.write(reinterpret_cast<const char*>(buffer->GetDataPointer()), static_cast<std::streamsize>(buffer->writtenData));
        if (!output.good())
        {
            error = "failed to write authored Material asset: " + path.string();
            return false;
        }
        return true;
    }

    FileFormat::AssetID HashAssetPath(std::string_view path)
    {
        return XXHash64::hash(path.data(), path.size(), 0);
    }
}

namespace MaterialCooking
{
    bool AuthoredMaterialAssetWriter::Save(const std::filesystem::path& outputDirectory, const MaterialSourceRegistry& registry, const MaterialCookPlan& plan, std::string& error)
    {
        error.clear();
        for (const AuthoredMaterialSource& source : registry.materials)
        {
            if (!source.IsNative())
                continue;
            if (!IsSafeAssetPath(source.assetPath, FileFormat::Material::MATERIAL_FILE_EXTENSION))
            {
                error = "invalid authored Material asset path: " + source.assetPath;
                return false;
            }
            const auto cooked = std::find_if(plan.programs.begin(), plan.programs.end(), [&source](const CookedMaterialProgram& program) { return program.canonicalKey == source.name; });
            if (cooked == plan.programs.end())
            {
                error = "authored Material was not present in the cook plan: " + source.name;
                return false;
            }

            FileFormat::Material::MaterialAsset material;
            material.programKey = cooked->program.sourceProgramKey;
            material.programID = cooked->program.sourceProgramID;
            material.flags = cooked->program.flags;
            material.parameterBlockAlignment = cooked->program.parameterBlockAlignment;
            material.textureSlotCount = cooked->program.textureCount;
            FileFormat::Material::MaterialData materialData;
            materialData.parameters = cooked->parameters;
            std::shared_ptr<Bytebuffer> materialBuffer = Bytebuffer::BorrowRuntime(material.GetSerializedSize(materialData));
            if (!material.Save(materialBuffer, materialData) || !WriteBuffer(outputDirectory / std::filesystem::path(source.assetPath), materialBuffer, error))
            {
                if (error.empty())
                    error = "failed to serialize authored Material: " + source.name;
                return false;
            }

            const FileFormat::AssetID materialAssetID = HashAssetPath(source.assetPath);
            for (const AuthoredMaterialInstance& authoredInstance : source.instances)
            {
                if (!IsSafeAssetPath(authoredInstance.path, FileFormat::Material::MATERIAL_INSTANCE_FILE_EXTENSION))
                {
                    error = "invalid authored MaterialInstance asset path: " + authoredInstance.path;
                    return false;
                }
                FileFormat::Material::MaterialInstanceAsset instance;
                instance.materialAssetID = materialAssetID;
                instance.parameterLayoutHash = cooked->program.parameterLayoutHash;
                instance.flags = authoredInstance.flags;
                instance.lightingModelID = authoredInstance.lightingModelID;
                instance.rasterClass = source.rasterClass;
                FileFormat::Material::MaterialInstanceData instanceData;
                std::shared_ptr<Bytebuffer> instanceBuffer = Bytebuffer::BorrowRuntime(instance.GetSerializedSize(instanceData));
                if (!instance.Save(instanceBuffer, instanceData) || !WriteBuffer(outputDirectory / std::filesystem::path(authoredInstance.path), instanceBuffer, error))
                {
                    if (error.empty())
                        error = "failed to serialize authored MaterialInstance: " + authoredInstance.name;
                    return false;
                }
            }
        }
        return true;
    }
}

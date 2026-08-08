#include "MaterialSourceRegistry.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <fstream>
#include <iterator>
#include <map>
#include <set>

namespace
{
    using namespace MaterialCooking;

    class MaterialDeclarationParser
    {
      public:
        explicit MaterialDeclarationParser(std::string_view source) : _source(source) {}

        bool Parse(AuthoredMaterialSource& material, std::string& error)
        {
            if (!ConsumeKeyword("material") || !ParseIdentifier(material.name) || !Consume('{'))
                return Fail("expected 'material <Name> {'", error);

            bool hasProgramFamily = false;
            while (true)
            {
                SkipWhitespace();
                if (Consume('}'))
                    break;

                std::string property;
                if (!ParseIdentifier(property) || !Consume('='))
                    return Fail("expected a Material property assignment", error);

                if (property == "programFamily")
                {
                    if (hasProgramFamily || !ParseIdentifier(material.programFamily) || !Consume(';'))
                        return Fail("invalid or duplicate programFamily", error);
                    hasProgramFamily = true;
                }
                else if (property == "pixelShaderIDs")
                {
                    if (!material.pixelShaderIDs.empty() || !ParsePixelShaderIDs(material.pixelShaderIDs) ||
                        !Consume(';'))
                        return Fail("invalid or duplicate pixelShaderIDs", error);
                }
                else
                {
                  return Fail("unsupported Material property '" + property +
                                  "'",
                              error);
                }
            }

            if (!hasProgramFamily)
                return Fail("missing programFamily", error);

            material.sourceBody = std::string(_source.substr(_position));
            material.materialFunction = "EvaluateMaterial_" + material.name;
            material.materialCoverageFunction = "EvaluateCoverage_" + material.name;
            if (material.sourceBody.find("SurfaceDescription EvaluateMaterial(") == std::string::npos)
                return Fail("missing EvaluateMaterial", error);
            if (material.sourceBody.find("float EvaluateCoverage(") == std::string::npos)
                material.materialCoverageFunction.clear();
            return true;
        }

      private:
        void SkipWhitespace()
        {
            while (_position < _source.size())
            {
                if (std::isspace(static_cast<unsigned char>(_source[_position])))
                {
                    ++_position;
                    continue;
                }
                if (_source.substr(_position, 2) == "//")
                {
                    _position = _source.find('\n', _position + 2);
                    if (_position == std::string_view::npos)
                        _position = _source.size();
                    continue;
                }
                if (_source.substr(_position, 2) == "/*")
                {
                    const size_t end = _source.find("*/", _position + 2);
                    _position = end == std::string_view::npos ? _source.size() : end + 2;
                    continue;
                }
                break;
            }
        }

        bool Consume(char character)
        {
            SkipWhitespace();
            if (_position >= _source.size() || _source[_position] != character)
                return false;
            ++_position;
            return true;
        }

        bool ConsumeKeyword(std::string_view keyword)
        {
            SkipWhitespace();
            if (_source.substr(_position, keyword.size()) != keyword)
                return false;
            const size_t end = _position + keyword.size();
            if (end < _source.size() && IsIdentifierCharacter(_source[end]))
                return false;
            _position = end;
            return true;
        }

        bool ParseIdentifier(std::string& value)
        {
            SkipWhitespace();
            if (_position >= _source.size() ||
                !(std::isalpha(static_cast<unsigned char>(_source[_position])) ||
                  _source[_position] == '_'))
                return false;
            const size_t start = _position++;
            while (_position < _source.size() && IsIdentifierCharacter(_source[_position]))
                ++_position;
            value.assign(_source.substr(start, _position - start));
            return true;
        }

        bool ParsePixelShaderIDs(std::vector<u8>& values)
        {
            if (!Consume('['))
                return false;
            std::set<u8> uniqueValues;
            while (true)
            {
                SkipWhitespace();
                const char* begin = _source.data() + _position;
                const char* end = _source.data() + _source.size();
                u32 value = 0;
                const auto [next, conversionError] = std::from_chars(begin, end, value);
                if (conversionError != std::errc{} || value > 255u ||
                    !uniqueValues.emplace(static_cast<u8>(value)).second)
                    return false;
                values.push_back(static_cast<u8>(value));
                _position = static_cast<size_t>(next - _source.data());
                SkipWhitespace();
                if (Consume(']'))
                    return true;
                if (!Consume(','))
                    return false;
            }
        }

        bool Fail(std::string message, std::string& error) const
        {
            const u32 line = 1u + static_cast<u32>(
                std::count(_source.begin(), _source.begin() + _position, '\n'));
            error = std::move(message) + " at line " + std::to_string(line);
            return false;
        }

        static bool IsIdentifierCharacter(char character)
        {
            return std::isalnum(static_cast<unsigned char>(character)) || character == '_';
        }

        std::string_view _source;
        size_t _position = 0;
    };

    bool ReadText(const std::filesystem::path& path, std::string& text)
    {
        std::ifstream input(path, std::ios::binary);
        if (!input)
            return false;
        text.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
        return input.good() || input.eof();
    }

    bool WriteIfChanged(const std::filesystem::path& path, std::string_view text)
    {
        std::string existing;
        if (ReadText(path, existing) && existing == text)
            return true;
        std::error_code filesystemError;
        std::filesystem::create_directories(path.parent_path(), filesystemError);
        if (filesystemError)
            return false;
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output.write(text.data(), static_cast<std::streamsize>(text.size()));
        return output.good();
    }
}

namespace MaterialCooking
{
    MaterialSourceRegistryLoadResult MaterialSourceRegistryIO::Load(
        const std::filesystem::path& authoredDirectory,
        const std::filesystem::path& shaderSourceDirectory)
    {
        MaterialSourceRegistryLoadResult result;
        std::error_code filesystemError;
        if (!std::filesystem::is_directory(authoredDirectory, filesystemError))
        {
            result.error = "Material authored source directory does not exist";
            return result;
        }

        std::vector<std::filesystem::path> paths;
        for (std::filesystem::recursive_directory_iterator iterator(authoredDirectory, filesystemError), end;
             iterator != end && !filesystemError; iterator.increment(filesystemError))
        {
            if (iterator->is_regular_file() && iterator->path().filename().string().ends_with(".mat.slang"))
                paths.push_back(iterator->path());
        }
        if (filesystemError)
        {
            result.error = "failed to enumerate authored Material sources";
            return result;
        }
        std::sort(paths.begin(), paths.end());

        std::set<std::string> names;
        std::set<u8> pixelShaderIDs;
        for (const std::filesystem::path& path : paths)
        {
            std::string source;
            if (!ReadText(path, source))
            {
                result.error = "failed to read authored Material source: " + path.string();
                return result;
            }

            AuthoredMaterialSource& material = result.registry.materials.emplace_back();
            MaterialDeclarationParser parser(source);
            if (!parser.Parse(material, result.error))
            {
                result.error = path.string() + ": " + result.error;
                return result;
            }
            if (!names.emplace(material.name).second)
            {
                result.error = "duplicate authored Material name: " + material.name;
                return result;
            }
            for (u8 pixelShaderID : material.pixelShaderIDs)
            {
                if (!pixelShaderIDs.emplace(pixelShaderID).second)
                {
                    result.error = "duplicate pixelShaderID in authored Materials: " +
                        std::to_string(pixelShaderID);
                    return result;
                }
            }

            std::filesystem::path relative = std::filesystem::relative(path, shaderSourceDirectory,
                                                                        filesystemError);
            if (filesystemError || relative.empty() ||
                (!relative.empty() && *relative.begin() == ".."))
            {
                result.error = "authored Material source is outside the shader source directory: " +
                    path.string();
                return result;
            }
            material.materialSource = relative.generic_string();
            relative = std::filesystem::relative(path, authoredDirectory, filesystemError);
            if (filesystemError)
            {
                result.error = "failed to make authored Material path relative";
                return result;
            }
            std::string generated = relative.generic_string();
            generated.resize(generated.size() - std::string_view(".mat.slang").size());
            material.generatedSource = "Generated/MaterialSources/" + generated + ".inc.slang";
        }
        return result;
    }

    MaterialSourceResolveResult MaterialSourceRegistryIO::Resolve(
        const MaterialSourceRegistry& registry,
        std::span<const AuthoredMaterialProgramView> sourcePrograms)
    {
        MaterialSourceResolveResult result;
        std::map<u8, const AuthoredMaterialSource*> byPixelShaderID;
        std::map<std::string_view, const AuthoredMaterialSource*> byName;
        for (const AuthoredMaterialSource& material : registry.materials)
        {
            byName.emplace(material.name, &material);
            for (u8 pixelShaderID : material.pixelShaderIDs)
                byPixelShaderID.emplace(pixelShaderID, &material);
        }

        result.programs.reserve(sourcePrograms.size());
        for (const AuthoredMaterialProgramView& source : sourcePrograms)
        {
            MaterialProgramAssignment& resolved = result.programs.emplace_back();
            resolved.canonicalKey = source.canonicalKey;

            if (source.sourceUnits.empty())
            {
                const auto authored = byName.find(source.canonicalKey);
                if (authored == byName.end())
                {
                    result.error = "Material program has no authored source: " +
                        std::string(source.canonicalKey);
                    return result;
                }
                resolved.programFamily = authored->second->programFamily;
                resolved.materialSource = authored->second->generatedSource;
                resolved.materialFunction = authored->second->materialFunction;
                resolved.materialCoverageFunction = authored->second->materialCoverageFunction;
                continue;
            }

            if (source.sourceUnits.size() > resolved.unitMaterialFunctions.size())
            {
                result.error = "Material program contains too many authored units: " +
                    std::string(source.canonicalKey);
                return result;
            }
            resolved.authoredUnitCount = static_cast<u8>(source.sourceUnits.size());
            for (u32 unitIndex = 0; unitIndex < source.sourceUnits.size(); ++unitIndex)
            {
                const LegacyModelSourceUnit& unit = source.sourceUnits[unitIndex];
                u8 pixelShaderID = 0;
                u8 vertexShaderID = 0;
                if (!LegacyMaterialCompiler::ResolveShaderIDs(
                        unit.authoredShaderID, unit.textureCount, pixelShaderID, vertexShaderID))
                {
                    result.error = "unsupported source shader in Material program: " +
                        std::string(source.canonicalKey);
                    return result;
                }
                const auto authored = byPixelShaderID.find(pixelShaderID);
                if (authored == byPixelShaderID.end())
                {
                    result.error = "source pixel shader has no authored Material: " +
                        std::to_string(pixelShaderID);
                    return result;
                }
                if (unitIndex == 0)
                    resolved.programFamily = authored->second->programFamily;
                else if (resolved.programFamily != authored->second->programFamily)
                {
                    result.error = "composed Material spans multiple program families: " +
                        std::string(source.canonicalKey);
                    return result;
                }
                resolved.unitMaterialSources[unitIndex] = authored->second->generatedSource;
                resolved.unitMaterialFunctions[unitIndex] = authored->second->materialFunction;
                resolved.unitCoverageFunctions[unitIndex] =
                    authored->second->materialCoverageFunction;
            }
            resolved.materialSource = resolved.unitMaterialSources[0];
            if (resolved.authoredUnitCount == 1)
            {
                resolved.materialFunction = resolved.unitMaterialFunctions[0];
                resolved.materialCoverageFunction = resolved.unitCoverageFunctions[0];
            }
            else
            {
                resolved.materialFunction = "EvaluateMaterial_ComposedCompatibility";
                resolved.materialCoverageFunction = "EvaluateCoverage_ComposedCompatibility";
            }
        }
        return result;
    }

    bool MaterialSourceRegistryIO::WriteGeneratedSources(const MaterialSourceRegistry &registry,
                                                         const std::filesystem::path &shaderSourceDirectory,
                                                         std::string &error)
    {
        for (const AuthoredMaterialSource &material : registry.materials)
        {
            const std::string guard = "GENERATED_MATERIAL_SOURCE_" + material.name + "_INCLUDED";
            std::string source = "#ifndef " + guard + "\n#define " + guard + "\n" + "#define EvaluateMaterial " +
                                 material.materialFunction + "\n";
            if (!material.materialCoverageFunction.empty())
                source += "#define EvaluateCoverage " + material.materialCoverageFunction + "\n";
            source += material.sourceBody;
            source += "\n#undef EvaluateMaterial\n";
            if (!material.materialCoverageFunction.empty())
                source += "#undef EvaluateCoverage\n";
            source += "#endif\n";
            if (!WriteIfChanged(shaderSourceDirectory / material.generatedSource, source))
            {
                error = "failed to write stripped Material source: " + material.generatedSource;
                return false;
            }
        }
        return true;
    }
}

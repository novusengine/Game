#pragma once

#include <Base/Types.h>

#include <limits>
#include <string>
#include <string_view>

namespace AssetLoading
{
    enum class DiagnosticCode : u8
    {
        None,
        PayloadTooSmall,
        InvalidHeader,
        InvalidRootSection,
        InvalidRange,
        EmptyRequiredRange,
        CountMismatch,
        InvalidIndex,
        InvalidValue,
        NonFiniteValue,
        UnsupportedEnum,
        UnsupportedFlags,
        ReservedValueNonZero,
        MissingRequiredReference,
        DuplicateParameter,
        OverlappingParameters,
        LayoutHashMismatch,
        UnsupportedVersion,
        UnsupportedCodec,
        MissingDependency,
        IncompatibleDomain,
        StaleHandle
    };

    enum class DiagnosticStage : u8
    {
        None,
        Read,
        Validate,
        ResolveDependency,
        Compatibility,
        Registry
    };

    struct Diagnostic
    {
        static constexpr u32 NO_INDEX = std::numeric_limits<u32>::max();

        DiagnosticCode code = DiagnosticCode::None;
        std::string_view field;
        u32 index = NO_INDEX;
        u64 observed = 0;
        u64 expected = 0;
        u64 requestedAssetID = std::numeric_limits<u64>::max();
        u64 dependencyAssetID = std::numeric_limits<u64>::max();
        DiagnosticStage stage = DiagnosticStage::None;

        explicit operator bool() const
        {
            return code != DiagnosticCode::None;
        }
    };

    const char* ToString(DiagnosticCode code);
    const char* ToString(DiagnosticStage stage);
    std::string Describe(const Diagnostic& diagnostic);
} // namespace AssetLoading

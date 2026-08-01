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
        LayoutHashMismatch
    };

    struct Diagnostic
    {
        static constexpr u32 NO_INDEX = std::numeric_limits<u32>::max();

        DiagnosticCode code = DiagnosticCode::None;
        std::string_view field;
        u32 index = NO_INDEX;
        u64 observed = 0;
        u64 expected = 0;

        explicit operator bool() const
        {
            return code != DiagnosticCode::None;
        }
    };

    const char* ToString(DiagnosticCode code);
    std::string Describe(const Diagnostic& diagnostic);
} // namespace AssetLoading

#include "AssetDiagnostic.h"

#include <sstream>

namespace AssetLoading
{
    const char* ToString(DiagnosticCode code)
    {
        switch (code)
        {
        case DiagnosticCode::None:
            return "none";
        case DiagnosticCode::PayloadTooSmall:
            return "payload_too_small";
        case DiagnosticCode::InvalidHeader:
            return "invalid_header";
        case DiagnosticCode::InvalidRootSection:
            return "invalid_root_section";
        case DiagnosticCode::InvalidRange:
            return "invalid_range";
        case DiagnosticCode::EmptyRequiredRange:
            return "empty_required_range";
        case DiagnosticCode::CountMismatch:
            return "count_mismatch";
        case DiagnosticCode::InvalidIndex:
            return "invalid_index";
        case DiagnosticCode::InvalidValue:
            return "invalid_value";
        case DiagnosticCode::NonFiniteValue:
            return "non_finite_value";
        case DiagnosticCode::UnsupportedEnum:
            return "unsupported_enum";
        case DiagnosticCode::UnsupportedFlags:
            return "unsupported_flags";
        case DiagnosticCode::ReservedValueNonZero:
            return "reserved_value_non_zero";
        case DiagnosticCode::MissingRequiredReference:
            return "missing_required_reference";
        case DiagnosticCode::DuplicateParameter:
            return "duplicate_parameter";
        case DiagnosticCode::OverlappingParameters:
            return "overlapping_parameters";
        case DiagnosticCode::LayoutHashMismatch:
            return "layout_hash_mismatch";
        }

        return "unknown";
    }

    std::string Describe(const Diagnostic& diagnostic)
    {
        std::ostringstream stream;
        stream << ToString(diagnostic.code);
        if (!diagnostic.field.empty())
            stream << " field=" << diagnostic.field;
        if (diagnostic.index != Diagnostic::NO_INDEX)
            stream << " index=" << diagnostic.index;
        stream << " observed=" << diagnostic.observed << " expected=" << diagnostic.expected;
        return stream.str();
    }
} // namespace AssetLoading

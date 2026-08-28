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
        case DiagnosticCode::UnsupportedVersion:
            return "unsupported_version";
        case DiagnosticCode::UnsupportedCodec:
            return "unsupported_codec";
        case DiagnosticCode::MissingDependency:
            return "missing_dependency";
        case DiagnosticCode::IncompatibleDomain:
            return "incompatible_domain";
        case DiagnosticCode::StaleHandle:
            return "stale_handle";
        }

        return "unknown";
    }

    const char* ToString(DiagnosticStage stage)
    {
        switch (stage)
        {
        case DiagnosticStage::None:
            return "none";
        case DiagnosticStage::Read:
            return "read";
        case DiagnosticStage::Validate:
            return "validate";
        case DiagnosticStage::ResolveDependency:
            return "resolve_dependency";
        case DiagnosticStage::Compatibility:
            return "compatibility";
        case DiagnosticStage::Registry:
            return "registry";
        }

        return "unknown";
    }

    std::string Describe(const Diagnostic& diagnostic)
    {
        std::ostringstream stream;
        stream << ToString(diagnostic.code);
        if (diagnostic.stage != DiagnosticStage::None)
            stream << " stage=" << ToString(diagnostic.stage);
        if (!diagnostic.field.empty())
            stream << " field=" << diagnostic.field;
        if (diagnostic.index != Diagnostic::NO_INDEX)
            stream << " index=" << diagnostic.index;
        stream << " observed=" << diagnostic.observed << " expected=" << diagnostic.expected;
        if (diagnostic.requestedAssetID != std::numeric_limits<u64>::max())
            stream << " requested_asset=" << diagnostic.requestedAssetID;
        if (diagnostic.dependencyAssetID != std::numeric_limits<u64>::max())
            stream << " dependency_asset=" << diagnostic.dependencyAssetID;
        return stream.str();
    }
} // namespace AssetLoading

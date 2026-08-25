#include "NsightGPUTrace.h"

#include <Base/Util/DebugHandler.h>

#include <NGFX_GPUTrace_Vulkan.h>

namespace
{
    const char* ResultName(NGFX_Result result)
    {
        switch (result)
        {
        case NGFX_Result_Success: return "success";
        case NGFX_Result_NotImplemented: return "not-implemented";
        case NGFX_Result_LibNotFound: return "library-not-found";
        case NGFX_Result_InvalidLib: return "invalid-library";
        case NGFX_Result_DifferentActivityInjected: return "different-activity-injected";
        case NGFX_Result_InvalidParameter: return "invalid-parameter";
        case NGFX_Result_InvalidState: return "invalid-state";
        case NGFX_Result_UnspecifiedError: return "unspecified-error";
        case NGFX_Result_Timeout: return "timeout";
        default: return "unknown";
        }
    }

    std::string MakeError(const char* operation, NGFX_Result result)
    {
        return std::string(operation) + " failed: " + ResultName(result) +
            " (" + std::to_string(static_cast<int>(result)) + ")";
    }
}

bool NsightGPUTrace::Initialize(const std::filesystem::path& installationPath, std::string& error)
{
    if (_initialized)
        return true;

    if (installationPath.empty())
    {
        error = "Nsight Graphics installation path is empty";
        return false;
    }

    NGFX_GPUTrace_InjectionSettings settings = {};
    NGFX_Result result = NGFX_GPUTrace_InjectionSettings_SetDefaults(&settings);
    if (result != NGFX_Result_Success)
    {
        error = MakeError("GPU Trace settings initialization", result);
        return false;
    }

    settings.startEvent = NGFX_GPUTrace_StartEvent_NGFXSDK;
    settings.stopEvent = NGFX_GPUTrace_StopEvent_NGFXSDK;
    settings.maxDurationMs = 10000;
    settings.collectScreenshot = true;
    settings.traceShaderBindings = true;
    settings.collectShaderInfo = true;
    settings.collectExternalShaderDebugInfo = true;
    settings.enableNVTXranges = true;
    settings.hudPosition = NGFX_GPUTrace_HUDPosition_TopRight;

    NGFX_SetLibraryLoadFn(NGFX_LoadLib_NoVerification);

    NGFX_GPUTrace_Inject_Vulkan_Params injectParams = {
        NGFX_GPUTrace_Inject_Vulkan_Params_VER
    };
    injectParams.installationPath = installationPath.c_str();
    injectParams.settings = &settings;
    result = NGFX_GPUTrace_Inject_Vulkan(&injectParams);
    if (result != NGFX_Result_Success)
    {
        error = MakeError("GPU Trace injection", result);
        return false;
    }

    NGFX_GPUTrace_InitializeActivity_Vulkan_Params initializeParams = {
        NGFX_GPUTrace_InitializeActivity_Vulkan_Params_VER
    };
    result = NGFX_GPUTrace_InitializeActivity_Vulkan(&initializeParams);
    if (result != NGFX_Result_Success)
    {
        error = MakeError("GPU Trace activity initialization", result);
        return false;
    }

    _initialized = true;
    NC_LOG_INFO("NOVUS_NSIGHT_GPU_TRACE event=initialized path={}", installationPath.string());
    return true;
}

bool NsightGPUTrace::IsReady() const
{
    return GetState() == State::Ready;
}

NsightGPUTrace::State NsightGPUTrace::GetState() const
{
    if (!_initialized)
        return State::Unavailable;

    NGFX_GPUTrace_GetStatus_Params params = {
        NGFX_GPUTrace_GetStatus_Params_VER
    };
    if (NGFX_GPUTrace_GetStatus(&params) != NGFX_Result_Success)
        return State::Errored;

    switch (params.status)
    {
    case NGFX_GPUTrace_Status_Inactive: return State::Inactive;
    case NGFX_GPUTrace_Status_Active: return State::Ready;
    case NGFX_GPUTrace_Status_Tracing: return State::Capturing;
    case NGFX_GPUTrace_Status_Draining: return State::Draining;
    case NGFX_GPUTrace_Status_Errored: return State::Errored;
    default: return State::Errored;
    }
}

bool NsightGPUTrace::Start(std::string& error)
{
    const State state = GetState();
    if (state != State::Ready)
    {
        error = std::string("GPU Trace is not ready; current state is ") + StateName(state);
        return false;
    }

    NGFX_GPUTrace_StartTrace_Vulkan_Params params = {
        NGFX_GPUTrace_StartTrace_Vulkan_Params_VER
    };
    const NGFX_Result result = NGFX_GPUTrace_StartTrace_Vulkan(&params);
    if (result != NGFX_Result_Success)
    {
        error = MakeError("GPU Trace start", result);
        return false;
    }

    NC_LOG_INFO("NOVUS_NSIGHT_GPU_TRACE event=started");
    return true;
}

bool NsightGPUTrace::Stop(std::string& error)
{
    const State state = GetState();
    if (state != State::Capturing)
    {
        error = std::string("GPU Trace is not capturing; current state is ") + StateName(state);
        return false;
    }

    NGFX_GPUTrace_StopTrace_Vulkan_Params params = {
        NGFX_GPUTrace_StopTrace_Vulkan_Params_VER
    };
    params.flags = NGFX_GPUTrace_StopTraceFlag_NextFrameBoundary;
    params.queue = {};
    params.outputResources = nullptr;
    params.numOutputResources = 0;
    const NGFX_Result result = NGFX_GPUTrace_StopTrace_Vulkan(&params);
    if (result != NGFX_Result_Success)
    {
        error = MakeError("GPU Trace stop", result);
        return false;
    }

    NC_LOG_INFO("NOVUS_NSIGHT_GPU_TRACE event=stop-requested");
    return true;
}

const char* NsightGPUTrace::StateName(State state)
{
    switch (state)
    {
    case State::Unavailable: return "unavailable";
    case State::Inactive: return "inactive";
    case State::Ready: return "ready";
    case State::Capturing: return "capturing";
    case State::Draining: return "draining";
    case State::Errored: return "errored";
    default: return "unknown";
    }
}

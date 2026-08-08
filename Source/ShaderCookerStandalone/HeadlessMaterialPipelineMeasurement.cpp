#include "HeadlessMaterialPipelineMeasurement.h"

#include <Base/Memory/Bytebuffer.h>
#include <FileFormat/Novus/ShaderPack/ShaderPack.h>

#include <vulkan/vulkan.h>

#include <algorithm>
#include <array>
#include <fstream>
#include <limits>
#include <set>
#include <sstream>
#include <vector>

namespace
{
    using namespace MaterialCooking;

    MaterialPipelineMeasurementReport Unavailable(std::string reason)
    {
        MaterialPipelineMeasurementReport report;
        report.status = MaterialMeasurementStatus::Unavailable;
        report.unavailableReason = std::move(reason);
        return report;
    }

    bool HasDeviceExtension(VkPhysicalDevice device, const char* requested)
    {
        u32 count = 0;
        if (vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr) != VK_SUCCESS)
            return false;
        std::vector<VkExtensionProperties> extensions(count);
        if (vkEnumerateDeviceExtensionProperties(
                device, nullptr, &count, extensions.data()) != VK_SUCCESS)
            return false;
        return std::any_of(extensions.begin(), extensions.end(),
                           [requested](const VkExtensionProperties& extension) {
                               return std::string_view(extension.extensionName) == requested;
                           });
    }

    std::shared_ptr<Bytebuffer> LoadShaderPackBytes(const std::filesystem::path& path)
    {
        std::ifstream input(path, std::ios::binary | std::ios::ate);
        if (!input)
            return nullptr;
        const std::streamsize size = input.tellg();
        if (size <= 0 || static_cast<u64>(size) > std::numeric_limits<u32>::max())
            return nullptr;
        input.seekg(0, std::ios::beg);
        std::vector<u8> bytes(static_cast<size_t>(size));
        if (!input.read(reinterpret_cast<char*>(bytes.data()), size))
            return nullptr;
        std::shared_ptr<Bytebuffer> buffer = Bytebuffer::BorrowRuntime(bytes.size());
        if (!buffer->PutBytes(bytes.data(), bytes.size()))
            return nullptr;
        buffer->readData = 0;
        return buffer;
    }

    f64 StatisticValue(const VkPipelineExecutableStatisticKHR& statistic)
    {
        switch (statistic.format)
        {
        case VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_BOOL32_KHR:
            return statistic.value.b32 != VK_FALSE ? 1.0 : 0.0;
        case VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_INT64_KHR:
            return static_cast<f64>(statistic.value.i64);
        case VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_UINT64_KHR:
            return static_cast<f64>(statistic.value.u64);
        case VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_FLOAT64_KHR:
            return statistic.value.f64;
        default: return 0.0;
        }
    }

    const char* StatisticFormat(VkPipelineExecutableStatisticFormatKHR format)
    {
        switch (format)
        {
        case VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_BOOL32_KHR: return "bool32";
        case VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_INT64_KHR: return "int64";
        case VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_UINT64_KHR: return "uint64";
        case VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_FLOAT64_KHR: return "float64";
        default: return "unknown";
        }
    }

    std::string DriverIdentity(const VkPhysicalDeviceProperties& properties)
    {
        std::ostringstream identity;
        identity << "vendor=" << properties.vendorID << ",device=" << properties.deviceID
                 << ",driver=" << properties.driverVersion << ",api="
                 << VK_API_VERSION_MAJOR(properties.apiVersion) << '.'
                 << VK_API_VERSION_MINOR(properties.apiVersion) << '.'
                 << VK_API_VERSION_PATCH(properties.apiVersion);
        return identity.str();
    }
}

HeadlessMaterialPipelineMeasurement::HeadlessMaterialPipelineMeasurement(
    std::filesystem::path shaderPackPath)
    : _shaderPackPath(std::move(shaderPackPath))
{
}

MaterialCooking::MaterialPipelineMeasurementReport
HeadlessMaterialPipelineMeasurement::Measure(
    std::span<const MaterialCooking::CookedMaterialProgram> programs)
{
    using namespace MaterialCooking;

    std::shared_ptr<Bytebuffer> shaderBytes = LoadShaderPackBytes(_shaderPackPath);
    FileFormat::ShaderPack shaderPack;
    if (!shaderBytes || !FileFormat::ShaderPack::Read(shaderBytes, shaderPack))
        return Unavailable("failed to read generated Material group shader pack");

    std::set<u16> groupSet;
    for (const CookedMaterialProgram& program : programs)
        groupSet.emplace(program.program.executionGroupID);
    const std::vector<u16> groups(groupSet.begin(), groupSet.end());
    if (groups.size() != shaderPack.GetNumShaders())
        return Unavailable("generated Material shader permutation count does not match execution groups");

    VkApplicationInfo application{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    application.pApplicationName = "Novus Material Cooker";
    application.apiVersion = VK_API_VERSION_1_2;
    VkInstanceCreateInfo instanceInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    instanceInfo.pApplicationInfo = &application;

    VkInstance instance = VK_NULL_HANDLE;
    if (vkCreateInstance(&instanceInfo, nullptr, &instance) != VK_SUCCESS)
        return Unavailable("failed to create a headless Vulkan instance");

    MaterialPipelineMeasurementReport report;
    report.status = MaterialMeasurementStatus::Unavailable;

    u32 physicalDeviceCount = 0;
    VkResult result = vkEnumeratePhysicalDevices(instance, &physicalDeviceCount, nullptr);
    if (result != VK_SUCCESS || physicalDeviceCount == 0)
    {
        vkDestroyInstance(instance, nullptr);
        return Unavailable("no Vulkan physical device is available");
    }
    std::vector<VkPhysicalDevice> physicalDevices(physicalDeviceCount);
    vkEnumeratePhysicalDevices(instance, &physicalDeviceCount, physicalDevices.data());

    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    u32 queueFamilyIndex = 0;
    VkPhysicalDeviceProperties physicalProperties{};
    for (VkPhysicalDevice candidate : physicalDevices)
    {
        if (!HasDeviceExtension(candidate,
                                VK_KHR_PIPELINE_EXECUTABLE_PROPERTIES_EXTENSION_NAME))
            continue;
        u32 queueCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &queueCount, nullptr);
        std::vector<VkQueueFamilyProperties> queues(queueCount);
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &queueCount, queues.data());
        const auto queue = std::find_if(
            queues.begin(), queues.end(), [](const VkQueueFamilyProperties& properties) {
                return (properties.queueFlags & VK_QUEUE_COMPUTE_BIT) != 0;
            });
        if (queue == queues.end())
            continue;
        physicalDevice = candidate;
        queueFamilyIndex = static_cast<u32>(std::distance(queues.begin(), queue));
        vkGetPhysicalDeviceProperties(candidate, &physicalProperties);
        if (physicalProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
            break;
    }
    if (physicalDevice == VK_NULL_HANDLE)
    {
        vkDestroyInstance(instance, nullptr);
        return Unavailable("VK_KHR_pipeline_executable_properties is unavailable");
    }

    VkPhysicalDeviceDescriptorIndexingFeatures descriptorFeatures{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES};
    VkPhysicalDeviceShaderFloat16Int8Features float16Features{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES};
    VkPhysicalDevicePipelineExecutablePropertiesFeaturesKHR executableFeatures{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PIPELINE_EXECUTABLE_PROPERTIES_FEATURES_KHR};
    descriptorFeatures.pNext = &float16Features;
    float16Features.pNext = &executableFeatures;
    VkPhysicalDeviceFeatures2 availableFeatures{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    availableFeatures.pNext = &descriptorFeatures;
    vkGetPhysicalDeviceFeatures2(physicalDevice, &availableFeatures);
    if (!descriptorFeatures.runtimeDescriptorArray ||
        !descriptorFeatures.shaderSampledImageArrayNonUniformIndexing ||
        !availableFeatures.features.shaderInt16 || !float16Features.shaderFloat16 ||
        !executableFeatures.pipelineExecutableInfo)
    {
        vkDestroyInstance(instance, nullptr);
        return Unavailable("required descriptor-indexing or pipeline-statistics features are unavailable");
    }

    const float queuePriority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queueInfo.queueFamilyIndex = queueFamilyIndex;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &queuePriority;

    descriptorFeatures.pNext = &float16Features;
    float16Features.pNext = &executableFeatures;
    float16Features.shaderFloat16 = VK_TRUE;
    executableFeatures.pipelineExecutableInfo = VK_TRUE;
    VkPhysicalDeviceFeatures enabledFeatures{};
    enabledFeatures.shaderInt16 = VK_TRUE;
    const char* extensions[] = {VK_KHR_PIPELINE_EXECUTABLE_PROPERTIES_EXTENSION_NAME};
    VkDeviceCreateInfo deviceInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    deviceInfo.pNext = &descriptorFeatures;
    deviceInfo.queueCreateInfoCount = 1;
    deviceInfo.pQueueCreateInfos = &queueInfo;
    deviceInfo.enabledExtensionCount = 1;
    deviceInfo.ppEnabledExtensionNames = extensions;
    deviceInfo.pEnabledFeatures = &enabledFeatures;

    VkDevice device = VK_NULL_HANDLE;
    if (vkCreateDevice(physicalDevice, &deviceInfo, nullptr, &device) != VK_SUCCESS)
    {
        vkDestroyInstance(instance, nullptr);
        return Unavailable("failed to create the headless Vulkan measurement device");
    }

    auto getExecutableProperties = reinterpret_cast<PFN_vkGetPipelineExecutablePropertiesKHR>(
        vkGetDeviceProcAddr(device, "vkGetPipelineExecutablePropertiesKHR"));
    auto getExecutableStatistics = reinterpret_cast<PFN_vkGetPipelineExecutableStatisticsKHR>(
        vkGetDeviceProcAddr(device, "vkGetPipelineExecutableStatisticsKHR"));
    if (!getExecutableProperties || !getExecutableStatistics)
    {
        vkDestroyDevice(device, nullptr);
        vkDestroyInstance(instance, nullptr);
        return Unavailable("pipeline executable query functions are unavailable");
    }

    VkDescriptorSetLayout emptySet = VK_NULL_HANDLE;
    VkDescriptorSetLayoutCreateInfo emptyInfo{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    if (vkCreateDescriptorSetLayout(device, &emptyInfo, nullptr, &emptySet) != VK_SUCCESS)
    {
        vkDestroyDevice(device, nullptr);
        vkDestroyInstance(instance, nullptr);
        return Unavailable("failed to create an empty measurement descriptor layout");
    }

    std::array<VkDescriptorSetLayoutBinding, 14> materialBindings{};
    for (u32 binding = 0; binding < materialBindings.size(); ++binding)
    {
        materialBindings[binding].binding = binding;
        materialBindings[binding].descriptorCount = 1;
        materialBindings[binding].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        materialBindings[binding].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }
    for (u32 binding = 3; binding <= 6; ++binding)
        materialBindings[binding].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
    materialBindings[13].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    materialBindings[13].descriptorCount =
        std::max(1u, std::min(16384u, physicalProperties.limits.maxDescriptorSetSampledImages));
    VkDescriptorBindingFlags textureBindingFlags =
        VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT |
        VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT;
    std::array<VkDescriptorBindingFlags, 14> materialBindingFlags{};
    materialBindingFlags[13] = textureBindingFlags;
    VkDescriptorSetLayoutBindingFlagsCreateInfo bindingFlags{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO};
    bindingFlags.bindingCount = static_cast<u32>(materialBindingFlags.size());
    bindingFlags.pBindingFlags = materialBindingFlags.data();
    VkDescriptorSetLayoutCreateInfo materialSetInfo{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    materialSetInfo.pNext = &bindingFlags;
    materialSetInfo.bindingCount = static_cast<u32>(materialBindings.size());
    materialSetInfo.pBindings = materialBindings.data();

    VkDescriptorSetLayout materialSet = VK_NULL_HANDLE;
    if (vkCreateDescriptorSetLayout(device, &materialSetInfo, nullptr, &materialSet) != VK_SUCCESS)
    {
        vkDestroyDescriptorSetLayout(device, emptySet, nullptr);
        vkDestroyDevice(device, nullptr);
        vkDestroyInstance(instance, nullptr);
        return Unavailable("failed to create the Material measurement descriptor layout");
    }

    VkDescriptorSetLayoutBinding outputBinding{};
    outputBinding.binding = 0;
    outputBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    outputBinding.descriptorCount = 1;
    outputBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    VkDescriptorSetLayoutCreateInfo outputSetInfo{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    outputSetInfo.bindingCount = 1;
    outputSetInfo.pBindings = &outputBinding;
    VkDescriptorSetLayout outputSet = VK_NULL_HANDLE;
    if (vkCreateDescriptorSetLayout(device, &outputSetInfo, nullptr, &outputSet) != VK_SUCCESS)
    {
        vkDestroyDescriptorSetLayout(device, materialSet, nullptr);
        vkDestroyDescriptorSetLayout(device, emptySet, nullptr);
        vkDestroyDevice(device, nullptr);
        vkDestroyInstance(instance, nullptr);
        return Unavailable("failed to create the measurement output descriptor layout");
    }

    std::array<VkDescriptorSetLayout, 8> layouts{};
    layouts.fill(emptySet);
    layouts[6] = materialSet;
    layouts[7] = outputSet;
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{
        VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pipelineLayoutInfo.setLayoutCount = static_cast<u32>(layouts.size());
    pipelineLayoutInfo.pSetLayouts = layouts.data();
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS)
    {
        vkDestroyDescriptorSetLayout(device, outputSet, nullptr);
        vkDestroyDescriptorSetLayout(device, materialSet, nullptr);
        vkDestroyDescriptorSetLayout(device, emptySet, nullptr);
        vkDestroyDevice(device, nullptr);
        vkDestroyInstance(instance, nullptr);
        return Unavailable("failed to create the Material measurement pipeline layout");
    }

    report.status = MaterialMeasurementStatus::Available;
    report.deviceName = physicalProperties.deviceName;
    report.driverIdentity = DriverIdentity(physicalProperties);

    for (u32 shaderIndex = 0; shaderIndex < shaderPack.GetNumShaders(); ++shaderIndex)
    {
        FileFormat::ShaderRef* shaderRef = shaderPack.GetShaderRef(shaderBytes, shaderIndex);
        if (!shaderRef || shaderRef->dataSize == 0 || (shaderRef->dataSize & 3u) != 0 ||
            shaderRef->dataOffset > std::numeric_limits<u32>::max())
        {
            report = Unavailable("generated Material shader data is malformed");
            break;
        }

        VkShaderModuleCreateInfo moduleInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        moduleInfo.codeSize = shaderRef->dataSize;
        moduleInfo.pCode = reinterpret_cast<const u32*>(
            shaderPack.GetShaderDataPtr(
                shaderBytes, static_cast<u32>(shaderRef->dataOffset)));
        VkShaderModule shaderModule = VK_NULL_HANDLE;
        if (!moduleInfo.pCode ||
            vkCreateShaderModule(device, &moduleInfo, nullptr, &shaderModule) != VK_SUCCESS)
        {
            report = Unavailable("failed to create a generated Material shader module");
            break;
        }

        VkComputePipelineCreateInfo pipelineInfo{
            VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
        pipelineInfo.flags = VK_PIPELINE_CREATE_CAPTURE_STATISTICS_BIT_KHR;
        pipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        pipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        pipelineInfo.stage.module = shaderModule;
        pipelineInfo.stage.pName = "main";
        pipelineInfo.layout = pipelineLayout;
        VkPipeline pipeline = VK_NULL_HANDLE;
        result = vkCreateComputePipelines(
            device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline);
        vkDestroyShaderModule(device, shaderModule, nullptr);
        if (result != VK_SUCCESS)
        {
            report = Unavailable("driver failed to create a Material measurement pipeline");
            break;
        }

        VkPipelineInfoKHR executableInfo{VK_STRUCTURE_TYPE_PIPELINE_INFO_KHR};
        executableInfo.pipeline = pipeline;
        u32 executableCount = 0;
        result = getExecutableProperties(device, &executableInfo, &executableCount, nullptr);
        std::vector<VkPipelineExecutablePropertiesKHR> executableProperties(
            executableCount, {VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_PROPERTIES_KHR});
        if (result == VK_SUCCESS)
            result = getExecutableProperties(
                device, &executableInfo, &executableCount, executableProperties.data());

        for (u32 executableIndex = 0;
             result == VK_SUCCESS && executableIndex < executableCount;
             ++executableIndex)
        {
            VkPipelineExecutableInfoKHR statisticInfo{
                VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_INFO_KHR};
            statisticInfo.pipeline = pipeline;
            statisticInfo.executableIndex = executableIndex;
            u32 statisticCount = 0;
            result = getExecutableStatistics(
                device, &statisticInfo, &statisticCount, nullptr);
            std::vector<VkPipelineExecutableStatisticKHR> statistics(
                statisticCount, {VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_STATISTIC_KHR});
            if (result == VK_SUCCESS)
                result = getExecutableStatistics(
                    device, &statisticInfo, &statisticCount, statistics.data());
            for (const VkPipelineExecutableStatisticKHR& statistic : statistics)
            {
                MaterialPipelineStatistic& output = report.statistics.emplace_back();
                output.canonicalKey = "execution-group";
                output.name = statistic.name;
                output.executionGroupID = groups[shaderIndex];
                output.groupLocalProgramID = std::numeric_limits<u16>::max();
                output.value = StatisticValue(statistic);
                output.description = statistic.description;
                output.format = StatisticFormat(statistic.format);
            }
        }
        vkDestroyPipeline(device, pipeline, nullptr);
        if (result != VK_SUCCESS)
        {
            report = Unavailable("driver failed to query Material pipeline statistics");
            break;
        }
    }

    vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
    vkDestroyDescriptorSetLayout(device, outputSet, nullptr);
    vkDestroyDescriptorSetLayout(device, materialSet, nullptr);
    vkDestroyDescriptorSetLayout(device, emptySet, nullptr);
    vkDestroyDevice(device, nullptr);
    vkDestroyInstance(instance, nullptr);
    return report;
}

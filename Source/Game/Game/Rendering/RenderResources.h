#pragma once
#include <Renderer/RenderSettings.h>
#include <Renderer/Buffer.h>
#include <Renderer/FrameResource.h>
#include <Renderer/DescriptorSet.h>
#include <Renderer/Descriptors/ImageDesc.h>
#include <Renderer/Descriptors/SemaphoreDesc.h>

struct RenderResources
{
public:
    // Permanent resources
    Renderer::ImageID finalColor;

    Renderer::SemaphoreID sceneRenderedSemaphore; // This semaphore tells the present function when the scene is ready to be blitted and presented
    FrameResource<Renderer::SemaphoreID, 2> frameSyncSemaphores; // This semaphore makes sure the GPU handles frames in order
};
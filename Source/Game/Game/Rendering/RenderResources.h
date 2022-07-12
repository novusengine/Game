#pragma once
#include "Game/Rendering/Camera.h"

#include <Renderer/RenderSettings.h>
#include <Renderer/Buffer.h>
#include <Renderer/FrameResource.h>
#include <Renderer/DescriptorSet.h>
#include <Renderer/Descriptors/ImageDesc.h>
#include <Renderer/Descriptors/SemaphoreDesc.h>
#include <Renderer/GPUVector.h>

struct RenderResources
{
public:
    // Permanent resources
    CameraComponent cameraComponent; // Name is stupid until we ECS

    Renderer::GPUVector<Camera> cameras;

    Renderer::ImageID finalColor;
    Renderer::DepthImageID depth;

    Renderer::DescriptorSet globalDescriptorSet;

    Renderer::SemaphoreID sceneRenderedSemaphore; // This semaphore tells the present function when the scene is ready to be blitted and presented
    FrameResource<Renderer::SemaphoreID, 2> frameSyncSemaphores; // This semaphore makes sure the GPU handles frames in order
};
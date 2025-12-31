#pragma once
#include "Game-Lib/Rendering/Camera.h"

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
    RenderResources() 
        : debugDescriptorSet(Renderer::DescriptorSetSlot::DEBUG)
        , globalDescriptorSet(Renderer::DescriptorSetSlot::GLOBAL)
        , lightDescriptorSet(Renderer::DescriptorSetSlot::LIGHT)
        , terrainDescriptorSet(Renderer::DescriptorSetSlot::TERRAIN)
        , modelDescriptorSet(Renderer::DescriptorSetSlot::MODEL)
    {

    }

    // Descriptor sets
    Renderer::DescriptorSet debugDescriptorSet;
    Renderer::DescriptorSet globalDescriptorSet;
    Renderer::DescriptorSet lightDescriptorSet;
    Renderer::DescriptorSet terrainDescriptorSet;
    Renderer::DescriptorSet modelDescriptorSet;

    // Permanent resources
    Renderer::GPUVector<Camera> cameras;

    Renderer::ImageID visibilityBuffer;
    Renderer::ImageID skyboxColor;
    Renderer::ImageID packedNormals;

    Renderer::ImageID sceneColor;
    Renderer::ImageID finalColor;

    Renderer::ImageID debugColor;

    Renderer::ImageID ssaoTarget;

    Renderer::ImageID transparency;
    Renderer::ImageID transparencyWeights;

    Renderer::ImageID depthPyramid;
    Renderer::DepthImageID depth;
    Renderer::ImageID depthColorCopy;
    Renderer::DepthImageID skyboxDepth;

    std::vector<Renderer::DepthImageID> shadowDepthCascades;

    Renderer::DepthImageID debugRendererDepth;

    Renderer::SemaphoreID sceneRenderedSemaphore; // This semaphore tells the present function when the scene is ready to be blitted and presented
    FrameResource<Renderer::SemaphoreID, 2> frameSyncSemaphores; // This semaphore makes sure the GPU handles frames in order
};
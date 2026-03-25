#include "util.h"

#define VOLK_IMPLEMENTATION
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <glm/glm.hpp>
#include <volk.h>
#include <vulkan/vulkan.h>

#define ASSERT(line)                                                                               \
    do                                                                                             \
    {                                                                                              \
        bool _r = (line);                                                                          \
        if (!_r)                                                                                   \
            LOG_FATAL("ASSERT FAIL: %s", #line);                                                   \
    } while (0)

#define VK_ASSERT(line)                                                                            \
    do                                                                                             \
    {                                                                                              \
        VkResult _r = (line);                                                                      \
        if (_r != VK_SUCCESS)                                                                      \
            LOG_FATAL("VK_ASSERT FAIL: %s|res=%u", #line, _r);                                     \
    } while (0)

static constexpr U32 maxFramesInFlight = 2;

static U32* ReadShader(const char* path, U64& outSize)
{
    FILE* f = fopen(path, "rb");
    if (!f)
        LOG_FATAL("Failed to open %s", path);
    fseek(f, 0, SEEK_END);
    outSize = ftell(f);
    fseek(f, 0, SEEK_SET);
    U32* buf = new U32[Cdiv(outSize, 4)];
    ASSERT(buf != nullptr);
    fread(buf, 1, outSize, f);
    fclose(f);
    return buf;
}

int main()
{
    ASSERT(SDL_Init(SDL_INIT_VIDEO));
    ASSERT(SDL_Vulkan_LoadLibrary(nullptr));
    volkInitialize();

    VkApplicationInfo appInfo{.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
                              .pApplicationName = "Triangle",
                              .apiVersion = VK_API_VERSION_1_3};
    U32 instanceExtensionsCount;
    char const* const* instanceExtensions{
        SDL_Vulkan_GetInstanceExtensions(&instanceExtensionsCount)};
    VkInstanceCreateInfo instanceCI{
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &appInfo,
        .enabledExtensionCount = instanceExtensionsCount,
        .ppEnabledExtensionNames = instanceExtensions,
    };
    VkInstance instance;
    VK_ASSERT(vkCreateInstance(&instanceCI, nullptr, &instance));
    volkLoadInstance(instance);

    U32 physicalDeviceCount;
    VkPhysicalDevice physicalDevices[4];
    VK_ASSERT(vkEnumeratePhysicalDevices(instance, &physicalDeviceCount, nullptr));
    ASSERT(physicalDeviceCount <= ARRAY_COUNT(physicalDevices));
    VK_ASSERT(vkEnumeratePhysicalDevices(instance, &physicalDeviceCount, physicalDevices));
    ASSERT(physicalDeviceCount > 0);
    VkPhysicalDevice physicalDevice = physicalDevices[0];

    VkPhysicalDeviceProperties2 deviceProperties{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
    vkGetPhysicalDeviceProperties2(physicalDevice, &deviceProperties);
    LOG_INFO("Device: %s", deviceProperties.properties.deviceName);

    U32 queueFamilyCount;
    VkQueueFamilyProperties queueFamilies[8];
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, nullptr);
    ASSERT(queueFamilyCount <= ARRAY_COUNT(queueFamilies));
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, queueFamilies);
    U32 queueFamily = U32(-1);
    for (U32 i = 0; i < queueFamilyCount; i++)
    {
        if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
        {
            queueFamily = i;
            break;
        }
    }
    ASSERT(queueFamily != U32(-1));
    ASSERT(SDL_Vulkan_GetPresentationSupport(instance, physicalDevice, queueFamily));

    const float qfPrios = 1.0f;
    VkDeviceQueueCreateInfo queueCI{.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                                    .queueFamilyIndex = queueFamily,
                                    .queueCount = 1,
                                    .pQueuePriorities = &qfPrios};
    VkPhysicalDeviceVulkan12Features enabledVk12Features{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
        .descriptorIndexing = true,
        .shaderSampledImageArrayNonUniformIndexing = true,
        .descriptorBindingVariableDescriptorCount = true,
        .runtimeDescriptorArray = true,
        .bufferDeviceAddress = true};
    VkPhysicalDeviceVulkan13Features enabledVk13Features{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
        .pNext = &enabledVk12Features,
        .synchronization2 = true,
        .dynamicRendering = true};
    const char* deviceExtensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    VkDeviceCreateInfo deviceCI{.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
                                .pNext = &enabledVk13Features,
                                .queueCreateInfoCount = 1,
                                .pQueueCreateInfos = &queueCI,
                                .enabledExtensionCount = ARRAY_COUNT(deviceExtensions),
                                .ppEnabledExtensionNames = deviceExtensions};
    VkDevice device;
    VK_ASSERT(vkCreateDevice(physicalDevice, &deviceCI, nullptr, &device));
    VkQueue queue;
    vkGetDeviceQueue(device, queueFamily, 0, &queue);

    // Window
    SDL_Window* window =
        SDL_CreateWindow("Triangle", 1280u, 720u, SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
    ASSERT(window != nullptr);
    VkSurfaceKHR surface;
    ASSERT(SDL_Vulkan_CreateSurface(window, instance, nullptr, &surface));
    glm::ivec2 windowSize;
    ASSERT(SDL_GetWindowSize(window, &windowSize.x, &windowSize.y));

    VkSurfaceCapabilitiesKHR surfaceCaps{};
    VK_ASSERT(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &surfaceCaps));
    VkExtent2D swapchainExtent{surfaceCaps.currentExtent};
    if (surfaceCaps.currentExtent.width == 0xFFFFFFFF)
        swapchainExtent = {.width = (U32)windowSize.x, .height = (U32)windowSize.y};

    // Swapchain
    constexpr VkFormat kImageFormat = VK_FORMAT_B8G8R8A8_SRGB;
    VkSwapchainCreateInfoKHR swapchainCI{.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
                                         .surface = surface,
                                         .minImageCount = surfaceCaps.minImageCount,
                                         .imageFormat = kImageFormat,
                                         .imageColorSpace = VK_COLORSPACE_SRGB_NONLINEAR_KHR,
                                         .imageExtent = swapchainExtent,
                                         .imageArrayLayers = 1,
                                         .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                                         .preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
                                         .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
                                         .presentMode = VK_PRESENT_MODE_FIFO_KHR};
    VkSwapchainKHR swapchain;
    VK_ASSERT(vkCreateSwapchainKHR(device, &swapchainCI, nullptr, &swapchain));

    U32 imageCount;
    VkImage swapchainImages[2];
    VkImageView swapchainImageViews[2];
    VK_ASSERT(vkGetSwapchainImagesKHR(device, swapchain, &imageCount, nullptr));
    ASSERT(imageCount <= ARRAY_COUNT(swapchainImages));
    VK_ASSERT(vkGetSwapchainImagesKHR(device, swapchain, &imageCount, swapchainImages));
    for (U32 i = 0; i < imageCount; i++)
    {
        VkImageViewCreateInfo viewCI{.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                                     .image = swapchainImages[i],
                                     .viewType = VK_IMAGE_VIEW_TYPE_2D,
                                     .format = kImageFormat,
                                     .subresourceRange{.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                                       .levelCount = 1,
                                                       .layerCount = 1}};
        VK_ASSERT(vkCreateImageView(device, &viewCI, nullptr, &swapchainImageViews[i]));
    }

    // Command pool + buffers
    VkCommandPoolCreateInfo commandPoolCI{.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
                                          .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
                                          .queueFamilyIndex = queueFamily};
    VkCommandPool commandPool;
    VK_ASSERT(vkCreateCommandPool(device, &commandPoolCI, nullptr, &commandPool));

    VkCommandBuffer commandBuffers[maxFramesInFlight];
    VkCommandBufferAllocateInfo cbAllocCI{.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
                                          .commandPool = commandPool,
                                          .commandBufferCount = maxFramesInFlight};
    VK_ASSERT(vkAllocateCommandBuffers(device, &cbAllocCI, commandBuffers));

    // Sync objects
    VkFence fences[maxFramesInFlight];
    VkSemaphore presentSemaphores[maxFramesInFlight];
    VkSemaphore renderSemaphores[4]; // one per swapchain image
    VkFenceCreateInfo fenceCI{.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
                              .flags = VK_FENCE_CREATE_SIGNALED_BIT};
    VkSemaphoreCreateInfo semaphoreCI{.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    for (U32 i = 0; i < maxFramesInFlight; i++)
    {
        VK_ASSERT(vkCreateFence(device, &fenceCI, nullptr, &fences[i]));
        VK_ASSERT(vkCreateSemaphore(device, &semaphoreCI, nullptr, &presentSemaphores[i]));
    }
    for (U32 i = 0; i < imageCount; i++)
        VK_ASSERT(vkCreateSemaphore(device, &semaphoreCI, nullptr, &renderSemaphores[i]));

    // Pipeline
    U64 vertSize;
    U64 fragSize;
    U32* vertSpirv = ReadShader("shaders/tri.vert.spv", vertSize);
    U32* fragSpirv = ReadShader("shaders/tri.frag.spv", fragSize);
    VkShaderModule vertModule, fragModule;
    VkShaderModuleCreateInfo vertModuleCI{.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
                                          .codeSize = vertSize,
                                          .pCode = vertSpirv};
    VkShaderModuleCreateInfo fragModuleCI{.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
                                          .codeSize = fragSize,
                                          .pCode = fragSpirv};
    VK_ASSERT(vkCreateShaderModule(device, &vertModuleCI, nullptr, &vertModule));
    VK_ASSERT(vkCreateShaderModule(device, &fragModuleCI, nullptr, &fragModule));
    delete[] vertSpirv;
    delete[] fragSpirv;

    VkPipelineLayoutCreateInfo pipelineLayoutCI{.sType =
                                                    VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    VkPipelineLayout pipelineLayout;
    VK_ASSERT(vkCreatePipelineLayout(device, &pipelineLayoutCI, nullptr, &pipelineLayout));

    VkPipelineShaderStageCreateInfo shaderStages[2]{
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .stage = VK_SHADER_STAGE_VERTEX_BIT,
         .module = vertModule,
         .pName = "main"},
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
         .module = fragModule,
         .pName = "main"},
    };
    VkPipelineVertexInputStateCreateInfo vertexInputState{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo inputAssemblyState{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
    VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = ARRAY_COUNT(dynamicStates),
        .pDynamicStates = dynamicStates};
    VkPipelineViewportStateCreateInfo viewportState{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount = 1};
    VkPipelineRasterizationStateCreateInfo rasterizationState{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO, .lineWidth = 1.0f};
    VkPipelineMultisampleStateCreateInfo multisampleState{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT};
    VkPipelineColorBlendAttachmentState blendAttachment{.colorWriteMask = 0xF};
    VkPipelineColorBlendStateCreateInfo colorBlendState{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &blendAttachment};
    VkPipelineRenderingCreateInfo renderingCI{.sType =
                                                  VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
                                              .colorAttachmentCount = 1,
                                              .pColorAttachmentFormats = &kImageFormat};
    VkGraphicsPipelineCreateInfo pipelineCI{.sType =
                                                VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
                                            .pNext = &renderingCI,
                                            .stageCount = ARRAY_COUNT(shaderStages),
                                            .pStages = shaderStages,
                                            .pVertexInputState = &vertexInputState,
                                            .pInputAssemblyState = &inputAssemblyState,
                                            .pViewportState = &viewportState,
                                            .pRasterizationState = &rasterizationState,
                                            .pMultisampleState = &multisampleState,
                                            .pColorBlendState = &colorBlendState,
                                            .pDynamicState = &dynamicState,
                                            .layout = pipelineLayout};
    VkPipeline pipeline;
    VK_ASSERT(
        vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineCI, nullptr, &pipeline));

    vkDestroyShaderModule(device, vertModule, nullptr);
    vkDestroyShaderModule(device, fragModule, nullptr);

    // Render loop
    U32 frameIndex = 0;
    U32 imageIndex = 0;
    bool quit = false;
    bool updateSwapchain = false;
    while (!quit)
    {
        VK_ASSERT(vkWaitForFences(device, 1, &fences[frameIndex], VK_TRUE, UINT64_MAX));
        VK_ASSERT(vkResetFences(device, 1, &fences[frameIndex]));

        VkResult acquireResult = vkAcquireNextImageKHR(device,
                                                       swapchain,
                                                       UINT64_MAX,
                                                       presentSemaphores[frameIndex],
                                                       VK_NULL_HANDLE,
                                                       &imageIndex);
        if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR)
            updateSwapchain = true;
        else
            VK_ASSERT(acquireResult);

        VkCommandBuffer cb = commandBuffers[frameIndex];
        VK_ASSERT(vkResetCommandBuffer(cb, 0));
        VkCommandBufferBeginInfo cbBI{.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                                      .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
        VK_ASSERT(vkBeginCommandBuffer(cb, &cbBI));

        VkImageMemoryBarrier2 barrierColor{
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            .srcAccessMask = 0,
            .dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            .dstAccessMask =
                VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
            .image = swapchainImages[imageIndex],
            .subresourceRange{
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1}};
        VkDependencyInfo barrierColorDep{.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                                         .imageMemoryBarrierCount = 1,
                                         .pImageMemoryBarriers = &barrierColor};
        vkCmdPipelineBarrier2(cb, &barrierColorDep);

        VkRenderingAttachmentInfo colorAttachment{.sType =
                                                      VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                                                  .imageView = swapchainImageViews[imageIndex],
                                                  .imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
                                                  .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                                                  .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                                                  .clearValue{.color{0.0f, 0.0f, 0.0f, 1.0f}}};
        VkRenderingInfo renderingInfo{
            .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
            .renderArea{.extent{.width = (U32)windowSize.x, .height = (U32)windowSize.y}},
            .layerCount = 1,
            .colorAttachmentCount = 1,
            .pColorAttachments = &colorAttachment};
        vkCmdBeginRendering(cb, &renderingInfo);

        VkViewport vp{.width = (float)windowSize.x,
                      .height = (float)windowSize.y,
                      .minDepth = 0.0f,
                      .maxDepth = 1.0f};
        VkRect2D scissor{.extent{.width = (U32)windowSize.x, .height = (U32)windowSize.y}};
        vkCmdSetViewport(cb, 0, 1, &vp);
        vkCmdSetScissor(cb, 0, 1, &scissor);
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        vkCmdDraw(cb, 3, 1, 0, 0);

        vkCmdEndRendering(cb);

        VkImageMemoryBarrier2 barrierPresent{
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            .srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            .dstAccessMask = 0,
            .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            .image = swapchainImages[imageIndex],
            .subresourceRange{
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1}};
        VkDependencyInfo barrierPresentDep{.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                                           .imageMemoryBarrierCount = 1,
                                           .pImageMemoryBarriers = &barrierPresent};
        vkCmdPipelineBarrier2(cb, &barrierPresentDep);

        VK_ASSERT(vkEndCommandBuffer(cb));

        VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo submitInfo{.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                                .waitSemaphoreCount = 1,
                                .pWaitSemaphores = &presentSemaphores[frameIndex],
                                .pWaitDstStageMask = &waitStage,
                                .commandBufferCount = 1,
                                .pCommandBuffers = &cb,
                                .signalSemaphoreCount = 1,
                                .pSignalSemaphores = &renderSemaphores[imageIndex]};
        VK_ASSERT(vkQueueSubmit(queue, 1, &submitInfo, fences[frameIndex]));

        frameIndex = (frameIndex + 1) % maxFramesInFlight;

        VkPresentInfoKHR presentInfo{.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
                                     .waitSemaphoreCount = 1,
                                     .pWaitSemaphores = &renderSemaphores[imageIndex],
                                     .swapchainCount = 1,
                                     .pSwapchains = &swapchain,
                                     .pImageIndices = &imageIndex};
        VkResult presentResult = vkQueuePresentKHR(queue, &presentInfo);
        if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR)
            updateSwapchain = true;
        else
            VK_ASSERT(presentResult);

        for (SDL_Event event; SDL_PollEvent(&event);)
        {
            if (event.type == SDL_EVENT_QUIT)
                quit = true;
            if (event.type == SDL_EVENT_WINDOW_RESIZED)
                updateSwapchain = true;
        }

        if (updateSwapchain)
        {
            updateSwapchain = false;
            ASSERT(SDL_GetWindowSize(window, &windowSize.x, &windowSize.y));
            VK_ASSERT(vkDeviceWaitIdle(device));
            VK_ASSERT(
                vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &surfaceCaps));
            swapchainCI.oldSwapchain = swapchain;
            swapchainCI.imageExtent = {.width = (U32)windowSize.x, .height = (U32)windowSize.y};
            VK_ASSERT(vkCreateSwapchainKHR(device, &swapchainCI, nullptr, &swapchain));
            for (U32 i = 0; i < imageCount; i++)
                vkDestroyImageView(device, swapchainImageViews[i], nullptr);
            vkDestroySwapchainKHR(device, swapchainCI.oldSwapchain, nullptr);
            VK_ASSERT(vkGetSwapchainImagesKHR(device, swapchain, &imageCount, nullptr));
            ASSERT(imageCount <= ARRAY_COUNT(swapchainImages));
            VK_ASSERT(vkGetSwapchainImagesKHR(device, swapchain, &imageCount, swapchainImages));
            for (U32 i = 0; i < imageCount; i++)
            {
                VkImageViewCreateInfo viewCI{
                    .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                    .image = swapchainImages[i],
                    .viewType = VK_IMAGE_VIEW_TYPE_2D,
                    .format = kImageFormat,
                    .subresourceRange{
                        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1}};
                VK_ASSERT(vkCreateImageView(device, &viewCI, nullptr, &swapchainImageViews[i]));
            }
        }
    }

    // Teardown
    VK_ASSERT(vkDeviceWaitIdle(device));
    vkDestroyPipeline(device, pipeline, nullptr);
    vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
    for (U32 i = 0; i < maxFramesInFlight; i++)
    {
        vkDestroyFence(device, fences[i], nullptr);
        vkDestroySemaphore(device, presentSemaphores[i], nullptr);
    }
    for (U32 i = 0; i < imageCount; i++)
    {
        vkDestroySemaphore(device, renderSemaphores[i], nullptr);
        vkDestroyImageView(device, swapchainImageViews[i], nullptr);
    }
    vkDestroyCommandPool(device, commandPool, nullptr);
    vkDestroySwapchainKHR(device, swapchain, nullptr);
    vkDestroySurfaceKHR(instance, surface, nullptr);
    vkDestroyDevice(device, nullptr);
    vkDestroyInstance(instance, nullptr);
    SDL_DestroyWindow(window);
    SDL_Quit();
}
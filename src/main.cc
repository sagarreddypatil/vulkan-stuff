#include "shared.h"
#include "util.h"

#define VOLK_IMPLEMENTATION
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <glm/glm.hpp>
#include <math.h>
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

static U32 FindMemoryType(VkPhysicalDevice physDev, U32 typeFilter, VkMemoryPropertyFlags props)
{
    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(physDev, &memProps);
    for (U32 i = 0; i < memProps.memoryTypeCount; i++)
        if ((typeFilter & (1 << i)) && (memProps.memoryTypes[i].propertyFlags & props) == props)
            return i;
    LOG_FATAL("Failed to find suitable memory type");
    return 0;
}

static void Fnv1a(U32& h, const U8* data, U32 len)
{
    for (U32 i = 0; i < len; i++)
    {
        h ^= data[i];
        h *= 0x01000193;
    }
}

static float HashF(U32 index, U32 channel)
{
    U32 h = 0x811c9dc5;
    for (U32 i = 0; i < 10; ++i)
    {
        Fnv1a(h, reinterpret_cast<U8*>(&channel), sizeof(channel));
        Fnv1a(h, reinterpret_cast<U8*>(&index), sizeof(index));
    }
    return float(h & ((1 << 24) - 1)) / float((1 << 24) - 1);
}

static void CreateImage(VkDevice device,
                        VkPhysicalDevice physDev,
                        U32 w,
                        U32 h,
                        VkFormat fmt,
                        VkSampleCountFlagBits samples,
                        VkImageUsageFlags usage,
                        VkImageAspectFlags aspect,
                        VkImage& img,
                        VkDeviceMemory& mem,
                        VkImageView& view)
{
    VkImageCreateInfo ci{.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
                         .imageType = VK_IMAGE_TYPE_2D,
                         .format = fmt,
                         .extent = {w, h, 1},
                         .mipLevels = 1,
                         .arrayLayers = 1,
                         .samples = samples,
                         .tiling = VK_IMAGE_TILING_OPTIMAL,
                         .usage = usage};
    VK_ASSERT(vkCreateImage(device, &ci, nullptr, &img));
    VkMemoryRequirements reqs;
    vkGetImageMemoryRequirements(device, img, &reqs);
    VkMemoryAllocateInfo ai{.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                            .allocationSize = reqs.size,
                            .memoryTypeIndex = FindMemoryType(
                                physDev, reqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)};
    VK_ASSERT(vkAllocateMemory(device, &ai, nullptr, &mem));
    VK_ASSERT(vkBindImageMemory(device, img, mem, 0));
    VkImageViewCreateInfo vci{
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = img,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = fmt,
        .subresourceRange = {.aspectMask = aspect, .levelCount = 1, .layerCount = 1}};
    VK_ASSERT(vkCreateImageView(device, &vci, nullptr, &view));
}

static void DestroyImage(VkDevice device, VkImage img, VkDeviceMemory mem, VkImageView view)
{
    vkDestroyImageView(device, view, nullptr);
    vkDestroyImage(device, img, nullptr);
    vkFreeMemory(device, mem, nullptr);
}

static VkShaderModule LoadModule(VkDevice device, const char* shaderName, const char* stage)
{
    char path[256];
    int pathLen = snprintf(path, sizeof(path), "shaders/%s.%s.spv", shaderName, stage);
    ASSERT(pathLen > 0 && pathLen < I32(sizeof(path)));
    FILE* f = fopen(path, "rb");
    if (!f)
        LOG_FATAL("Failed to open %s", path);
    ON_SCOPE_EXIT(fclose(f));
    fseek(f, 0, SEEK_END);
    U64 size = ftell(f);
    fseek(f, 0, SEEK_SET);
    ASSERT((size % sizeof(U32)) == 0);
    U32* spirv = new U32[size / sizeof(U32)];
    ASSERT(spirv != nullptr);
    ON_SCOPE_EXIT(delete[] spirv);
    ASSERT(fread(spirv, 1, size, f) == size);
    VkShaderModuleCreateInfo ci{
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, .codeSize = size, .pCode = spirv};
    VkShaderModule mod;
    VK_ASSERT(vkCreateShaderModule(device, &ci, nullptr, &mod));
    return mod;
}

static VkPipeline CreateGraphicsPipeline(VkDevice device,
                                         VkPipelineLayout layout,
                                         VkFormat colorFormat,
                                         VkFormat depthFormat,
                                         VkSampleCountFlagBits samples,
                                         const char* shaderName,
                                         VkPrimitiveTopology topology)
{
    VkShaderModule vertex = LoadModule(device, shaderName, "vert");
    ON_SCOPE_EXIT(vkDestroyShaderModule(device, vertex, nullptr));
    VkShaderModule fragment = LoadModule(device, shaderName, "frag");
    ON_SCOPE_EXIT(vkDestroyShaderModule(device, fragment, nullptr));
    VkPipelineShaderStageCreateInfo shaderStages[2]{
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = vertex, .pName = "main"},
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = fragment, .pName = "main"},
    };
    VkPipelineVertexInputStateCreateInfo vertexInputState{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo inputAssemblyState{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = topology};
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
        .rasterizationSamples = samples};
    VkPipelineColorBlendAttachmentState blendAttachment{
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
                          | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT};
    VkPipelineColorBlendStateCreateInfo colorBlendState{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &blendAttachment};
    VkPipelineDepthStencilStateCreateInfo depthStencilState{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = VK_TRUE,
        .depthWriteEnable = VK_TRUE,
        .depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL};
    VkPipelineRenderingCreateInfo renderingCI{.sType =
                                                  VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
                                              .colorAttachmentCount = 1,
                                              .pColorAttachmentFormats = &colorFormat,
                                              .depthAttachmentFormat = depthFormat};
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
                                            .pDepthStencilState = &depthStencilState,
                                            .pColorBlendState = &colorBlendState,
                                            .pDynamicState = &dynamicState,
                                            .layout = layout};
    VkPipeline pipeline;
    VK_ASSERT(
        vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineCI, nullptr, &pipeline));
    return pipeline;
}

static glm::uvec2 PackDevicePtr(VkDeviceAddress gp)
{
    return {U32(gp & U32(-1)), U32(gp >> 32)};
}

static constexpr U32 kMaxFramesInFlight = 2;
static constexpr VkSampleCountFlagBits kMsaaSamples = VK_SAMPLE_COUNT_32_BIT;

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
        SDL_CreateWindow("Triangle", 1280U, 720U, SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
    ASSERT(window != nullptr);
    VkSurfaceKHR surface;
    ASSERT(SDL_Vulkan_CreateSurface(window, instance, nullptr, &surface));
    glm::ivec2 windowSize;
    ASSERT(SDL_GetWindowSize(window, &windowSize.x, &windowSize.y));

    // Orbit camera state
    float camYaw = 0.0f;
    float camPitch = 0.3f;
    float camDist = 3.0f;
    bool mouseDown = false;

    VkSurfaceCapabilitiesKHR surfaceCaps{};
    VK_ASSERT(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &surfaceCaps));
    VkExtent2D swapchainExtent{surfaceCaps.currentExtent};
    if (surfaceCaps.currentExtent.width == 0xFFFFFFFF)
        swapchainExtent = {.width = (U32)windowSize.x, .height = (U32)windowSize.y};

    // Swapchain
    constexpr VkFormat kImageFormat = VK_FORMAT_B8G8R8A8_SRGB;
    constexpr VkFormat kDepthFormat = VK_FORMAT_D32_SFLOAT;
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
                                         .presentMode = VK_PRESENT_MODE_MAILBOX_KHR};
    VkSwapchainKHR swapchain;
    VK_ASSERT(vkCreateSwapchainKHR(device, &swapchainCI, nullptr, &swapchain));

    U32 imageCount;
    VkImage swapchainImages[2];
    VkImageView swapchainImageViews[ARRAY_COUNT(swapchainImages)];
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

    // MSAA color + depth images
    VkImage msaaColorImage, depthImage;
    VkDeviceMemory msaaColorMemory, depthMemory;
    VkImageView msaaColorView, depthView;
    CreateImage(device,
                physicalDevice,
                (U32)windowSize.x,
                (U32)windowSize.y,
                kImageFormat,
                kMsaaSamples,
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                VK_IMAGE_ASPECT_COLOR_BIT,
                msaaColorImage,
                msaaColorMemory,
                msaaColorView);
    CreateImage(device,
                physicalDevice,
                (U32)windowSize.x,
                (U32)windowSize.y,
                kDepthFormat,
                kMsaaSamples,
                VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                VK_IMAGE_ASPECT_DEPTH_BIT,
                depthImage,
                depthMemory,
                depthView);

    // Command pool + buffers
    VkCommandPoolCreateInfo commandPoolCI{.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
                                          .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
                                          .queueFamilyIndex = queueFamily};
    VkCommandPool commandPool;
    VK_ASSERT(vkCreateCommandPool(device, &commandPoolCI, nullptr, &commandPool));

    VkCommandBuffer commandBuffers[kMaxFramesInFlight];
    VkCommandBufferAllocateInfo cbAllocCI{.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
                                          .commandPool = commandPool,
                                          .commandBufferCount = kMaxFramesInFlight};
    VK_ASSERT(vkAllocateCommandBuffers(device, &cbAllocCI, commandBuffers));

    // Sync objects
    VkFence fences[kMaxFramesInFlight];
    VkSemaphore presentSemaphores[kMaxFramesInFlight];
    VkSemaphore renderSemaphores[ARRAY_COUNT(swapchainImages)]; // one per swapchain image
    VkFenceCreateInfo fenceCI{.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
                              .flags = VK_FENCE_CREATE_SIGNALED_BIT};
    VkSemaphoreCreateInfo semaphoreCI{.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    for (U32 i = 0; i < kMaxFramesInFlight; i++)
    {
        VK_ASSERT(vkCreateFence(device, &fenceCI, nullptr, &fences[i]));
        VK_ASSERT(vkCreateSemaphore(device, &semaphoreCI, nullptr, &presentSemaphores[i]));
    }
    for (U32 i = 0; i < imageCount; i++)
    {
        VK_ASSERT(vkCreateSemaphore(device, &semaphoreCI, nullptr, &renderSemaphores[i]));
    }

    // Scene buffer
    VkBuffer sceneBuf;
    VkDeviceMemory sceneMem;
    {
        VkBufferCreateInfo bufCI{.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                                 .size = sizeof(GpuScene),
                                 .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
                                          | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT};
        VK_ASSERT(vkCreateBuffer(device, &bufCI, nullptr, &sceneBuf));

        VkMemoryRequirements memReqs;
        vkGetBufferMemoryRequirements(device, sceneBuf, &memReqs);
        VkMemoryAllocateFlagsInfo allocFlags{.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO,
                                             .flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT};
        VkMemoryAllocateInfo memAI{.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                                   .pNext = &allocFlags,
                                   .allocationSize = memReqs.size,
                                   .memoryTypeIndex =
                                       FindMemoryType(physicalDevice,
                                                      memReqs.memoryTypeBits,
                                                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
                                                          | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)};
        VK_ASSERT(vkAllocateMemory(device, &memAI, nullptr, &sceneMem));
        VK_ASSERT(vkBindBufferMemory(device, sceneBuf, sceneMem, 0));
    }
    GpuScene* gpuScene;
    VK_ASSERT(vkMapMemory(device, sceneMem, 0, sizeof(GpuScene), 0, (void**)&gpuScene));
    for (U32 i = 0; i < SCENE_POINT_COUNT; i++)
    {
        float theta = HashF(i, 0) * 2.0f * 3.14159265f;
        float z = HashF(i, 1) * 2.0f - 1.0f;
        float r = sqrtf(1.0f - z * z);
        gpuScene->pointPositions[i] =
            glm::vec4(r * cosf(theta), z, r * sinf(theta), 0) * (float)SCENE_ORBIT_RADIUS;
    }
    VkBufferDeviceAddressInfo sceneBufAddressInfo{
        .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, .buffer = sceneBuf};
    VkDeviceAddress gpScene = vkGetBufferDeviceAddress(device, &sceneBufAddressInfo);
    ASSERT(gpScene != 0);

    // Pipeline layout
    VkPushConstantRange pushRange{.stageFlags =
                                      VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                  .offset = 0,
                                  .size = sizeof(PushConstants)};
    VkPipelineLayoutCreateInfo pipelineLayoutCI{.sType =
                                                    VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                                                .pushConstantRangeCount = 1,
                                                .pPushConstantRanges = &pushRange};
    VkPipelineLayout pipelineLayout;
    VK_ASSERT(vkCreatePipelineLayout(device, &pipelineLayoutCI, nullptr, &pipelineLayout));
    VkPipeline globePipeline = CreateGraphicsPipeline(device,
                                                      pipelineLayout,
                                                      kImageFormat,
                                                      kDepthFormat,
                                                      kMsaaSamples,
                                                      "globe",
                                                      VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    VkPipeline pointPipeline = CreateGraphicsPipeline(device,
                                                      pipelineLayout,
                                                      kImageFormat,
                                                      kDepthFormat,
                                                      kMsaaSamples,
                                                      "point",
                                                      VK_PRIMITIVE_TOPOLOGY_POINT_LIST);

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

        VkImageMemoryBarrier2 barriers[3]{
            {.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
             .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
             .srcAccessMask = 0,
             .dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
             .dstAccessMask =
                 VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
             .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
             .newLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
             .image = swapchainImages[imageIndex],
             .subresourceRange{
                 .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1}},
            {.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
             .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
             .srcAccessMask = 0,
             .dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
             .dstAccessMask =
                 VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
             .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
             .newLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
             .image = msaaColorImage,
             .subresourceRange{
                 .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1}},
            {.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
             .srcStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT
                             | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
             .srcAccessMask = 0,
             .dstStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT
                             | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
             .dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT
                              | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
             .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
             .newLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
             .image = depthImage,
             .subresourceRange{
                 .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT, .levelCount = 1, .layerCount = 1}}};
        VkDependencyInfo depInfo{.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                                 .imageMemoryBarrierCount = 3,
                                 .pImageMemoryBarriers = barriers};
        vkCmdPipelineBarrier2(cb, &depInfo);

        VkRenderingAttachmentInfo colorAttachment{
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = msaaColorView,
            .imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
            .resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT,
            .resolveImageView = swapchainImageViews[imageIndex],
            .resolveImageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .clearValue{.color{0.0f, 0.0f, 0.0f, 1.0f}}};
        VkRenderingAttachmentInfo depthAttachment{.sType =
                                                      VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                                                  .imageView = depthView,
                                                  .imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
                                                  .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                                                  .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                                                  .clearValue{.depthStencil{1.0f, 0}}};
        VkRenderingInfo renderingInfo{
            .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
            .renderArea{.extent{.width = (U32)windowSize.x, .height = (U32)windowSize.y}},
            .layerCount = 1,
            .colorAttachmentCount = 1,
            .pColorAttachments = &colorAttachment,
            .pDepthAttachment = &depthAttachment};
        vkCmdBeginRendering(cb, &renderingInfo);

        VkViewport vp{.width = float(windowSize.x),
                      .height = float(windowSize.y),
                      .minDepth = 0.0f,
                      .maxDepth = 1.0f};
        VkRect2D scissor{.extent{.width = (U32)windowSize.x, .height = (U32)windowSize.y}};
        vkCmdSetViewport(cb, 0, 1, &vp);
        vkCmdSetScissor(cb, 0, 1, &scissor);

        {
            float cy = cosf(camYaw), sy = sinf(camYaw);
            float cp = cosf(camPitch), sp = sinf(camPitch);
            glm::vec3 pos = glm::vec3(cy * cp, sp, sy * cp) * camDist;
            glm::vec3 fwd = glm::normalize(-pos);
            glm::vec3 right = glm::normalize(glm::cross(fwd, glm::vec3(0, 1, 0)));
            glm::vec3 up = glm::cross(right, fwd);
            float aspect = float(windowSize.x) / float(windowSize.y);
            float halfTan = tanf(35.0f * 3.14159265f / 180.0f);
            glm::mat4 view(
                glm::vec4(right.x, up.x, -fwd.x, 0),
                glm::vec4(right.y, up.y, -fwd.y, 0),
                glm::vec4(right.z, up.z, -fwd.z, 0),
                glm::vec4(-glm::dot(right, pos), -glm::dot(up, pos), glm::dot(fwd, pos), 1));
            float n = 0.1f, f = 100.0f;
            glm::mat4 proj(glm::vec4(1.0f / (aspect * halfTan), 0, 0, 0),
                           glm::vec4(0, -1.0f / halfTan, 0, 0),
                           glm::vec4(0, 0, f / (n - f), -1),
                           glm::vec4(0, 0, n * f / (n - f), 0));
            PushConstants pc{.screenToWorld = glm::mat4(glm::vec4(right * aspect * halfTan, 0),
                                                        glm::vec4(-up * halfTan, 0),
                                                        glm::vec4(fwd, 0),
                                                        glm::vec4(pos, 1)),
                             .worldToScreen = proj * view,
                             .gpScene = PackDevicePtr(gpScene),
                             ._pad0 = glm::uvec2(0, 0)};
            vkCmdPushConstants(cb,
                               pipelineLayout,
                               VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                               0,
                               sizeof(pc),
                               &pc);
        }

        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, globePipeline);
        vkCmdDraw(cb, 3, 1, 0, 0);

        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pointPipeline);
        vkCmdDraw(cb, SCENE_POINT_COUNT, 1, 0, 0);

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

        frameIndex = (frameIndex + 1) % kMaxFramesInFlight;

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
            if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == 1)
                mouseDown = true;
            if (event.type == SDL_EVENT_MOUSE_BUTTON_UP && event.button.button == 1)
                mouseDown = false;
            if (event.type == SDL_EVENT_MOUSE_MOTION && mouseDown)
            {
                camYaw += event.motion.xrel * 0.005f;
                camPitch += event.motion.yrel * 0.005f;
                float limit = 1.55f;
                if (camPitch > limit)
                    camPitch = limit;
                if (camPitch < -limit)
                    camPitch = -limit;
            }
            if (event.type == SDL_EVENT_MOUSE_WHEEL)
            {
                camDist -= event.wheel.y * 0.3f;
                if (camDist < 1.5f)
                    camDist = 1.5f;
                if (camDist > 20.0f)
                    camDist = 20.0f;
            }
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
            DestroyImage(device, msaaColorImage, msaaColorMemory, msaaColorView);
            DestroyImage(device, depthImage, depthMemory, depthView);
            CreateImage(device,
                        physicalDevice,
                        (U32)windowSize.x,
                        (U32)windowSize.y,
                        kImageFormat,
                        kMsaaSamples,
                        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                        VK_IMAGE_ASPECT_COLOR_BIT,
                        msaaColorImage,
                        msaaColorMemory,
                        msaaColorView);
            CreateImage(device,
                        physicalDevice,
                        (U32)windowSize.x,
                        (U32)windowSize.y,
                        kDepthFormat,
                        kMsaaSamples,
                        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                        VK_IMAGE_ASPECT_DEPTH_BIT,
                        depthImage,
                        depthMemory,
                        depthView);
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
    vkDestroyPipeline(device, globePipeline, nullptr);
    vkDestroyPipeline(device, pointPipeline, nullptr);
    vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
    for (U32 i = 0; i < kMaxFramesInFlight; i++)
    {
        vkDestroyFence(device, fences[i], nullptr);
        vkDestroySemaphore(device, presentSemaphores[i], nullptr);
    }
    for (U32 i = 0; i < imageCount; i++)
    {
        vkDestroySemaphore(device, renderSemaphores[i], nullptr);
        vkDestroyImageView(device, swapchainImageViews[i], nullptr);
    }
    DestroyImage(device, msaaColorImage, msaaColorMemory, msaaColorView);
    DestroyImage(device, depthImage, depthMemory, depthView);
    vkDestroyBuffer(device, sceneBuf, nullptr);
    vkUnmapMemory(device, sceneMem);
    vkFreeMemory(device, sceneMem, nullptr);
    vkDestroyCommandPool(device, commandPool, nullptr);
    vkDestroySwapchainKHR(device, swapchain, nullptr);
    vkDestroySurfaceKHR(instance, surface, nullptr);
    vkDestroyDevice(device, nullptr);
    vkDestroyInstance(instance, nullptr);
    SDL_DestroyWindow(window);
    SDL_Quit();
}

#include "shaders.h"
#include "shared.h"
#include "util.h"

#define VOLK_IMPLEMENTATION
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <cstring>
#include <math.h>
#include <volk.h>

static constexpr U32 kMaxNumSwapchainImages = 2;
static constexpr VkSampleCountFlagBits kMsaaSamples = VK_SAMPLE_COUNT_4_BIT;
constexpr VkFormat kImageFormat = VK_FORMAT_B8G8R8A8_SRGB;
constexpr VkFormat kDepthFormat = VK_FORMAT_D32_SFLOAT;

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
            LOG_FATAL("VK_ASSERT FAIL: %s|res=%d", #line, _r);                                     \
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

struct App
{
    VkInstance instance;
    VkPhysicalDevice physicalDevice;
    VkDevice device;
    VkQueue queue;
    U32 queueFamily;

    SDL_Window* window;
    VkSurfaceKHR surface;
    U32 windowSizeX;
    U32 windowSizeY;

    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    U32 imageCount = 0;
    VkImage swapchainImages[kMaxNumSwapchainImages];
    VkImageView swapchainImageViews[kMaxNumSwapchainImages];
    VkImage msaaColorImage = VK_NULL_HANDLE;
    VkDeviceMemory msaaColorMemory = VK_NULL_HANDLE;
    VkImageView msaaColorView = VK_NULL_HANDLE;
    VkImage depthImage = VK_NULL_HANDLE;
    VkDeviceMemory depthMemory = VK_NULL_HANDLE;
    VkImageView depthView = VK_NULL_HANDLE;

    VkCommandPool commandPool;
    VkCommandBuffer cb;
    VkFence fence;
    VkSemaphore presentSemaphore;
    VkSemaphore renderSemaphores[kMaxNumSwapchainImages];

    VkBuffer sceneBuf;
    VkDeviceMemory sceneMem;
    GpuScene* gpuScene;
    VkDeviceAddress gpScene;

    VkPipelineLayout pipelineLayout;
    VkShaderEXT globeShaders[2];
    VkShaderEXT pointShaders[2];

    float camYaw = 0.0f;
    float camPitch = 0.3f;
    float camDist = 3.0f;
    bool mouseDown = false;

    void CreateImage(U32 w,
                     U32 h,
                     VkFormat fmt,
                     VkSampleCountFlagBits samples,
                     VkImageUsageFlags usage,
                     VkImageAspectFlags aspect,
                     VkImage& img,
                     VkDeviceMemory& mem,
                     VkImageView& view);
    void DestroyImage(VkImage img, VkDeviceMemory mem, VkImageView view);
    void RefreshSwapchain();
    void CreateVertFragPair(const U32* const (&pSpvs)[2],
                            const U64 (&sizes)[2],
                            VkShaderEXT (&shaders)[2]);

    void Init();
    void Run();
    void Shutdown();
};

void App::CreateImage(U32 w,
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
                            .memoryTypeIndex = FindMemoryType(physicalDevice,
                                                              reqs.memoryTypeBits,
                                                              VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)};
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

void App::DestroyImage(VkImage img, VkDeviceMemory mem, VkImageView view)
{
    vkDestroyImageView(device, view, nullptr);
    vkDestroyImage(device, img, nullptr);
    vkFreeMemory(device, mem, nullptr);
}

void App::RefreshSwapchain()
{
    VkSwapchainCreateInfoKHR swapchainCI{.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
                                         .surface = surface,
                                         .imageFormat = kImageFormat,
                                         .imageColorSpace = VK_COLORSPACE_SRGB_NONLINEAR_KHR,
                                         .imageArrayLayers = 1,
                                         .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                                         .preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
                                         .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
                                         .presentMode = VK_PRESENT_MODE_MAILBOX_KHR};

    VkSurfaceCapabilitiesKHR surfaceCaps{};
    VK_ASSERT(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &surfaceCaps));
    swapchainCI.minImageCount = surfaceCaps.minImageCount;
    swapchainCI.imageExtent = surfaceCaps.currentExtent.width == 0xFFFFFFFF
                                  ? VkExtent2D{.width = windowSizeX, .height = windowSizeY}
                                  : surfaceCaps.currentExtent;
    VkSwapchainKHR oldSwapchain = swapchain;
    swapchainCI.oldSwapchain = oldSwapchain;
    VK_ASSERT(vkCreateSwapchainKHR(device, &swapchainCI, nullptr, &swapchain));
    if (oldSwapchain != VK_NULL_HANDLE)
    {
        for (U32 i = 0; i < imageCount; i++)
            vkDestroyImageView(device, swapchainImageViews[i], nullptr);
        vkDestroySwapchainKHR(device, oldSwapchain, nullptr);
    }
    if (msaaColorImage != VK_NULL_HANDLE)
        DestroyImage(msaaColorImage, msaaColorMemory, msaaColorView);
    if (depthImage != VK_NULL_HANDLE)
        DestroyImage(depthImage, depthMemory, depthView);
    CreateImage(windowSizeX,
                windowSizeY,
                kImageFormat,
                kMsaaSamples,
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                VK_IMAGE_ASPECT_COLOR_BIT,
                msaaColorImage,
                msaaColorMemory,
                msaaColorView);
    CreateImage(windowSizeX,
                windowSizeY,
                kDepthFormat,
                kMsaaSamples,
                VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                VK_IMAGE_ASPECT_DEPTH_BIT,
                depthImage,
                depthMemory,
                depthView);

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
}

void App::CreateVertFragPair(const U32* const (&pSpvs)[2],
                             const U64 (&sizes)[2],
                             VkShaderEXT (&shaders)[2])
{
    VkPushConstantRange pushRange{.stageFlags =
                                      VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                  .offset = 0,
                                  .size = sizeof(PushConstants)};

    VkShaderStageFlagBits stageFlags[] = {VK_SHADER_STAGE_VERTEX_BIT, VK_SHADER_STAGE_FRAGMENT_BIT};
    VkShaderStageFlags nextStage[] = {VK_SHADER_STAGE_FRAGMENT_BIT, 0};

    VkShaderCreateInfoEXT createInfos[2]{};
    for (int i = 0; i < 2; i++)
    {
        createInfos[i] = {.sType = VK_STRUCTURE_TYPE_SHADER_CREATE_INFO_EXT,
                          .stage = stageFlags[i],
                          .nextStage = nextStage[i],
                          .codeType = VK_SHADER_CODE_TYPE_SPIRV_EXT,
                          .codeSize = sizes[i],
                          .pCode = pSpvs[i],
                          .pName = "main",
                          .pushConstantRangeCount = 1,
                          .pPushConstantRanges = &pushRange};
    }

    VK_ASSERT(vkCreateShadersEXT(device, 2, createInfos, nullptr, shaders));
}

void App::Init()
{
    ASSERT(SDL_Init(SDL_INIT_VIDEO));
    {
        constexpr const char* kVulkanLibs[] = {
            nullptr,
#ifdef __APPLE__
            "/usr/local/lib/libvulkan.dylib",
#endif
        };
        bool loaded = false;
        for (const char* const lib : kVulkanLibs)
            if ((loaded = SDL_Vulkan_LoadLibrary(lib)))
                break;
        if (!loaded)
            LOG_FATAL("Failed to load Vulkan: %s", SDL_GetError());
    }
    volkInitializeCustom((PFN_vkGetInstanceProcAddr)SDL_Vulkan_GetVkGetInstanceProcAddr());

    VkApplicationInfo appInfo{.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
                              .pApplicationName = "Triangle",
                              .apiVersion = VK_API_VERSION_1_3};
    U32 availableExtensionsCount;
    VkExtensionProperties availableExtensions[32];
    vkEnumerateInstanceExtensionProperties(nullptr, &availableExtensionsCount, nullptr);
    ASSERT(availableExtensionsCount <= ARRAY_COUNT(availableExtensions));
    vkEnumerateInstanceExtensionProperties(nullptr, &availableExtensionsCount, availableExtensions);

    U32 instanceExtensionsCount;
    char const* const* instanceExtensions =
        SDL_Vulkan_GetInstanceExtensions(&instanceExtensionsCount);
    const char* filteredExtensions[32];
    U32 filteredExtensionsCount = 0;
    for (U32 i = 0; i < instanceExtensionsCount; i++)
    {
        bool available = false;
        for (U32 j = 0; j < availableExtensionsCount; j++)
        {
            if (!__builtin_strcmp(instanceExtensions[i], availableExtensions[j].extensionName))
            {
                ASSERT(filteredExtensionsCount < ARRAY_COUNT(filteredExtensions));
                filteredExtensions[filteredExtensionsCount++] = instanceExtensions[i];
                available = true;
                break;
            }
        }

        if (!available)
        {
            LOG_INFO("SDL instance extension unavailable: %s", instanceExtensions[i]);
        }
    }
    VkInstanceCreateFlags instanceFlags = 0;
    for (U32 i = 0; i < filteredExtensionsCount; ++i)
    {
        if (!__builtin_strcmp(filteredExtensions[i], VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME))
            instanceFlags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
    }
    VkInstanceCreateInfo instanceCI{
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .flags = instanceFlags,
        .pApplicationInfo = &appInfo,
        .enabledExtensionCount = filteredExtensionsCount,
        .ppEnabledExtensionNames = filteredExtensions,
    };
    VK_ASSERT(vkCreateInstance(&instanceCI, nullptr, &instance));
    volkLoadInstance(instance);

    U32 physicalDeviceCount;
    VkPhysicalDevice physicalDevices[4];
    VK_ASSERT(vkEnumeratePhysicalDevices(instance, &physicalDeviceCount, nullptr));
    ASSERT(physicalDeviceCount <= ARRAY_COUNT(physicalDevices));
    VK_ASSERT(vkEnumeratePhysicalDevices(instance, &physicalDeviceCount, physicalDevices));
    ASSERT(physicalDeviceCount > 0);
    physicalDevice = physicalDevices[0];

    VkPhysicalDeviceProperties2 deviceProperties{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
    vkGetPhysicalDeviceProperties2(physicalDevice, &deviceProperties);
    LOG_INFO("Device: %s", deviceProperties.properties.deviceName);

    U32 queueFamilyCount;
    VkQueueFamilyProperties queueFamilies[8];
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, nullptr);
    ASSERT(queueFamilyCount <= ARRAY_COUNT(queueFamilies));
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, queueFamilies);
    queueFamily = U32(-1);
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
    VkPhysicalDeviceShaderObjectFeaturesEXT shaderObjectFeatures{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_OBJECT_FEATURES_EXT,
        .shaderObject = VK_TRUE};
    VkPhysicalDeviceVulkan12Features enabledVk12Features{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
        .pNext = &shaderObjectFeatures,
        .bufferDeviceAddress = true};
    VkPhysicalDeviceVulkan13Features enabledVk13Features{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
        .pNext = &enabledVk12Features,
        .synchronization2 = true,
        .dynamicRendering = true};

    const char* deviceExtensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME,
                                      VK_EXT_SHADER_OBJECT_EXTENSION_NAME,
                                      "VK_KHR_portability_subset"};
    VkDeviceCreateInfo deviceCI{.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
                                .pNext = &enabledVk13Features,
                                .queueCreateInfoCount = 1,
                                .pQueueCreateInfos = &queueCI,
                                .enabledExtensionCount = ARRAY_COUNT(deviceExtensions),
                                .ppEnabledExtensionNames = deviceExtensions};
    VK_ASSERT(vkCreateDevice(physicalDevice, &deviceCI, nullptr, &device));
    vkGetDeviceQueue(device, queueFamily, 0, &queue);

    // Window
    window = SDL_CreateWindow("Triangle", 1280U, 720U, SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
    ASSERT(window != nullptr);
    ASSERT(SDL_Vulkan_CreateSurface(window, instance, nullptr, &surface));
    I32 windowSizeXs;
    I32 windowSizeYs;
    ASSERT(SDL_GetWindowSize(window, &windowSizeXs, &windowSizeYs));
    ASSERT(windowSizeXs > 0);
    ASSERT(windowSizeYs > 0);
    windowSizeX = windowSizeXs;
    windowSizeY = windowSizeYs;

    // Swapchain
    RefreshSwapchain();

    // Command pool + buffers
    VkCommandPoolCreateInfo commandPoolCI{.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
                                          .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
                                          .queueFamilyIndex = queueFamily};
    VK_ASSERT(vkCreateCommandPool(device, &commandPoolCI, nullptr, &commandPool));

    VkCommandBufferAllocateInfo cbAllocCI{.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
                                          .commandPool = commandPool,
                                          .commandBufferCount = 1};
    VK_ASSERT(vkAllocateCommandBuffers(device, &cbAllocCI, &cb));

    // Sync objects
    VkFenceCreateInfo fenceCI{.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
                              .flags = VK_FENCE_CREATE_SIGNALED_BIT};
    VkSemaphoreCreateInfo semaphoreCI{.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VK_ASSERT(vkCreateFence(device, &fenceCI, nullptr, &fence));
    VK_ASSERT(vkCreateSemaphore(device, &semaphoreCI, nullptr, &presentSemaphore));
    for (U32 i = 0; i < imageCount; i++)
        VK_ASSERT(vkCreateSemaphore(device, &semaphoreCI, nullptr, &renderSemaphores[i]));

    // Scene buffer
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
    VK_ASSERT(vkMapMemory(device, sceneMem, 0, sizeof(GpuScene), 0, (void**)&gpuScene));
    VkBufferDeviceAddressInfo sceneBufAddressInfo{
        .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, .buffer = sceneBuf};
    gpScene = vkGetBufferDeviceAddress(device, &sceneBufAddressInfo);
    ASSERT(gpScene != 0);

    for (U32 i = 0; i < kScenePointCount; i++)
    {
        float theta = HashF(i, 0) * 2.0f * 3.14159265f;
        float z = HashF(i, 1) * 2.0f - 1.0f;
        float r = sqrtf(1.0f - z * z);
        Vec3 point{r * cosf(theta), z, r * sinf(theta)};
        gpuScene->pointPositions[i] = float4(point * kSceneOrbitRadius, 0.0f);
    }

    // Pipeline layout
    VkPushConstantRange pushRange{.stageFlags =
                                      VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                  .offset = 0,
                                  .size = sizeof(PushConstants)};
    VkPipelineLayoutCreateInfo pipelineLayoutCI{.sType =
                                                    VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                                                .pushConstantRangeCount = 1,
                                                .pPushConstantRanges = &pushRange};
    VK_ASSERT(vkCreatePipelineLayout(device, &pipelineLayoutCI, nullptr, &pipelineLayout));
    CreateVertFragPair({globe_vert_spv, globe_frag_spv},
                       {globe_vert_spv_sizeInBytes, globe_frag_spv_sizeInBytes},
                       globeShaders);
    CreateVertFragPair({point_vert_spv, point_frag_spv},
                       {point_vert_spv_sizeInBytes, point_frag_spv_sizeInBytes},
                       pointShaders);
}

void App::Run()
{
    U32 imageIndex = 0;
    bool quit = false;
    bool updateSwapchain = false;
    while (!quit)
    {
        VK_ASSERT(vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX));
        VK_ASSERT(vkResetFences(device, 1, &fence));

        VkResult acquireResult = vkAcquireNextImageKHR(
            device, swapchain, UINT64_MAX, presentSemaphore, VK_NULL_HANDLE, &imageIndex);
        if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR || acquireResult == VK_SUBOPTIMAL_KHR)
            updateSwapchain = true;
        else
            VK_ASSERT(acquireResult);

        VK_ASSERT(vkResetCommandBuffer(cb, 0));
        VkCommandBufferBeginInfo cbBI{.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                                      .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
        VK_ASSERT(vkBeginCommandBuffer(cb, &cbBI));

        VkImageMemoryBarrier2 barriers[] = {
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
                                 .imageMemoryBarrierCount = ARRAY_COUNT(barriers),
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
            .renderArea{.extent{.width = windowSizeX, .height = windowSizeY}},
            .layerCount = 1,
            .colorAttachmentCount = 1,
            .pColorAttachments = &colorAttachment,
            .pDepthAttachment = &depthAttachment};
        vkCmdBeginRendering(cb, &renderingInfo);

        VkViewport vp{.width = float(windowSizeX),
                      .height = float(windowSizeY),
                      .minDepth = 0.0f,
                      .maxDepth = 1.0f};
        VkRect2D scissor{.extent{.width = windowSizeX, .height = windowSizeY}};
        vkCmdSetViewportWithCount(cb, 1, &vp);
        vkCmdSetScissorWithCount(cb, 1, &scissor);

        {
            float cy = cosf(camYaw), sy = sinf(camYaw);
            float cp = cosf(camPitch), sp = sinf(camPitch);
            Vec3 pos{cy * cp, sp, sy * cp};
            pos = pos * camDist;
            Vec3 fwd = Normalize(-pos);
            Vec3 right = Normalize(Cross(fwd, Vec3{0.0f, 1.0f, 0.0f}));
            Vec3 up = Cross(right, fwd);
            float aspect = float(windowSizeX) / float(windowSizeY);
            float halfTan = tanf(35.0f * 3.14159265f / 180.0f);
            float4x4 view = float4x4(float4(right.x, up.x, -fwd.x, 0.0f),
                                     float4(right.y, up.y, -fwd.y, 0.0f),
                                     float4(right.z, up.z, -fwd.z, 0.0f),
                                     float4(-Dot(right, pos), -Dot(up, pos), Dot(fwd, pos), 1.0f));
            float n = 0.1f, f = 100.0f;
            float4x4 proj = float4x4(float4(1.0f / (aspect * halfTan), 0.0f, 0.0f, 0.0f),
                                     float4(0.0f, -1.0f / halfTan, 0.0f, 0.0f),
                                     float4(0.0f, 0.0f, f / (n - f), -1.0f),
                                     float4(0.0f, 0.0f, n * f / (n - f), 0.0f));
            gpuScene->screenToWorld = float4x4(float4(right * (aspect * halfTan), 0.0f),
                                               float4(up * -halfTan, 0.0f),
                                               float4(fwd, 0.0f),
                                               float4(pos, 1.0f));
            gpuScene->worldToScreen = Mul(proj, view);
            PushConstants pc{.gpScene = gpScene};
            vkCmdPushConstants(cb,
                               pipelineLayout,
                               VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                               0,
                               sizeof(pc),
                               &pc);
        }

        // Dynamic state for shader objects
        vkCmdSetRasterizerDiscardEnable(cb, VK_FALSE);
        vkCmdSetPolygonModeEXT(cb, VK_POLYGON_MODE_FILL);
        vkCmdSetCullMode(cb, VK_CULL_MODE_NONE);
        vkCmdSetFrontFace(cb, VK_FRONT_FACE_COUNTER_CLOCKWISE);
        vkCmdSetDepthBiasEnable(cb, VK_FALSE);
        vkCmdSetDepthTestEnable(cb, VK_TRUE);
        vkCmdSetDepthWriteEnable(cb, VK_TRUE);
        vkCmdSetDepthCompareOp(cb, VK_COMPARE_OP_LESS_OR_EQUAL);
        vkCmdSetDepthBoundsTestEnable(cb, VK_FALSE);
        vkCmdSetStencilTestEnable(cb, VK_FALSE);
        vkCmdSetPrimitiveRestartEnable(cb, VK_FALSE);
        vkCmdSetRasterizationSamplesEXT(cb, kMsaaSamples);
        VkSampleMask sampleMask = 0xFFFFFFFF;
        vkCmdSetSampleMaskEXT(cb, kMsaaSamples, &sampleMask);
        vkCmdSetAlphaToCoverageEnableEXT(cb, VK_FALSE);
        VkBool32 blendEnable = VK_FALSE;
        vkCmdSetColorBlendEnableEXT(cb, 0, 1, &blendEnable);
        VkColorBlendEquationEXT blendEquation{};
        vkCmdSetColorBlendEquationEXT(cb, 0, 1, &blendEquation);
        VkColorComponentFlags colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
                                               | VK_COLOR_COMPONENT_B_BIT
                                               | VK_COLOR_COMPONENT_A_BIT;
        vkCmdSetColorWriteMaskEXT(cb, 0, 1, &colorWriteMask);
        vkCmdSetLogicOpEnableEXT(cb, VK_FALSE);
        vkCmdSetVertexInputEXT(cb, 0, nullptr, 0, nullptr);

        // Unbind unused shader stages
        VkShaderStageFlagBits unusedStages[] = {VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT,
                                                VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT,
                                                VK_SHADER_STAGE_GEOMETRY_BIT};
        VkShaderEXT nullShaders[] = {VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE};
        vkCmdBindShadersEXT(cb, 3, unusedStages, nullShaders);

        VkShaderStageFlagBits vertFragStages[] = {VK_SHADER_STAGE_VERTEX_BIT,
                                                  VK_SHADER_STAGE_FRAGMENT_BIT};

        vkCmdSetPrimitiveTopology(cb, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
        vkCmdBindShadersEXT(cb, 2, vertFragStages, globeShaders);
        vkCmdDraw(cb, 3, 1, 0, 0);

        vkCmdSetPrimitiveTopology(cb, VK_PRIMITIVE_TOPOLOGY_POINT_LIST);
        vkCmdBindShadersEXT(cb, 2, vertFragStages, pointShaders);
        vkCmdDraw(cb, kScenePointCount, 1, 0, 0);

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
                                .pWaitSemaphores = &presentSemaphore,
                                .pWaitDstStageMask = &waitStage,
                                .commandBufferCount = 1,
                                .pCommandBuffers = &cb,
                                .signalSemaphoreCount = 1,
                                .pSignalSemaphores = &renderSemaphores[imageIndex]};
        VK_ASSERT(vkQueueSubmit(queue, 1, &submitInfo, fence));

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
                camDist -= event.wheel.y * 0.1f;
                camDist = Max(camDist, 1.1f);
                camDist = Min(camDist, 10.0f);
            }
        }

        if (updateSwapchain)
        {
            updateSwapchain = false;
            I32 windowSizeXs;
            I32 windowSizeYs;
            ASSERT(SDL_GetWindowSize(window, &windowSizeXs, &windowSizeYs));
            ASSERT(windowSizeXs > 0);
            ASSERT(windowSizeYs > 0);
            windowSizeX = windowSizeXs;
            windowSizeY = windowSizeYs;
            VK_ASSERT(vkDeviceWaitIdle(device));
            RefreshSwapchain();
        }
    }
}

void App::Shutdown()
{
    VK_ASSERT(vkDeviceWaitIdle(device));
    for (auto s : globeShaders)
        vkDestroyShaderEXT(device, s, nullptr);
    for (auto s : pointShaders)
        vkDestroyShaderEXT(device, s, nullptr);
    vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
    vkDestroyFence(device, fence, nullptr);
    vkDestroySemaphore(device, presentSemaphore, nullptr);
    for (U32 i = 0; i < imageCount; i++)
    {
        vkDestroySemaphore(device, renderSemaphores[i], nullptr);
        vkDestroyImageView(device, swapchainImageViews[i], nullptr);
    }
    DestroyImage(msaaColorImage, msaaColorMemory, msaaColorView);
    DestroyImage(depthImage, depthMemory, depthView);
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

int main()
{
    App app{};
    app.Init();
    app.Run();
    app.Shutdown();
}

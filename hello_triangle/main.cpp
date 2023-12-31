#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <random>
#include <set>
#include <stdexcept>
#include <unordered_map>
#include <vector>

#define TINYOBJLOADER_IMPLEMENTATION
#include "thirdparty/tinyobjloader/tiny_obj_loader.h"

#define STB_IMAGE_IMPLEMENTATION
#include "thirdparty/stb/stb_image.h"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE // Use Vulkan depth range instead of OpenGL's.
#define GLM_ENABLE_EXPERIMENTAL // Required for hash functions.
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/hash.hpp>

constexpr const char* APP_NAME = "Hello Triangle";
constexpr int WINDOW_WIDTH = 800;
constexpr int WINDOW_HEIGHT = 600;
constexpr int MAX_FRAMES_IN_FLIGHT = 2;
constexpr const char* MODEL_PATH = "viking_room.obj";
constexpr const char* TEXTURE_PATH = "viking_room.png";
constexpr int PARTICLE_COUNT = 1'000;
constexpr float PI = 3.1415926535f;

const std::vector<const char*> validationLayers = {
    "VK_LAYER_KHRONOS_validation",
};

const std::vector<const char*> deviceExtensions = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME,
};

#ifdef NDEBUG
constexpr bool enableValidationLayers = false;
#else
constexpr bool enableValidationLayers = true;
#endif

std::vector<char> readFile(const std::string& pFilename)
{
    std::ifstream file(pFilename, std::ios::ate | std::ios::binary);

    if (!file.is_open()) {
        throw std::runtime_error("Couldn't open file: " + pFilename);
    }

    size_t fileSize = (size_t)file.tellg();
    std::vector<char> buffer(fileSize);
    // The advantage of starting to read at the end of the file is that we can
    // use the read position to determine the size of the file and allocate a
    // buffer:
    file.seekg(0);
    file.read(buffer.data(), fileSize);
    file.close();

    return buffer;
}

VkResult CreateDebugUtilsMessengerEXT(VkInstance pInstance, const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDebugUtilsMessengerEXT* pDebugMessenger)
{
    auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(pInstance, "vkCreateDebugUtilsMessengerEXT");
    if (func != nullptr) {
        return func(pInstance, pCreateInfo, pAllocator, pDebugMessenger);
    } else {
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    }
}

void DestroyDebugUtilsMessengerEXT(VkInstance pInstance, VkDebugUtilsMessengerEXT pDebugMessenger, const VkAllocationCallbacks* pAllocator)
{
    auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(pInstance, "vkDestroyDebugUtilsMessengerEXT");
    if (func != nullptr) {
        func(pInstance, pDebugMessenger, pAllocator);
    }
};

struct Vertex {
    glm::vec3 position;
    glm::vec3 color;
    glm::vec2 texCoord;

    static VkVertexInputBindingDescription getBindingDescription()
    {
        VkVertexInputBindingDescription bindingDescription {};
        bindingDescription.binding = 0;
        bindingDescription.stride = sizeof(Vertex);
        bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        return bindingDescription;
    }

    static std::array<VkVertexInputAttributeDescription, 3> getAttributeDescriptions()
    {
        std::array<VkVertexInputAttributeDescription, 3> attributeDescriptions {};

        attributeDescriptions[0].binding = 0;
        attributeDescriptions[0].location = 0;
        attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributeDescriptions[0].offset = offsetof(Vertex, position);

        attributeDescriptions[1].binding = 0;
        attributeDescriptions[1].location = 1;
        attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributeDescriptions[1].offset = offsetof(Vertex, color);

        attributeDescriptions[2].binding = 0;
        attributeDescriptions[2].location = 2;
        attributeDescriptions[2].format = VK_FORMAT_R32G32_SFLOAT;
        attributeDescriptions[2].offset = offsetof(Vertex, texCoord);

        return attributeDescriptions;
    }

    bool operator==(const Vertex& pOther) const
    {
        return position == pOther.position && color == pOther.color && texCoord == pOther.texCoord;
    }
};

namespace std {
template <>
struct hash<Vertex> {
    // Hash functions are a complex topic, but cppreference.com recommends the
    // following approach combining the fields of a struct to create a decent
    // quality hash function:
    size_t operator()(Vertex const& pVertex) const
    {
        return ((hash<glm::vec3>()(pVertex.position) ^ (hash<glm::vec3>()(pVertex.color) << 1)) >> 1)
            ^ (hash<glm::vec2>()(pVertex.texCoord) << 1);
    }
};
}

class HelloTriangleApplication {
public:
    void run()
    {
        initWindow();
        initVulkan();
        mainLoop();
        cleanup();
    }

private:
    struct UniformBufferObject {
        glm::mat4 model;
        glm::mat4 view;
        glm::mat4 projection;
    };

    struct Particle {
        glm::vec2 position;
        glm::vec2 velocity;
        glm::vec4 color;
    };

    GLFWwindow* mWindow;
    VkInstance mInstance;
    VkDebugUtilsMessengerEXT mDebugMessenger;
    VkPhysicalDevice mPhysicalDevice = VK_NULL_HANDLE;
    VkDevice mDevice;
    VkQueue mGraphicsQueue;
    VkQueue mComputeQueue;
    VkQueue mPresentQueue;
    VkSurfaceKHR mSurface;
    VkSwapchainKHR mSwapChain;
    std::vector<VkImage> mSwapChainImages;
    VkFormat mSwapChainImageFormat;
    VkExtent2D mSwapChainExtent;
    std::vector<VkImageView> mSwapChainImageViews;
    VkRenderPass mRenderPass;
    VkDescriptorSetLayout mDescriptorSetLayout;
    VkDescriptorSetLayout mComputeDescriptorSetLayout;
    VkPipelineLayout mPipelineLayout;
    VkPipelineLayout mComputePipelineLayout;
    VkPipeline mGraphicsPipeline;
    VkPipeline mComputePipeline;
    std::vector<VkFramebuffer> mSwapChainFramebuffers;
    VkCommandPool mCommandPool;
    VkDescriptorPool mDescriptorPool;
    std::vector<VkDescriptorSet> mDescriptorSets;
    std::vector<VkDescriptorSet> mComputeDescriptorSets;
    std::vector<VkCommandBuffer> mCommandBuffers;
    std::vector<VkSemaphore> mImageAvailableSemaphores;
    std::vector<VkSemaphore> mRenderFinishedSemaphores;
    std::vector<VkFence> mInFlightFences;

    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    VkBuffer mVertexBuffer;
    VkDeviceMemory mVertexBufferMemory;
    VkBuffer mIndexBuffer;
    VkDeviceMemory mIndexBufferMemory;

    std::vector<VkBuffer> mShaderStorageBuffers;
    std::vector<VkDeviceMemory> mShaderStorageBuffersMemory;

    uint32_t mMipLevels;
    VkImage mTextureImage;
    VkDeviceMemory mTextureImageMemory;
    VkImageView mTextureImageView;
    VkSampler mTextureSampler;

    VkImage mColorImage;
    VkDeviceMemory mColorImageMemory;
    VkImageView mColorImageView;

    VkImage mDepthImage;
    VkDeviceMemory mDepthImageMemory;
    VkImageView mDepthImageView;

    VkSampleCountFlagBits mMsaaSamples = VK_SAMPLE_COUNT_1_BIT;

    std::vector<VkBuffer> mUniformBuffers;
    std::vector<VkDeviceMemory> mUniformBuffersMemory;
    std::vector<void*> mUniformBuffersMapped;

    // The current frame index (incremented every drawn frame, wraps around `MAX_FRAMES_IN_FLIGHT`).
    uint32_t mCurrentFrame = 0;
    bool mFramebufferResized = false;

    bool checkValidationLayerSupport()
    {
        uint32_t layerCount;
        vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
        std::vector<VkLayerProperties> availableLayers(layerCount);
        vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

        for (const char* layerName : validationLayers) {
            for (const auto& layerProperties : availableLayers) {
                if (strcmp(layerName, layerProperties.layerName) == 0) {
                    return true;
                }
            }
        }

        return false;
    }

    std::vector<const char*> getRequiredExtensions()
    {
        uint32_t glfwExtensionCount = 0;
        const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

        std::vector<const char*> extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);

        if (enableValidationLayers) {
            extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        }

        return extensions;
    }

    void initWindow()
    {
        glfwInit();
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

        mWindow = glfwCreateWindow(WINDOW_WIDTH, WINDOW_HEIGHT, APP_NAME, nullptr, nullptr);
        glfwSetWindowUserPointer(mWindow, this);
        glfwSetFramebufferSizeCallback(mWindow, framebufferResizeCallback);
    }

    static void framebufferResizeCallback(GLFWwindow* window, int width, int height)
    {
        auto app = reinterpret_cast<HelloTriangleApplication*>(glfwGetWindowUserPointer(window));
        app->mFramebufferResized = true;
    }

    void createInstance()
    {
        if (enableValidationLayers) {
            std::cout << "Enabling validation layers, as this is a debug build.\n";
            if (!checkValidationLayerSupport()) {
                throw std::runtime_error("Validation layers were requested but are not available. Check if validation layers are installed.");
            }
        }

        VkApplicationInfo appInfo {};
        appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName = APP_NAME;
        appInfo.applicationVersion = VK_MAKE_API_VERSION(0, 1, 0, 0);
        appInfo.pEngineName = "No Engine";
        appInfo.engineVersion = VK_MAKE_API_VERSION(0, 1, 0, 0);
        appInfo.apiVersion = VK_API_VERSION_1_0;

        VkInstanceCreateInfo createInfo {};
        createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        createInfo.pApplicationInfo = &appInfo;

        auto extensions = getRequiredExtensions();
        createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
        createInfo.ppEnabledExtensionNames = extensions.data();

        // This is located outside the `if` statement to ensure that
        // it is not destroyed before the `vkCreateInstance()` call.
        VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo {};
        if (enableValidationLayers) {
            createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
            createInfo.ppEnabledLayerNames = validationLayers.data();
            populateDebugMessengerCreateInfo(debugCreateInfo);
            createInfo.pNext = (VkDebugUtilsMessengerCreateInfoEXT*)&debugCreateInfo;
        } else {
            createInfo.enabledLayerCount = 0;
            createInfo.pNext = nullptr;
        }

        if (vkCreateInstance(&createInfo, nullptr, &mInstance) != VK_SUCCESS) {
            throw std::runtime_error("Couldn't create Vulkan instance. Check if the graphics driver supports Vulkan 1.0.");
        };

        uint32_t extensionCount = 0;
        vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr);
    }

    static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
        VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
        VkDebugUtilsMessageTypeFlagsEXT messageType,
        const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
        void* pUserData)
    {
        const char* color;
        switch (messageSeverity) {
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT:
            color = "90"; // Bright black (= gray).
            break;
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT:
            color = "90"; // Bright black (= gray).
            break;
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT:
            color = "93"; // Bright yellow.
            break;
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:
            color = "91"; // Bright red.
            break;
        default:
            // Prevent compiler warning due to enum's max value.
            break;
        }
        std::cerr << "\x1b[1;" << color << "mVulkan:\x1b[22m " << pCallbackData->pMessage << "\x1b[0m" << std::endl;
        return VK_FALSE;
    }

    void populateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& pCreateInfo)
    {
        pCreateInfo = {};
        pCreateInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        // Don't include INFO severity as it's too verbose.
        pCreateInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        pCreateInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        pCreateInfo.pfnUserCallback = debugCallback;
    }

    void setupDebugMessenger()
    {
        if (!enableValidationLayers) {
            return;
        }

        VkDebugUtilsMessengerCreateInfoEXT createInfo;
        populateDebugMessengerCreateInfo(createInfo);

        if (CreateDebugUtilsMessengerEXT(mInstance, &createInfo, nullptr, &mDebugMessenger) != VK_SUCCESS) {
            throw std::runtime_error("Couldn't set up Vulkan debug messenger.");
        }
    }

    void createSurface()
    {
        if (glfwCreateWindowSurface(mInstance, mWindow, nullptr, &mSurface) != VK_SUCCESS) {
            throw std::runtime_error("Couldn't create Vulkan window surface.");
        }
    }

    void pickPhysicalDevice()
    {
        uint32_t deviceCount = 0;
        vkEnumeratePhysicalDevices(mInstance, &deviceCount, nullptr);
        if (deviceCount == 0) {
            throw std::runtime_error("Couldn't find any GPUs with Vulkan 1.0 support. Check if graphics drivers are up-to-date, and try rebooting if you've updated graphics drivers on Linux.");
        }

        std::vector<VkPhysicalDevice> devices(deviceCount);
        vkEnumeratePhysicalDevices(mInstance, &deviceCount, devices.data());
        for (const auto& device : devices) {
            if (isDeviceSuitable(device)) {
                mPhysicalDevice = device;
                mMsaaSamples = getMaxUsableSampleCount();
                break;
            }
        }

        if (mPhysicalDevice == VK_NULL_HANDLE) {
            throw std::runtime_error("Couldn't find a suitable GPU for Vulkan rendering. Check if the GPU supports all required extensions.");
        }
    };

    bool isDeviceSuitable(VkPhysicalDevice pDevice)
    {
        const QueueFamilyIndices indices = findQueueFamilies(pDevice);
        const bool extensionsSupported = checkDeviceExtensionSupport(pDevice);

        bool swapChainAdequate = false;
        if (extensionsSupported) {
            // Only try to query for swap chain support after verifying that the extension is available.
            SwapChainSupportDetails swapChainSupport = querySwapChainSupport(pDevice);
            swapChainAdequate = !swapChainSupport.formats.empty() && !swapChainSupport.presentModes.empty();
        }

        VkPhysicalDeviceFeatures supportedFeatures;
        vkGetPhysicalDeviceFeatures(pDevice, &supportedFeatures);

        return indices.isComplete() && extensionsSupported && swapChainAdequate && supportedFeatures.samplerAnisotropy;
    }

    bool checkDeviceExtensionSupport(VkPhysicalDevice pDevice)
    {
        uint32_t extensionCount;
        vkEnumerateDeviceExtensionProperties(pDevice, nullptr, &extensionCount, nullptr);
        std::vector<VkExtensionProperties> availableExtensions(extensionCount);
        vkEnumerateDeviceExtensionProperties(pDevice, nullptr, &extensionCount, availableExtensions.data());

        std::set<std::string> requiredExtensions(deviceExtensions.begin(), deviceExtensions.end());
        for (const auto& extension : availableExtensions) {
            // "Tick off" the required extension as supported.
            requiredExtensions.erase(extension.extensionName);
        }

        // If all required extensions are supported, the list is empty.
        return requiredExtensions.empty();
    }

    struct QueueFamilyIndices {
        std::optional<uint32_t> graphicsAndComputeFamily;
        std::optional<uint32_t> presentFamily;

        bool isComplete() const
        {
            return graphicsAndComputeFamily.has_value() && presentFamily.has_value();
        }
    };

    QueueFamilyIndices findQueueFamilies(VkPhysicalDevice pDevice)
    {
        QueueFamilyIndices indices;

        uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(pDevice, &queueFamilyCount, nullptr);
        std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(pDevice, &queueFamilyCount, queueFamilies.data());

        int i = 0;
        for (const auto& queueFamily : queueFamilies) {
            if ((queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) && (queueFamily.queueFlags & VK_QUEUE_COMPUTE_BIT)) {
                indices.graphicsAndComputeFamily = i;
            }

            VkBool32 presentSupport = false;
            vkGetPhysicalDeviceSurfaceSupportKHR(pDevice, i, mSurface, &presentSupport);
            if (presentSupport) {
                indices.presentFamily = i;
            }

            if (indices.isComplete()) {
                break;
            }

            i += 1;
        }

        return indices;
    }

    struct SwapChainSupportDetails {
        VkSurfaceCapabilitiesKHR capabilities;
        std::vector<VkSurfaceFormatKHR> formats;
        std::vector<VkPresentModeKHR> presentModes;
    };

    SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice pDevice)
    {
        SwapChainSupportDetails details;

        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(pDevice, mSurface, &details.capabilities);

        uint32_t formatCount;
        vkGetPhysicalDeviceSurfaceFormatsKHR(pDevice, mSurface, &formatCount, nullptr);
        if (formatCount != 0) {
            details.formats.resize(formatCount);
            vkGetPhysicalDeviceSurfaceFormatsKHR(pDevice, mSurface, &formatCount, details.formats.data());
        }

        uint32_t presentModeCount;
        vkGetPhysicalDeviceSurfacePresentModesKHR(pDevice, mSurface, &presentModeCount, nullptr);
        if (presentModeCount != 0) {
            details.presentModes.resize(presentModeCount);
            vkGetPhysicalDeviceSurfacePresentModesKHR(pDevice, mSurface, &presentModeCount, details.presentModes.data());
        }

        return details;
    }

    VkSurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& pAvailableFormats)
    {
        for (const auto& availableFormat : pAvailableFormats) {
            if (availableFormat.format == VK_FORMAT_B8G8R8A8_SRGB && availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                // Return ideal desired format.
                return availableFormat;
            }
        }

        // Return fallback format.
        return pAvailableFormats[0];
    }

    VkPresentModeKHR chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& pAvailablePresentModes)
    {
        for (const auto& availablePresentMode : pAvailablePresentModes) {
            if (availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR) {
                std::cout << "Using mailbox present mode as it's available.\n";
                return availablePresentMode;
            }
        }

        std::cout << "Using FIFO present mode as mailbox is unavailable.\n";
        return VK_PRESENT_MODE_IMMEDIATE_KHR;
    }

    VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR& pCapabilities)
    {
        if (pCapabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
            return pCapabilities.currentExtent;
        } else {
            int width, height;
            glfwGetFramebufferSize(mWindow, &width, &height);

            VkExtent2D actualExtent = { static_cast<uint32_t>(width), static_cast<uint32_t>(height) };
            actualExtent.width = std::clamp(actualExtent.width, pCapabilities.minImageExtent.width, pCapabilities.maxImageExtent.width);
            actualExtent.height = std::clamp(actualExtent.height, pCapabilities.minImageExtent.height, pCapabilities.maxImageExtent.height);

            return actualExtent;
        }
    }

    void createLogicalDevice()
    {
        const QueueFamilyIndices indices = findQueueFamilies(mPhysicalDevice);

        std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
        const std::set<uint32_t> uniqueQueueFamilies = { indices.graphicsAndComputeFamily.value(), indices.presentFamily.value() };
        const float queuePriority = 1.0f;
        for (uint32_t queueFamily : uniqueQueueFamilies) {
            VkDeviceQueueCreateInfo queueCreateInfo {};
            queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            queueCreateInfo.queueFamilyIndex = queueFamily;
            queueCreateInfo.queueCount = 1;
            queueCreateInfo.pQueuePriorities = &queuePriority;
            queueCreateInfos.push_back(queueCreateInfo);
        }

        VkDeviceCreateInfo createInfo {};
        createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
        createInfo.pQueueCreateInfos = queueCreateInfos.data();
        VkPhysicalDeviceFeatures deviceFeatures {};
        deviceFeatures.samplerAnisotropy = VK_TRUE;
        createInfo.pEnabledFeatures = &deviceFeatures;

        createInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
        createInfo.ppEnabledExtensionNames = deviceExtensions.data();

        // Per-device validation layers are ignored on recent Vulkan implementations,
        // but we still need to do set them up for drivers only supporting older Vulkan versions.
        // <https://www.khronos.org/registry/vulkan/specs/1.3-extensions/html/chap40.html#extendingvulkan-layers-devicelayerdeprecation>
        if (enableValidationLayers) {
            createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
            createInfo.ppEnabledLayerNames = validationLayers.data();
        } else {
            createInfo.enabledLayerCount = 0;
        }

        if (vkCreateDevice(mPhysicalDevice, &createInfo, nullptr, &mDevice) != VK_SUCCESS) {
            throw std::runtime_error("Couldn't create logical Vulkan device.");
        }

        // We're only creating a single queue from these families, so use index 0.
        vkGetDeviceQueue(mDevice, indices.graphicsAndComputeFamily.value(), 0, &mGraphicsQueue);
        vkGetDeviceQueue(mDevice, indices.graphicsAndComputeFamily.value(), 0, &mComputeQueue);
        vkGetDeviceQueue(mDevice, indices.presentFamily.value(), 0, &mPresentQueue);
    }

    void createSwapChain()
    {
        const SwapChainSupportDetails swapChainSupport = querySwapChainSupport(mPhysicalDevice);

        // Simply sticking to the minimum `minImageCount` means that we may
        // sometimes have to wait on the driver to complete internal operations
        // before we can acquire another image to render to. Therefore, it is
        // recommended to request at least one more image than the minimum.
        uint32_t imageCount = swapChainSupport.capabilities.minImageCount + 1;

        // We should also make sure to not exceed the maximum number of images
        // while doing this, where 0 is a special value that means that there is
        // no maximum:
        if (swapChainSupport.capabilities.maxImageCount > 0 && imageCount > swapChainSupport.capabilities.maxImageCount) {
            imageCount = swapChainSupport.capabilities.maxImageCount;
        }

        VkSwapchainCreateInfoKHR createInfo {};
        createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        createInfo.surface = mSurface;
        createInfo.minImageCount = imageCount;

        const VkSurfaceFormatKHR swapChainFormat = chooseSwapSurfaceFormat(swapChainSupport.formats);
        mSwapChainImageFormat = swapChainFormat.format;
        createInfo.imageFormat = swapChainFormat.format;
        createInfo.imageColorSpace = swapChainFormat.colorSpace;

        mSwapChainExtent = chooseSwapExtent(swapChainSupport.capabilities);
        createInfo.imageExtent = mSwapChainExtent;

        createInfo.imageArrayLayers = 1;
        createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

        QueueFamilyIndices indices = findQueueFamilies(mPhysicalDevice);
        uint32_t queueFamilyIndices[] = { indices.graphicsAndComputeFamily.value(), indices.presentFamily.value() };

        if (indices.graphicsAndComputeFamily != indices.presentFamily) {
            std::cout << "Using concurrent sharing mode.\n";
            createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
            createInfo.queueFamilyIndexCount = 2;
            createInfo.pQueueFamilyIndices = queueFamilyIndices;
        } else {
            std::cout << "Using exclusive sharing mode.\n";
            createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        }

        createInfo.preTransform = swapChainSupport.capabilities.currentTransform;
        createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        const VkPresentModeKHR presentMode = chooseSwapPresentMode(swapChainSupport.presentModes);
        createInfo.presentMode = presentMode;
        createInfo.clipped = VK_TRUE;

        // With Vulkan, it's possible that your swap chain becomes invalid or
        // unoptimized while your application is running, for example because
        // the window was resized. In that case, the swap chain actually needs
        // to be recreated from scratch and a reference to the old one must be
        // specified in this field.
        // For now, we'll assume that we'll only ever create one swap chain.
        createInfo.oldSwapchain = VK_NULL_HANDLE;

        if (vkCreateSwapchainKHR(mDevice, &createInfo, nullptr, &mSwapChain) != VK_SUCCESS) {
            throw std::runtime_error("Couldn't create Vulkan swap chain.");
        }

        vkGetSwapchainImagesKHR(mDevice, mSwapChain, &imageCount, nullptr);
        mSwapChainImages.resize(imageCount);
        vkGetSwapchainImagesKHR(mDevice, mSwapChain, &imageCount, mSwapChainImages.data());
    }

    void cleanupSwapChain()
    {
        vkDestroyImageView(mDevice, mColorImageView, nullptr);
        vkDestroyImage(mDevice, mColorImage, nullptr);
        vkFreeMemory(mDevice, mColorImageMemory, nullptr);

        vkDestroyImageView(mDevice, mDepthImageView, nullptr);
        vkDestroyImage(mDevice, mDepthImage, nullptr);
        vkFreeMemory(mDevice, mDepthImageMemory, nullptr);

        for (auto framebuffer : mSwapChainFramebuffers) {
            vkDestroyFramebuffer(mDevice, framebuffer, nullptr);
        }

        // Swapchain images are automatically cleaned up, but not the image views.
        for (auto imageView : mSwapChainImageViews) {
            vkDestroyImageView(mDevice, imageView, nullptr);
        }

        vkDestroySwapchainKHR(mDevice, mSwapChain, nullptr);
    }

    void recreateSwapChain()
    {
        int width = 0, height = 0;
        glfwGetFramebufferSize(mWindow, &width, &height);
        while (width == 0 || height == 0) {
            // Wait if window is minimized.
            glfwGetFramebufferSize(mWindow, &width, &height);
            glfwWaitEvents();
        }

        vkDeviceWaitIdle(mDevice);

        cleanupSwapChain();

        createSwapChain();
        createImageViews();
        createColorResources();
        createDepthResources();
        createFramebuffers();
    }

    void createImageViews()
    {
        mSwapChainImageViews.resize(mSwapChainImages.size());

        for (size_t i = 0; i < mSwapChainImages.size(); i += 1) {
            mSwapChainImageViews[i] = createImageView(mSwapChainImages[i], mSwapChainImageFormat, VK_IMAGE_ASPECT_COLOR_BIT, 1);
        }
    }

    VkShaderModule createShaderModule(const std::vector<char>& pCode)
    {
        VkShaderModuleCreateInfo createInfo {};
        createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        createInfo.codeSize = pCode.size();
        createInfo.pCode = reinterpret_cast<const uint32_t*>(pCode.data());

        VkShaderModule shaderModule;
        if (vkCreateShaderModule(mDevice, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
            throw std::runtime_error("Couldn't create Vulkan shader module.");
        }

        return shaderModule;
    }

    void createShaderStorageBuffers()
    {
        mShaderStorageBuffers.resize(MAX_FRAMES_IN_FLIGHT);
        mShaderStorageBuffersMemory.resize(MAX_FRAMES_IN_FLIGHT);

        // Initialize particles.
        std::default_random_engine rndEngine((unsigned)time(nullptr));
        std::uniform_real_distribution<float> rndDist(0.0f, 1.0f);

        // Initialize particle positions on a circle.
        std::vector<Particle> particles(PARTICLE_COUNT);
        for (auto& particle : particles) {
            const float r = 0.25f * sqrt(rndDist(rndEngine));
            const float theta = rndDist(rndEngine) * 2 * PI;
            const float x = r * cos(theta) * WINDOW_HEIGHT / WINDOW_WIDTH;
            const float y = r * sin(theta);
            particle.position = glm::vec2(x, y);
            particle.velocity = glm::normalize(glm::vec2(x, y)) * 0.00025f;
            particle.color = glm::vec4(rndDist(rndEngine), rndDist(rndEngine), rndDist(rndEngine), 1.0);
        }

        VkDeviceSize bufferSize = sizeof(Particle) * PARTICLE_COUNT;
        VkBuffer stagingBuffer;
        VkDeviceMemory stagingBufferMemory;
        createBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer, stagingBufferMemory);

        void* data;
        vkMapMemory(mDevice, stagingBufferMemory, 0, bufferSize, 0, &data);
        memcpy(data, particles.data(), (size_t)bufferSize);
        vkUnmapMemory(mDevice, stagingBufferMemory);

        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i += 1) {
            createBuffer(bufferSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, mShaderStorageBuffers[i], mShaderStorageBuffersMemory[i]);
            // Copy data from the staging buffer (host) to the shader storage buffer (GPU).
            copyBuffer(stagingBuffer, mShaderStorageBuffers[i], bufferSize);
        }
    }

    void createRenderPass()
    {
        VkAttachmentDescription colorAttachment {};
        colorAttachment.format = mSwapChainImageFormat;
        colorAttachment.samples = mMsaaSamples;
        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        colorAttachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkAttachmentReference colorAttachmentRef {};
        colorAttachmentRef.attachment = 0;
        colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkAttachmentDescription depthAttachment {};
        depthAttachment.format = findDepthFormat();
        depthAttachment.samples = mMsaaSamples;
        depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkAttachmentReference depthAttachmentRef {};
        depthAttachmentRef.attachment = 1;
        depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkAttachmentDescription colorAttachmentResolve {};
        colorAttachmentResolve.format = mSwapChainImageFormat;
        colorAttachmentResolve.samples = VK_SAMPLE_COUNT_1_BIT;
        colorAttachmentResolve.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAttachmentResolve.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachmentResolve.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAttachmentResolve.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        colorAttachmentResolve.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        colorAttachmentResolve.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentReference colorAttachmentResolveRef {};
        colorAttachmentResolveRef.attachment = 2;
        colorAttachmentResolveRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass {};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        // The index of the attachment in this array is directly referenced from the fragment shader with the
        // `layout(location = 0) out vec4 outColor` directive.
        subpass.pColorAttachments = &colorAttachmentRef;
        subpass.pDepthStencilAttachment = &depthAttachmentRef;
        subpass.pResolveAttachments = &colorAttachmentResolveRef;

        std::array<VkAttachmentDescription, 3> attachments = { colorAttachment, depthAttachment, colorAttachmentResolve };
        VkRenderPassCreateInfo renderPassInfo {};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        renderPassInfo.pAttachments = attachments.data();
        renderPassInfo.subpassCount = 1;
        renderPassInfo.pSubpasses = &subpass;

        VkSubpassDependency dependency {};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        // The depth image is first accessed in the early fragment test pipeline
        // stage and because we have a load operation that clears, we should
        // specify the access mask for writes.
        dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependency.srcAccessMask = 0;
        dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        renderPassInfo.dependencyCount = 1;
        renderPassInfo.pDependencies = &dependency;

        if (vkCreateRenderPass(mDevice, &renderPassInfo, nullptr, &mRenderPass) != VK_SUCCESS) {
            throw std::runtime_error("Couldn't create Vulkan render pass.");
        }
    }

    void createDescriptorSetLayout()
    {
        VkDescriptorSetLayoutBinding uboLayoutBinding {};
        uboLayoutBinding.binding = 0;
        uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        uboLayoutBinding.descriptorCount = 1;
        uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        uboLayoutBinding.pImmutableSamplers = nullptr; // Optional (only relevant for image sampling-related descriptors)

        VkDescriptorSetLayoutBinding samplerLayoutBinding {};
        samplerLayoutBinding.binding = 1;
        samplerLayoutBinding.descriptorCount = 1;
        samplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        samplerLayoutBinding.pImmutableSamplers = nullptr;
        // We'll only use the texture in the fragment shader.
        samplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        std::array<VkDescriptorSetLayoutBinding, 2> bindings = { uboLayoutBinding, samplerLayoutBinding };
        VkDescriptorSetLayoutCreateInfo layoutInfo {};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();

        if (vkCreateDescriptorSetLayout(mDevice, &layoutInfo, nullptr, &mDescriptorSetLayout) != VK_SUCCESS) {
            throw std::runtime_error("Couldn't create Vulkan graphics descriptor set layout.");
        }

        const std::array<VkDescriptorSetLayoutBinding, 3> layoutBindings = { {
            {
                .binding = 0,
                .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
                .pImmutableSamplers = nullptr,
            },
            {
                .binding = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
                .pImmutableSamplers = nullptr,
            },
            // We need two SSBOs, as particle positions are updated
            // frame-by-frame based on delta time. This means that each frame
            // needs to know about the last frames' particle positions, so it
            // can update them with a new delta time and write them to its own
            // SSBO.
            {
                .binding = 2,
                .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
                .pImmutableSamplers = nullptr,
            },
        } };

        const VkDescriptorSetLayoutCreateInfo computeLayoutInfo = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .pNext = nullptr,
            .flags = {},
            .bindingCount = 3,
            .pBindings = layoutBindings.data(),
        };

        if (vkCreateDescriptorSetLayout(mDevice, &computeLayoutInfo, nullptr, &mComputeDescriptorSetLayout) != VK_SUCCESS) {
            throw std::runtime_error("Couldn't create Vulkan compute descriptor set layout.");
        }
    }

    void createGraphicsPipeline()
    {
        auto vertShaderCode = readFile("build/shader.vert.spv");
        auto fragShaderCode = readFile("build/shader.frag.spv");
        auto computeShaderCode = readFile("build/compute.spv");

        VkShaderModule vertShaderModule = createShaderModule(vertShaderCode);
        VkShaderModule fragShaderModule = createShaderModule(fragShaderCode);
        VkShaderModule computeShaderModule = createShaderModule(computeShaderCode);

        VkPipelineShaderStageCreateInfo vertShaderStageInfo {};
        vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
        vertShaderStageInfo.module = vertShaderModule;
        vertShaderStageInfo.pName = "main";

        VkPipelineShaderStageCreateInfo fragShaderStageInfo {};
        fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        fragShaderStageInfo.module = fragShaderModule;
        fragShaderStageInfo.pName = "main";

        VkPipelineShaderStageCreateInfo computeShaderStageInfo {};
        computeShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        computeShaderStageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        computeShaderStageInfo.module = computeShaderModule;
        computeShaderStageInfo.pName = "main";

        VkPipelineShaderStageCreateInfo shaderStages[] = { vertShaderStageInfo, fragShaderStageInfo, computeShaderStageInfo };

        VkPipelineVertexInputStateCreateInfo vertexInputInfo {};
        vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

        auto bindingDescription = Vertex::getBindingDescription();
        vertexInputInfo.vertexBindingDescriptionCount = 1;
        vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;

        auto attributeDescriptions = Vertex::getAttributeDescriptions();
        vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
        vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

        VkPipelineInputAssemblyStateCreateInfo inputAssembly {};
        inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        inputAssembly.primitiveRestartEnable = VK_FALSE;

        VkViewport viewport {};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = (float)mSwapChainExtent.width;
        viewport.height = (float)mSwapChainExtent.height;
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;

        VkRect2D scissor {};
        scissor.offset = { 0, 0 };
        scissor.extent = mSwapChainExtent;

        std::vector<VkDynamicState> dynamicStates = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR,
        };

        VkPipelineDynamicStateCreateInfo dynamicState {};
        dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
        dynamicState.pDynamicStates = dynamicStates.data();

        VkPipelineViewportStateCreateInfo viewportState {};
        viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.pViewports = &viewport;
        viewportState.scissorCount = 1;
        viewportState.pScissors = &scissor;

        VkPipelineRasterizationStateCreateInfo rasterizer {};
        rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.depthClampEnable = VK_FALSE;
        rasterizer.rasterizerDiscardEnable = VK_FALSE;
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.lineWidth = 1.0f;
        rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
        rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE; // Account for flipped Y viewport.
        rasterizer.depthBiasEnable = VK_FALSE;

        VkPipelineMultisampleStateCreateInfo multisampling {};
        multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.sampleShadingEnable = VK_FALSE;
        multisampling.rasterizationSamples = mMsaaSamples;

        VkPipelineDepthStencilStateCreateInfo depthStencil {};
        depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencil.depthTestEnable = VK_TRUE;
        depthStencil.depthWriteEnable = VK_TRUE;
        depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
        depthStencil.depthBoundsTestEnable = VK_FALSE;
        depthStencil.minDepthBounds = 0.0f; // Optional
        depthStencil.maxDepthBounds = 1.0f; // Optional
        depthStencil.stencilTestEnable = VK_FALSE;
        depthStencil.front = {}; // Optional
        depthStencil.back = {}; // Optional

        VkPipelineColorBlendAttachmentState colorBlendAttachment {};
        colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        colorBlendAttachment.blendEnable = VK_FALSE;

        VkPipelineColorBlendStateCreateInfo colorBlending {};
        colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlending.logicOpEnable = VK_FALSE;
        colorBlending.attachmentCount = 1;
        colorBlending.pAttachments = &colorBlendAttachment;

        VkPipelineLayoutCreateInfo pipelineLayoutInfo {};
        pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutInfo.setLayoutCount = 1;
        pipelineLayoutInfo.pSetLayouts = &mDescriptorSetLayout;

        if (vkCreatePipelineLayout(mDevice, &pipelineLayoutInfo, nullptr, &mPipelineLayout) != VK_SUCCESS) {
            throw std::runtime_error("Couldn't create Vulkan pipeline layout.");
        }

        VkPipelineLayoutCreateInfo computePipelineLayoutInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .pNext = nullptr,
            .flags = {},
            .setLayoutCount = 1,
            .pSetLayouts = &mComputeDescriptorSetLayout,
            .pushConstantRangeCount = {},
            .pPushConstantRanges = nullptr,
        };

        if (vkCreatePipelineLayout(mDevice, &computePipelineLayoutInfo, nullptr, &mComputePipelineLayout) != VK_SUCCESS) {
            throw std::runtime_error("Couldn't create Vulkan compute pipeline layout.");
        }

        VkGraphicsPipelineCreateInfo pipelineInfo {};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.stageCount = 2;
        pipelineInfo.pStages = shaderStages;
        pipelineInfo.pVertexInputState = &vertexInputInfo;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.pDynamicState = &dynamicState;

        pipelineInfo.layout = mPipelineLayout;
        pipelineInfo.renderPass = mRenderPass;
        pipelineInfo.subpass = 0;

        pipelineInfo.basePipelineHandle = VK_NULL_HANDLE; // Optional
        pipelineInfo.basePipelineIndex = -1; // Optional

        if (vkCreateGraphicsPipelines(mDevice, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &mGraphicsPipeline) != VK_SUCCESS) {
            throw std::runtime_error("Couldn't create Vulkan graphics pipeline.");
        }

        const VkComputePipelineCreateInfo computePipelineInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .pNext = nullptr,
            .flags = {},
            .stage = computeShaderStageInfo,
            .layout = mComputePipelineLayout,
            .basePipelineHandle = {},
            .basePipelineIndex = {},
        };

        if (vkCreateComputePipelines(mDevice, VK_NULL_HANDLE, 1, &computePipelineInfo, nullptr, &mComputePipeline) != VK_SUCCESS) {
            throw std::runtime_error("Couldn't create Vulkan compute pipeline.");
        }

        // Shader modules don't need to exist after the pipeline has been compiled,
        // so we can destroy them now.
        vkDestroyShaderModule(mDevice, fragShaderModule, nullptr);
        vkDestroyShaderModule(mDevice, vertShaderModule, nullptr);
    }

    void createFramebuffers()
    {
        mSwapChainFramebuffers.resize(mSwapChainImageViews.size());

        for (size_t i = 0; i < mSwapChainImageViews.size(); i += 1) {
            // The color resolve attachment differs for every swap chain image, but the
            // same color and depth images can be used by all of them because only a single
            // subpass is running at the same time thanks to our semaphores.
            std::array<VkImageView, 3> attachments = {
                mColorImageView,
                mDepthImageView,
                mSwapChainImageViews[i],
            };

            VkFramebufferCreateInfo framebufferInfo {};
            framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            framebufferInfo.renderPass = mRenderPass;
            framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
            framebufferInfo.pAttachments = attachments.data();
            framebufferInfo.width = mSwapChainExtent.width;
            framebufferInfo.height = mSwapChainExtent.height;
            framebufferInfo.layers = 1;

            if (vkCreateFramebuffer(mDevice, &framebufferInfo, nullptr, &mSwapChainFramebuffers[i]) != VK_SUCCESS) {
                throw std::runtime_error("Couldn't create Vulkan framebuffer.");
            }
        }
    }

    void createCommandPool()
    {
        const QueueFamilyIndices queueFamilyIndices = findQueueFamilies(mPhysicalDevice);
        VkCommandPoolCreateInfo poolInfo {};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        poolInfo.queueFamilyIndex = queueFamilyIndices.graphicsAndComputeFamily.value();

        if (vkCreateCommandPool(mDevice, &poolInfo, nullptr, &mCommandPool) != VK_SUCCESS) {
            throw std::runtime_error("Couldn't create Vulkan command pool.");
        }
    }

    /**
     * Returns the first format supported by the hardware in the list of
     * candidates for the specified image tiling and format features.
     */
    VkFormat findSupportedFormat(const std::vector<VkFormat>& pCandidates, VkImageTiling pTiling, VkFormatFeatureFlags pFeatures)
    {
        for (VkFormat format : pCandidates) {
            VkFormatProperties properties;
            vkGetPhysicalDeviceFormatProperties(mPhysicalDevice, format, &properties);

            if (pTiling == VK_IMAGE_TILING_LINEAR && (properties.linearTilingFeatures & pFeatures)) {
                return format;
            } else if (pTiling == VK_IMAGE_TILING_OPTIMAL && (properties.optimalTilingFeatures & pFeatures)) {
                return format;
            }
        }

        throw std::runtime_error("Couldn't find supported Vulkan format.");
    }

    VkFormat findDepthFormat()
    {
        return findSupportedFormat(
            { VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT },
            VK_IMAGE_TILING_OPTIMAL,
            VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
    }

    bool hasStencilComponent(VkFormat pFormat)
    {
        return pFormat == VK_FORMAT_D32_SFLOAT_S8_UINT || pFormat == VK_FORMAT_D24_UNORM_S8_UINT;
    }

    void createColorResources()
    {
        VkFormat colorFormat = mSwapChainImageFormat;
        createImage(
            mSwapChainExtent.width,
            mSwapChainExtent.height,
            1,
            mMsaaSamples,
            colorFormat,
            VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            mColorImage, mColorImageMemory);

        mColorImageView = createImageView(mColorImage, colorFormat, VK_IMAGE_ASPECT_COLOR_BIT, 1);
    }

    void createDepthResources()
    {
        const VkFormat depthFormat = findDepthFormat();
        createImage(
            mSwapChainExtent.width,
            mSwapChainExtent.height,
            1,
            mMsaaSamples,
            depthFormat,
            VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            mDepthImage,
            mDepthImageMemory);

        mDepthImageView = createImageView(mDepthImage, depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT, 1);
        // That's it for creating the depth image. We don't need to map it or
        // copy another image to it, because we're going to clear it at the
        // start of the render pass like the color attachment.
    }

    // Return the highest supported MSAA level.
    VkSampleCountFlagBits getMaxUsableSampleCount()
    {
        VkPhysicalDeviceProperties physicalDeviceProperties;
        vkGetPhysicalDeviceProperties(mPhysicalDevice, &physicalDeviceProperties);

        VkSampleCountFlags counts = physicalDeviceProperties.limits.framebufferColorSampleCounts & physicalDeviceProperties.limits.framebufferDepthSampleCounts;

        if (counts & VK_SAMPLE_COUNT_64_BIT) {
            return VK_SAMPLE_COUNT_64_BIT;
        } else if (counts & VK_SAMPLE_COUNT_32_BIT) {
            return VK_SAMPLE_COUNT_32_BIT;
        } else if (counts & VK_SAMPLE_COUNT_16_BIT) {
            return VK_SAMPLE_COUNT_16_BIT;
        } else if (counts & VK_SAMPLE_COUNT_8_BIT) {
            return VK_SAMPLE_COUNT_8_BIT;
        } else if (counts & VK_SAMPLE_COUNT_4_BIT) {
            return VK_SAMPLE_COUNT_4_BIT;
        } else if (counts & VK_SAMPLE_COUNT_2_BIT) {
            return VK_SAMPLE_COUNT_2_BIT;
        }

        return VK_SAMPLE_COUNT_1_BIT;
    }

    void createImage(
        uint32_t pWidth,
        uint32_t pHeight,
        uint32_t pMipLevels,
        VkSampleCountFlagBits pNumSamples,
        VkFormat pFormat,
        VkImageTiling pTiling,
        VkImageUsageFlags pUsage,
        VkMemoryPropertyFlags pProperties,
        VkImage& pImage,
        VkDeviceMemory& pImageMemory)
    {
        VkImageCreateInfo imageInfo {};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent.width = pWidth;
        imageInfo.extent.height = pHeight;
        imageInfo.extent.depth = 1;
        imageInfo.mipLevels = pMipLevels;
        imageInfo.arrayLayers = 1;
        imageInfo.format = pFormat;
        imageInfo.tiling = pTiling;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = pUsage;
        imageInfo.samples = pNumSamples;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (vkCreateImage(mDevice, &imageInfo, nullptr, &pImage) != VK_SUCCESS) {
            throw std::runtime_error("Couldn't create Vulkan image.");
        }

        VkMemoryRequirements memRequirements;
        vkGetImageMemoryRequirements(mDevice, pImage, &memRequirements);

        VkMemoryAllocateInfo allocInfo {};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memRequirements.size;
        allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, pProperties);

        if (vkAllocateMemory(mDevice, &allocInfo, nullptr, &pImageMemory) != VK_SUCCESS) {
            throw std::runtime_error("Couldn't allocate Vulkan image memory.");
        }

        vkBindImageMemory(mDevice, pImage, pImageMemory, 0);
    }

    void generateMipmaps(VkImage pImage, VkFormat pImageFormat, int32_t pTexWidth, int32_t pTexHeight, uint32_t pMipLevels)
    {
        // Check if image format supports linear blitting.
        VkFormatProperties formatProperties;
        vkGetPhysicalDeviceFormatProperties(mPhysicalDevice, pImageFormat, &formatProperties);
        if (!(formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)) {
            throw std::runtime_error("Vulkan texture image format doesn't support linear blitting.");
        }

        VkCommandBuffer commandBuffer = beginSingleTimeCommands();

        VkImageMemoryBarrier barrier {};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.image = pImage;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;
        barrier.subresourceRange.levelCount = 1;

        int32_t mipWidth = pTexWidth;
        int32_t mipHeight = pTexHeight;

        for (uint32_t i = 1; i < pMipLevels; i += 1) {
            // First, we transition level i - 1 to `VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL`.
            // This transition will wait for level i - 1 to be filled, either
            // from the previous blit command, or from `vkCmdCopyBufferToImage()`.
            // The current blit command will wait on this transition.
            barrier.subresourceRange.baseMipLevel = i - 1;
            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

            vkCmdPipelineBarrier(
                commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

            VkImageBlit blit {};
            blit.srcOffsets[0] = { 0, 0, 0 };
            blit.srcOffsets[1] = { mipWidth, mipHeight, 1 };
            blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            blit.srcSubresource.mipLevel = i - 1;
            blit.srcSubresource.baseArrayLayer = 0;
            blit.srcSubresource.layerCount = 1;
            blit.dstOffsets[0] = { 0, 0, 0 };
            blit.dstOffsets[1] = {
                mipWidth > 1 ? mipWidth / 2 : 1,
                mipHeight > 1 ? mipHeight / 2 : 1,
                1,
            };
            blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            blit.dstSubresource.mipLevel = i;
            blit.dstSubresource.baseArrayLayer = 0;
            blit.dstSubresource.layerCount = 1;

            vkCmdBlitImage(
                commandBuffer, pImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, pImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);

            // This barrier transitions mip level i - 1 to `VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL`.
            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

            vkCmdPipelineBarrier(
                commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

            if (mipWidth > 1) {
                mipWidth /= 2;
            }

            if (mipHeight > 1) {
                mipHeight /= 2;
            }
        }

        // This barrier transitions the last mip level from
        // `VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL` to
        // `VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL`. This wasn't handled by
        // the loop, since the last mip level is never blitted from.
        barrier.subresourceRange.baseMipLevel = pMipLevels - 1;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(
            commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

        endSingleTimeCommands(commandBuffer);
    }

    void createTextureImage()
    {
        int texWidth, texHeight, texChannels;
        stbi_uc* pixels = stbi_load(TEXTURE_PATH, &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
        VkDeviceSize imageSize = texWidth * texHeight * 4;

        // The max function selects the largest dimension. The log2 function
        // calculates how many times that dimension can be divided by 2. The
        // floor function handles cases where the largest dimension is not a
        // power of 2. 1 is added so that the original image has a mip level.
        mMipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(texWidth, texHeight)))) + 1;

        if (!pixels) {
            throw std::runtime_error("Couldn't load image.");
        }

        VkBuffer stagingBuffer;
        VkDeviceMemory stagingBufferMemory;
        createBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer, stagingBufferMemory);

        void* data;
        vkMapMemory(mDevice, stagingBufferMemory, 0, imageSize, 0, &data);
        memcpy(data, pixels, static_cast<size_t>(imageSize));
        vkUnmapMemory(mDevice, stagingBufferMemory);
        stbi_image_free(pixels);

        // Mipmap generation requires `VK_IMAGE_USAGE_TRANSFER_SRC_BIT`.
        createImage(texWidth, texHeight, mMipLevels, VK_SAMPLE_COUNT_1_BIT, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_MEMORY_HEAP_DEVICE_LOCAL_BIT, mTextureImage, mTextureImageMemory);

        transitionImageLayout(mTextureImage, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, mMipLevels);

        copyBufferToImage(stagingBuffer, mTextureImage, static_cast<uint32_t>(texWidth), static_cast<uint32_t>(texHeight));

        // Image is transitioned to `VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL` while generating mipmaps.

        vkDestroyBuffer(mDevice, stagingBuffer, nullptr);
        vkFreeMemory(mDevice, stagingBufferMemory, nullptr);

        generateMipmaps(mTextureImage, VK_FORMAT_R8G8B8A8_SRGB, texWidth, texHeight, mMipLevels);
    }

    VkImageView createImageView(VkImage pImage, VkFormat pFormat, VkImageAspectFlags pAspectFlags, uint32_t pMipLevels)
    {
        VkImageViewCreateInfo viewInfo {};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = pImage;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = pFormat;

        viewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;

        viewInfo.subresourceRange.aspectMask = pAspectFlags;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = pMipLevels;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        VkImageView imageView;
        if (vkCreateImageView(mDevice, &viewInfo, nullptr, &imageView) != VK_SUCCESS) {
            throw std::runtime_error("Couldn't create Vulkan image view.");
        }

        return imageView;
    }

    void createTextureImageView()
    {
        mTextureImageView = createImageView(mTextureImage, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_ASPECT_COLOR_BIT, mMipLevels);
    }

    void createTextureSampler()
    {
        VkSamplerCreateInfo samplerInfo {};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.anisotropyEnable = VK_TRUE;
        VkPhysicalDeviceProperties properties {};
        vkGetPhysicalDeviceProperties(mPhysicalDevice, &properties);
        samplerInfo.maxAnisotropy = properties.limits.maxSamplerAnisotropy;
        samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
        samplerInfo.unnormalizedCoordinates = VK_FALSE;
        samplerInfo.compareEnable = VK_FALSE;
        samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        samplerInfo.minLod = 0.0f;
        samplerInfo.maxLod = VK_LOD_CLAMP_NONE;
        samplerInfo.mipLodBias = 0.0f;

        if (vkCreateSampler(mDevice, &samplerInfo, nullptr, &mTextureSampler) != VK_SUCCESS) {
            throw std::runtime_error("Couldn't create Vulkan texture sampler.");
        }
    }

    VkCommandBuffer beginSingleTimeCommands()
    {
        VkCommandBufferAllocateInfo allocInfo {};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandPool = mCommandPool;
        allocInfo.commandBufferCount = 1;

        VkCommandBuffer commandBuffer;
        vkAllocateCommandBuffers(mDevice, &allocInfo, &commandBuffer);

        VkCommandBufferBeginInfo beginInfo {};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        vkBeginCommandBuffer(commandBuffer, &beginInfo);

        return commandBuffer;
    }

    void endSingleTimeCommands(VkCommandBuffer pCommandBuffer)
    {
        vkEndCommandBuffer(pCommandBuffer);

        VkSubmitInfo submitInfo {};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &pCommandBuffer;

        vkQueueSubmit(mGraphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);

        // All of the helper functions that submit commands so far have been set
        // up to execute synchronously by waiting for the queue to become idle.
        // For practical applications, it is recommended to combine these
        // operations in a single command buffer and execute them asynchronously
        // for higher throughput, especially the transitions and copy in the
        // createTextureImage() function.
        //
        // Try to experiment with this by creating a setupCommandBuffer that the
        // helper functions record commands into, and add a flushSetupCommands
        // to execute the commands that have been recorded so far. It's best to
        // do this after the texture mapping works to check if the texture
        // resources are still set up correctly.
        vkQueueWaitIdle(mGraphicsQueue);

        vkFreeCommandBuffers(mDevice, mCommandPool, 1, &pCommandBuffer);
    }

    void copyBufferToImage(VkBuffer pBuffer, VkImage pImage, uint32_t pWidth, uint32_t pHeight)
    {
        VkCommandBuffer commandBuffer = beginSingleTimeCommands();

        VkBufferImageCopy region {};
        region.bufferOffset = 0;
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;

        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount = 1;

        region.imageOffset = { 0, 0, 0 };
        region.imageExtent = { pWidth, pHeight, 1 };

        // This assumes the image has been already transitioned to the layout
        // that is optimal for copying pixels to.
        vkCmdCopyBufferToImage(commandBuffer, pBuffer, pImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        endSingleTimeCommands(commandBuffer);
    }

    void transitionImageLayout(VkImage pImage, VkFormat pFormat, VkImageLayout pOldLayout, VkImageLayout pNewLayout, uint32_t pMipLevels)
    {
        VkCommandBuffer commandBuffer = beginSingleTimeCommands();

        VkImageMemoryBarrier barrier {};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        // It is possible to use VK_IMAGE_LAYOUT_UNDEFINED as oldLayout if you
        // don't care about the existing contents of the image.
        barrier.oldLayout = pOldLayout;
        barrier.newLayout = pNewLayout;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

        barrier.image = pImage;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = pMipLevels;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;

        VkPipelineStageFlags sourceStage;
        VkPipelineStageFlags destinationStage;

        if (pOldLayout == VK_IMAGE_LAYOUT_UNDEFINED && pNewLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        } else if (pOldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && pNewLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        } else {
            throw std::invalid_argument("Unsupported Vulkan layout transition.");
        }

        vkCmdPipelineBarrier(
            commandBuffer,
            sourceStage,
            destinationStage,
            0,
            0,
            nullptr,
            0,
            nullptr,
            1,
            &barrier);

        endSingleTimeCommands(commandBuffer);
    }

    // Return the index of the suitable memory type for the given properties bitmsk.
    uint32_t findMemoryType(uint32_t pTypeFilter, VkMemoryPropertyFlags pProperties)
    {
        VkPhysicalDeviceMemoryProperties memProperties;
        vkGetPhysicalDeviceMemoryProperties(mPhysicalDevice, &memProperties);

        for (uint32_t i = 0; i < memProperties.memoryTypeCount; i += 1) {
            if (pTypeFilter & (1 << i) && (memProperties.memoryTypes[i].propertyFlags & pProperties) == pProperties) {
                return i;
            }
        }

        throw std::runtime_error("Couldn't find suitable Vulkan memory type.");
    }

    void createBuffer(VkDeviceSize pSize, VkBufferUsageFlags pUsage, VkMemoryPropertyFlags pProperties, VkBuffer& pBuffer, VkDeviceMemory& pBufferMemory)
    {
        VkBufferCreateInfo bufferInfo {};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = pSize;
        bufferInfo.usage = pUsage;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (vkCreateBuffer(mDevice, &bufferInfo, nullptr, &pBuffer) != VK_SUCCESS) {
            throw std::runtime_error("Couldn't create Vulkan buffer.");
        }

        VkMemoryRequirements memRequirements;
        vkGetBufferMemoryRequirements(mDevice, pBuffer, &memRequirements);

        VkMemoryAllocateInfo allocInfo {};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memRequirements.size;
        allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

        // It should be noted that in a real world application, you're not
        // supposed to actually call vkAllocateMemory for every individual
        // buffer.
        // The right way to allocate memory for a large number of
        // objects at the same time is to create a custom allocator that splits
        // up a single allocation among many different objects by using the
        // offset parameters that we've seen in many functions.
        if (vkAllocateMemory(mDevice, &allocInfo, nullptr, &pBufferMemory) != VK_SUCCESS) {
            throw std::runtime_error("Couldn't allocate Vulkan vertex buffer memory.");
        }

        vkBindBufferMemory(mDevice, pBuffer, pBufferMemory, 0);
    }

    void copyBuffer(VkBuffer pSrcBuffer, VkBuffer pDstBuffer, VkDeviceSize pSize)
    {
        VkCommandBuffer commandBuffer = beginSingleTimeCommands();

        VkBufferCopy copyRegion {};
        copyRegion.srcOffset = 0; // Optional
        copyRegion.dstOffset = 0; // Optional
        copyRegion.size = pSize;
        vkCmdCopyBuffer(commandBuffer, pSrcBuffer, pDstBuffer, 1, &copyRegion);

        endSingleTimeCommands(commandBuffer);
    }

    void loadModel()
    {
        tinyobj::attrib_t attrib;
        std::vector<tinyobj::shape_t> shapes;
        std::vector<tinyobj::material_t> materials;
        std::string warning, error;

        if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warning, &error, MODEL_PATH)) {
            throw std::runtime_error(warning + error);
        }

        // Key is the vertex data, value is the index.
        std::unordered_map<Vertex, uint32_t> uniqueVertices {};

        // tinyobj automatically triangulates meshes on load by default.
        for (const auto& shape : shapes) {
            for (const auto& index : shape.mesh.indices) {
                Vertex vertex = {
                    .position = {
                        attrib.vertices[3 * index.vertex_index + 0],
                        attrib.vertices[3 * index.vertex_index + 1],
                        attrib.vertices[3 * index.vertex_index + 2],
                    },
                    .color = { 1.0f, 1.0f, 1.0f },
                    .texCoord = {
                        attrib.texcoords[2 * index.texcoord_index + 0],
                        // In OBJ, 0.0 is the bottom of the image.
                        // Flip to match Vulkan coordinate system where 0.0 is the top.
                        1.0f - attrib.texcoords[2 * index.texcoord_index + 1],
                    },
                };

                // Every time we read a vertex from the OBJ file, we check if we've already
                // seen a vertex with the exact same position and texture coordinates.
                if (uniqueVertices.count(vertex) == 0) {
                    // Vertex wasn't added yet, so add it.
                    uniqueVertices[vertex] = static_cast<uint32_t>(vertices.size());
                    vertices.push_back(vertex);
                }

                indices.push_back(uniqueVertices[vertex]);
            }
        }
    }

    void createVertexBuffer()
    {
        VkDeviceSize bufferSize = sizeof(vertices[0]) * vertices.size();

        // Use staging memory to improve performance, then transfer it from the CPU to the GPU.
        VkBuffer stagingBuffer;
        VkDeviceMemory stagingBufferMemory;
        createBuffer(bufferSize,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            stagingBuffer,
            stagingBufferMemory);

        void* data;
        vkMapMemory(mDevice, stagingBufferMemory, 0, bufferSize, 0, &data);
        memcpy(data, vertices.data(), (size_t)bufferSize);
        vkUnmapMemory(mDevice, stagingBufferMemory);

        createBuffer(
            bufferSize,
            VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            mVertexBuffer,
            mVertexBufferMemory);

        // `mVertexBuffer` is device-local, so we can't use `vkMapMemory()`.
        copyBuffer(stagingBuffer, mVertexBuffer, bufferSize);

        vkDestroyBuffer(mDevice, stagingBuffer, nullptr);
        vkFreeMemory(mDevice, stagingBufferMemory, nullptr);
    }

    void createIndexBuffer()
    {
        VkDeviceSize bufferSize = sizeof(indices[0]) * indices.size();

        // Use staging memory to improve performance, then transfer it from the CPU to the GPU.
        VkBuffer stagingBuffer;
        VkDeviceMemory stagingBufferMemory;
        createBuffer(bufferSize,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            stagingBuffer,
            stagingBufferMemory);

        void* data;
        vkMapMemory(mDevice, stagingBufferMemory, 0, bufferSize, 0, &data);
        memcpy(data, indices.data(), (size_t)bufferSize);
        vkUnmapMemory(mDevice, stagingBufferMemory);

        createBuffer(
            bufferSize,
            VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            mIndexBuffer,
            mIndexBufferMemory);

        // `mIndexBuffer` is device-local, so we can't use `vkMapMemory()`.
        copyBuffer(stagingBuffer, mIndexBuffer, bufferSize);

        vkDestroyBuffer(mDevice, stagingBuffer, nullptr);
        vkFreeMemory(mDevice, stagingBufferMemory, nullptr);
    }

    void createUniformBuffers()
    {
        VkDeviceSize bufferSize = sizeof(UniformBufferObject);

        mUniformBuffers.resize(MAX_FRAMES_IN_FLIGHT);
        mUniformBuffersMemory.resize(MAX_FRAMES_IN_FLIGHT);
        mUniformBuffersMapped.resize(MAX_FRAMES_IN_FLIGHT);

        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i += 1) {
            createBuffer(bufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, mUniformBuffers[i], mUniformBuffersMemory[i]);
            vkMapMemory(mDevice, mUniformBuffersMemory[i], 0, bufferSize, 0, &mUniformBuffersMapped[i]);
        }
    }

    void createDescriptorPool()
    {
        std::array<VkDescriptorPoolSize, 2> poolSizes {};
        poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        poolSizes[0].descriptorCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);
        poolSizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        // We need to double the number of `VK_DESCRIPTOR_TYPE_STORAGE_BUFFER`
        // types requested from the pool by two because our sets reference the
        // SSBOs of the last and current frame.
        poolSizes[1].descriptorCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT) * 2;

        VkDescriptorPoolCreateInfo poolInfo {};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();
        poolInfo.maxSets = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);

        if (vkCreateDescriptorPool(mDevice, &poolInfo, nullptr, &mDescriptorPool) != VK_SUCCESS) {
            throw std::runtime_error("Couldn't create Vulkan descriptor pool.");
        }
    }

    void createDescriptorSets()
    {
        std::vector<VkDescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT, mDescriptorSetLayout);
        VkDescriptorSetAllocateInfo allocInfo {};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = mDescriptorPool;
        allocInfo.descriptorSetCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);
        allocInfo.pSetLayouts = layouts.data();

        mDescriptorSets.resize(MAX_FRAMES_IN_FLIGHT);
        if (vkAllocateDescriptorSets(mDevice, &allocInfo, mDescriptorSets.data()) != VK_SUCCESS) {
            throw std::runtime_error("Couldn't allocate Vulkan descriptor sets.");
        }

        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i += 1) {
            VkDescriptorBufferInfo bufferInfo {};
            bufferInfo.buffer = mUniformBuffers[i];
            bufferInfo.offset = 0;
            // If we're overwriting the whole buffer, like we are in this case,
            // then it is also possible to use the VK_WHOLE_SIZE value for the
            // range.
            bufferInfo.range = sizeof(UniformBufferObject);

            std::array<VkWriteDescriptorSet, 2> descriptorWrites {};
            descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptorWrites[0].dstSet = mDescriptorSets[i];
            descriptorWrites[0].dstBinding = 0;
            descriptorWrites[0].dstArrayElement = 0;
            descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            descriptorWrites[0].descriptorCount = 1;
            descriptorWrites[0].pBufferInfo = &bufferInfo;

            const VkDescriptorBufferInfo storageBufferInfoLastFrame = {
                .buffer = mShaderStorageBuffers[(i - 1) % MAX_FRAMES_IN_FLIGHT],
                .offset = 0,
                .range = sizeof(Particle) * PARTICLE_COUNT,
            };

            descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptorWrites[1].dstSet = mComputeDescriptorSets[i];
            descriptorWrites[1].dstBinding = 1;
            descriptorWrites[1].dstArrayElement = 0;
            descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            descriptorWrites[1].descriptorCount = 1;
            descriptorWrites[1].pBufferInfo = &storageBufferInfoLastFrame;

            const VkDescriptorBufferInfo storageBufferInfoCurrentFrame = {
                .buffer = mShaderStorageBuffers[i],
                .offset = 0,
                .range = sizeof(Particle) * PARTICLE_COUNT,
            };

            descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptorWrites[1].dstSet = mComputeDescriptorSets[i];
            descriptorWrites[1].dstBinding = 2;
            descriptorWrites[1].dstArrayElement = 0;
            descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            descriptorWrites[1].descriptorCount = 1;
            descriptorWrites[1].pBufferInfo = &storageBufferInfoCurrentFrame;

            vkUpdateDescriptorSets(mDevice, static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
        }
    }

    void createCommandBuffers()
    {
        mCommandBuffers.resize(MAX_FRAMES_IN_FLIGHT);
        VkCommandBufferAllocateInfo allocInfo {};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = mCommandPool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = (uint32_t)mCommandBuffers.size();

        if (vkAllocateCommandBuffers(mDevice, &allocInfo, mCommandBuffers.data()) != VK_SUCCESS) {
            throw std::runtime_error("Couldn't allocate Vulkan command buffers.");
        }
    }

    void recordCommandBuffer(VkCommandBuffer pCommandBuffer, uint32_t pImageIndex)
    {
        VkCommandBufferBeginInfo beginInfo {};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = 0; // Optional
        beginInfo.pInheritanceInfo = nullptr; // Optional

        if (vkBeginCommandBuffer(pCommandBuffer, &beginInfo) != VK_SUCCESS) {
            throw std::runtime_error("Couldn't record Vulkan command buffer.");
        }

        VkRenderPassBeginInfo renderPassInfo {};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassInfo.renderPass = mRenderPass;
        renderPassInfo.framebuffer = mSwapChainFramebuffers[pImageIndex];
        renderPassInfo.renderArea.offset = { 0, 0 };
        renderPassInfo.renderArea.extent = mSwapChainExtent;

        // The order of clearValues should be identical to the attachments' order.
        std::array<VkClearValue, 2> clearValues {};
        clearValues[0].color = { { 0.0f, 0.0f, 0.0f, 1.0f } };
        // The initial value at each point in the depth buffer should be the
        // furthest possible depth, which is 1.0.
        clearValues[1].depthStencil = { 1.0f, 0 };
        renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
        renderPassInfo.pClearValues = clearValues.data();

        vkCmdBeginRenderPass(pCommandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

        vkCmdBindPipeline(pCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, mGraphicsPipeline);

        VkViewport viewport {};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = static_cast<float>(mSwapChainExtent.width);
        viewport.height = static_cast<float>(mSwapChainExtent.height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(pCommandBuffer, 0, 1, &viewport);

        VkRect2D scissor {};
        scissor.offset = { 0, 0 };
        scissor.extent = mSwapChainExtent;
        vkCmdSetScissor(pCommandBuffer, 0, 1, &scissor);

        vkCmdBindPipeline(pCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, mGraphicsPipeline);
        VkBuffer vertexBuffers[] = { mVertexBuffer };
        VkDeviceSize offsets[] = { 0 };
        vkCmdBindVertexBuffers(pCommandBuffer, 0, 1, vertexBuffers, offsets);
        vkCmdBindIndexBuffer(pCommandBuffer, mIndexBuffer, 0, VK_INDEX_TYPE_UINT32);

        vkCmdBindDescriptorSets(pCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, mPipelineLayout, 0, 1, &mDescriptorSets[mCurrentFrame], 0, nullptr);

        // Command buffer, index count, instance count, index buffer offset, index offset, instance offset.
        vkCmdDrawIndexed(pCommandBuffer, static_cast<uint32_t>(indices.size()), 1, 0, 0, 0);

        vkCmdEndRenderPass(pCommandBuffer);

        if (vkEndCommandBuffer(pCommandBuffer) != VK_SUCCESS) {
            throw std::runtime_error("Couldn't record Vulkan command buffer.");
        }
    }

    void createSyncObjects()
    {
        mImageAvailableSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
        mRenderFinishedSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
        VkSemaphoreCreateInfo semaphoreInfo {};
        semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        mInFlightFences.resize(MAX_FRAMES_IN_FLIGHT);
        VkFenceCreateInfo fenceInfo {};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        // On the first frame, we call `drawFrame()`, which immediately waits on
        // inFlightFence to be signaled. mInFlightFence is only signaled after a
        // frame has finished rendering, yet since this is the first frame,
        // there are no previous frames in which to signal the fence! Thus
        // `vkWaitForFences()` blocks indefinitely, waiting on something which
        // will never happen.
        // To avoid this issue, create the fence in the signaled state.
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i += 1) {
            if (vkCreateSemaphore(mDevice, &semaphoreInfo, nullptr, &mImageAvailableSemaphores[i]) != VK_SUCCESS
                || vkCreateSemaphore(mDevice, &semaphoreInfo, nullptr, &mRenderFinishedSemaphores[i]) != VK_SUCCESS
                || vkCreateFence(mDevice, &fenceInfo, nullptr, &mInFlightFences[i]) != VK_SUCCESS) {
                throw std::runtime_error("Couldn't create Vulkan semaphores or fence.");
            }
        }
    }

    void initVulkan()
    {
        createInstance();
        setupDebugMessenger();
        createSurface();
        pickPhysicalDevice();
        createLogicalDevice();
        createSwapChain();
        createImageViews();
        createRenderPass();
        createDescriptorSetLayout();
        createGraphicsPipeline();
        createCommandPool();
        createShaderStorageBuffers(); // Must occur after creating the command pool.
        createColorResources();
        createDepthResources();
        createFramebuffers(); // Must occur after creating color and depth resources.
        createTextureImage();
        createTextureImageView();
        createTextureSampler();
        loadModel();
        createVertexBuffer();
        createIndexBuffer();
        createUniformBuffers();
        createDescriptorPool();
        createDescriptorSets();
        createCommandBuffers();
        createSyncObjects();
    }

    void updateUniformBuffer(uint32_t pCurrentImage)
    {
        static auto startTime = std::chrono::high_resolution_clock::now();
        auto currentTime = std::chrono::high_resolution_clock::now();
        float time = std::chrono::duration<float, std::chrono::seconds::period>(currentTime - startTime).count();

        UniformBufferObject ubo = {
            .model = glm::rotate(glm::mat4(1.0f), glm::radians(45.0f * sin(time)), glm::vec3(0.0f, 0.0f, 1.0f)),
            .view = glm::lookAt(glm::vec3(2.0f, 2.0f, 2.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f)),
            .projection = glm::perspective(glm::radians(45.0f), mSwapChainExtent.width / (float)mSwapChainExtent.height, 0.01f, 10.0f),
        };
        // GLM was originally designed for OpenGL, where the Y coordinate of
        // the clip coordinates is inverted. The easiest way to compensate
        // for that is to flip the sign on the scaling factor of the Y axis
        // in the projection matrix. If you don't do this, then the image
        // will be rendered upside down.
        ubo.projection[1][1] *= -1.0f;

        memcpy(mUniformBuffersMapped[pCurrentImage], &ubo, sizeof(ubo));
    }

    void drawFrame()
    {
        vkWaitForFences(mDevice, 1, &mInFlightFences[mCurrentFrame], VK_TRUE, UINT64_MAX);

        uint32_t imageIndex;
        const VkResult acquireResult = vkAcquireNextImageKHR(mDevice, mSwapChain, UINT64_MAX, mImageAvailableSemaphores[mCurrentFrame], VK_NULL_HANDLE, &imageIndex);

        if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR) {
            mFramebufferResized = false;
            recreateSwapChain();
            return;
        } else if (acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR) {
            throw std::runtime_error("Couldn't acquire Vulkan swapchain image.");
        }

        // Only reset the frame if we are submitting work to prevent deadlocks
        // when recreating the swapchain.
        vkResetFences(mDevice, 1, &mInFlightFences[mCurrentFrame]);

        // This makes sure the command buffer is able to be recorded.
        vkResetCommandBuffer(mCommandBuffers[mCurrentFrame], 0);

        recordCommandBuffer(mCommandBuffers[mCurrentFrame], imageIndex);

        updateUniformBuffer(mCurrentFrame);

        VkSubmitInfo submitInfo {};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

        VkSemaphore waitSemaphores[] = { mImageAvailableSemaphores[mCurrentFrame] };
        VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = waitSemaphores;
        submitInfo.pWaitDstStageMask = waitStages;

        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &mCommandBuffers[mCurrentFrame];

        VkSemaphore signalSemaphores[] = { mRenderFinishedSemaphores[mCurrentFrame] };
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = signalSemaphores;

        if (vkQueueSubmit(mGraphicsQueue, 1, &submitInfo, mInFlightFences[mCurrentFrame]) != VK_SUCCESS) {
            throw std::runtime_error("Couldn't submit Vulkan draw command buffer.");
        }

        VkPresentInfoKHR presentInfo {};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = signalSemaphores;

        VkSwapchainKHR swapChains[] = { mSwapChain };
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = swapChains;
        presentInfo.pImageIndices = &imageIndex;
        // `pResults` allows you to specify an array of VkResult values to check
        // for every individual swap chain if presentation was successful. It's
        // not necessary if you're only using a single swap chain, because you
        // can simply use the return value of the present function.
        presentInfo.pResults = nullptr; // Optional

        const VkResult presentResult = vkQueuePresentKHR(mPresentQueue, &presentInfo);
        if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR) {
            recreateSwapChain();
        } else if (presentResult != VK_SUCCESS) {
            throw std::runtime_error("Couldn't present Vulkan swap chain image.");
        }

        mCurrentFrame = (mCurrentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
    }

    void mainLoop()
    {
        while (!glfwWindowShouldClose(mWindow)) {
            glfwPollEvents();
            drawFrame();
        }

        vkDeviceWaitIdle(mDevice);
    }

    void cleanup()
    {
        cleanupSwapChain();

        vkDestroySampler(mDevice, mTextureSampler, nullptr);
        vkDestroyImageView(mDevice, mTextureImageView, nullptr);

        vkDestroyImage(mDevice, mTextureImage, nullptr);
        vkFreeMemory(mDevice, mTextureImageMemory, nullptr);

        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i += 1) {
            vkDestroyBuffer(mDevice, mUniformBuffers[i], nullptr);
            vkFreeMemory(mDevice, mUniformBuffersMemory[i], nullptr);
        }

        // We don't need to explicitly clean up descriptor sets, because they
        // will be automatically freed when the descriptor pool is destroyed.
        vkDestroyDescriptorPool(mDevice, mDescriptorPool, nullptr);

        // The descriptor layout must stick around while we may create new
        // graphics pipelines (i.e. until the program ends).
        vkDestroyDescriptorSetLayout(mDevice, mDescriptorSetLayout, nullptr);

        vkDestroyBuffer(mDevice, mIndexBuffer, nullptr);
        // Memory must be freed *after* destroying the buffer.
        vkFreeMemory(mDevice, mIndexBufferMemory, nullptr);

        vkDestroyBuffer(mDevice, mVertexBuffer, nullptr);
        vkFreeMemory(mDevice, mVertexBufferMemory, nullptr);

        vkDestroyPipeline(mDevice, mGraphicsPipeline, nullptr);
        vkDestroyPipelineLayout(mDevice, mPipelineLayout, nullptr);

        vkDestroyRenderPass(mDevice, mRenderPass, nullptr);

        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i += 1) {
            vkDestroyFence(mDevice, mInFlightFences[i], nullptr);
            vkDestroySemaphore(mDevice, mRenderFinishedSemaphores[i], nullptr);
            vkDestroySemaphore(mDevice, mImageAvailableSemaphores[i], nullptr);
        }

        // Command buffers are automatically cleaned up, but not the command pool.
        vkDestroyCommandPool(mDevice, mCommandPool, nullptr);

        vkDestroyDevice(mDevice, nullptr);

        if (enableValidationLayers) {
            DestroyDebugUtilsMessengerEXT(mInstance, mDebugMessenger, nullptr);
        }

        vkDestroySurfaceKHR(mInstance, mSurface, nullptr);
        vkDestroyInstance(mInstance, nullptr);

        glfwDestroyWindow(mWindow);
        glfwTerminate();
    }
};

int main()
{
    HelloTriangleApplication app;

    try {
        app.run();
    } catch (const std::exception& e) {
        std::cerr << "\x1b[1;91mERROR:\x1b[22m " << e.what() << "\x1b[0m" << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

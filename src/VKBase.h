#pragma once
#include "EasyVKStart.h"

#define VK_RESULT_THROW

#define DestroyHandleBy(Func)                                 \
    if (handle) {                                             \
        Func(graphicsBase::Base().Device(), handle, nullptr); \
        handle = VK_NULL_HANDLE;                              \
    }

#define MoveHandle         \
    handle = other.handle; \
    other.handle = VK_NULL_HANDLE;

#define DefineHandleTypeOperator      \
    operator decltype(handle)() const \
    {                                 \
        return handle;                \
    }

#define DefineAddressFunction               \
    const decltype(handle) *Address() const \
    {                                       \
        return &handle;                     \
    }

#ifndef NDEBUG
#define ENABLE_DEBUG_MESSENGER true
#else
#define ENABLE_DEBUG_MESSENGER false
#endif

namespace vulkan {
constexpr VkExtent2D defaultWindowSize = {1280, 720};
inline auto &outStream = std::cout;  // 不是constexpr，因为std::cout具有外部链接

// 情况1：根据函数返回值确定是否抛异常
#ifdef VK_RESULT_THROW
class result_t {
    VkResult result;

public:
    static void (*callback_throw)(VkResult);
    result_t(VkResult result) : result(result) {}
    result_t(result_t &&other) noexcept : result(other.result)
    {
        other.result = VK_SUCCESS;
    }
    ~result_t() noexcept(false)
    {
        if (uint32_t(result) < VK_RESULT_MAX_ENUM) return;
        if (callback_throw) callback_throw(result);
        throw result;
    }
    operator VkResult()
    {
        VkResult result = this->result;
        this->result = VK_SUCCESS;
        return result;
    }
};
inline void (*result_t::callback_throw)(VkResult);

// 情况2：若抛弃函数返回值，让编译器发出警告
#elifdef VK_RESULT_NODISCARD
struct [[nodiscard]] result_t {
    VkResult result;
    result_t(VkResult result) : result(result) {}
    operator VkResult() const
    {
        return result;
    }
};
// 在本文件中关闭弃值提醒（因为我懒得做处理）
#pragma warning(disable : 4834)
#pragma warning(disable : 6031)

// 情况3：啥都不干
#else
using result_t = VkResult;
#endif

class graphicsBasePlus;

class graphicsBase {
    uint32_t apiVersion = VK_API_VERSION_1_0;                         // vulkan 版本
    VkInstance instance;                                              // vulkan 实例
    VkPhysicalDevice physicalDevice;                                  // 物理设备
    VkPhysicalDeviceProperties physicalDeviceProperties;              // 物理设备属性
    VkPhysicalDeviceMemoryProperties physicalDeviceMemoryProperties;  // 物理设备内存属性
    std::vector<VkPhysicalDevice> availablePhysicalDevices;           // 可用的物理设备

    VkDevice device;                                                   // 逻辑设备
    uint32_t queueFamilyIndex_graphics = VK_QUEUE_FAMILY_IGNORED;      // 图形 队列族 idx
    uint32_t queueFamilyIndex_presentation = VK_QUEUE_FAMILY_IGNORED;  // 呈现 队列族 idx
    uint32_t queueFamilyIndex_compute = VK_QUEUE_FAMILY_IGNORED;       // 计算 队列组 idx
    VkQueue queue_graphics;                                            // 图形 队列
    VkQueue queue_presentation;                                        // 呈现 队列
    VkQueue queue_compute;                                             // 计算 队列

    VkSurfaceKHR surface;                                     // surface
    std::vector<VkSurfaceFormatKHR> availableSurfaceFormats;  // 可用的 surface 格式

    VkSwapchainKHR swapchain;                           // 交换链
    std::vector<VkImage> swapchainImages;               // 交换链图像
    std::vector<VkImageView> swapchainImageViews;       // image views
    VkSwapchainCreateInfoKHR swapchainCreateInfo = {};  // 交换链创建信息
    uint32_t currentImageIndex = 0;                     // 当前取得的交换链图像索引

    std::vector<const char *> instanceLayers;      // 实例 层
    std::vector<const char *> instanceExtensions;  // 实例 扩展
    std::vector<const char *> deviceExtensions;    // 设备 扩展

    VkDebugUtilsMessengerEXT debugMessenger;  // debug 信息实例

    std::vector<void (*)()> callbacks_createSwapchain;   // 创建交换链时调用的回调函数
    std::vector<void (*)()> callbacks_destroySwapchain;  // 销毁交换链时调用的回调函数
    std::vector<void (*)()> callbacks_createDevice;      // 创建逻辑设备时调用的回调函数
    std::vector<void (*)()> callbacks_destroyDevice;     // 销毁逻辑设备时调用的回调函数

    graphicsBasePlus *pPlus = nullptr;  // Pimpl

    // Static
    static graphicsBase singleton;
    //--------------------
    graphicsBase() = default;
    graphicsBase(graphicsBase &&) = delete;
    ~graphicsBase()
    {
        if (!instance) return;
        if (device) {
            WaitIdle();
            if (swapchain) {
                for (auto &i : callbacks_destroySwapchain) i();
                for (auto &i : swapchainImageViews)
                    if (i) vkDestroyImageView(device, i, nullptr);
                vkDestroySwapchainKHR(device, swapchain, nullptr);
            }
            for (auto &i : callbacks_destroyDevice) i();
            vkDestroyDevice(device, nullptr);
        }
        if (surface) vkDestroySurfaceKHR(instance, surface, nullptr);
        if (debugMessenger) {
            PFN_vkDestroyDebugUtilsMessengerEXT vkDestroyDebugUtilsMessenger =
                reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
                    vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
            if (vkDestroyDebugUtilsMessenger)
                vkDestroyDebugUtilsMessenger(instance, debugMessenger, nullptr);
        }
        vkDestroyInstance(instance, nullptr);
    }
    // Non-const Function
    // 遍历物理设备的所有队列族，获得支持所需操作的队列族索引
    // 队列族: 是一组具有共同属性并支持相同功能的队列，一个队列族至少支持一个队列
    result_t GetQueueFamilyIndices(VkPhysicalDevice physicalDevice, bool enableGraphicsQueue,
                                   bool enableComputeQueue, uint32_t (&queueFamilyIndices)[3])
    {
        uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, nullptr);
        if (!queueFamilyCount) return VK_RESULT_MAX_ENUM;
        std::vector<VkQueueFamilyProperties> queueFamilyPropertieses(queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount,
                                                 queueFamilyPropertieses.data());
        std::cout << "GetQueueFamilyIndices, num : " << queueFamilyPropertieses.size() << std::endl;
        std::cout << "queue [flags, count] : ";
        for (auto it : queueFamilyPropertieses) {
            std::cout << "[" << it.queueFlags << ", ";  // bit 位表示该队列族支持的操作类型
            std::cout << it.queueCount << "], ";        // 这个队列族有多少个队列
        }
        std::cout << std::endl;
        auto &[ig, ip, ic] = queueFamilyIndices;
        ig = ip = ic = VK_QUEUE_FAMILY_IGNORED;
        for (uint32_t i = 0; i < queueFamilyCount; i++) {
            // 只在 enableGraphicsQueue 为 true 时获取支持图形操作的队列族的索引
            VkBool32 supportGraphics =
                enableGraphicsQueue &&
                queueFamilyPropertieses[i].queueFlags & VK_QUEUE_GRAPHICS_BIT;
            // 只在 enableComputeQueue 为 true 时获取支持计算的队列族的索引
            VkBool32 supportCompute =
                enableComputeQueue && queueFamilyPropertieses[i].queueFlags & VK_QUEUE_COMPUTE_BIT;
            // 只在创建了 window surface 时获取支持显示的队列族的索引
            VkBool32 supportPresentation = false;
            if (surface)
                if (VkResult result = vkGetPhysicalDeviceSurfaceSupportKHR(
                        physicalDevice, i, surface, &supportPresentation)) {
                    std::cout << std::format(
                        "[ graphicsBase ] ERROR\nFailed to determine if the queue family supports "
                        "presentation!\nError code: {}\n",
                        int32_t(result));
                    return result;
                }
            if (supportGraphics && supportCompute) {
                if (supportPresentation) {
                    ig = ip = ic = i;
                    break;
                }
                if (ig != ic || ig == VK_QUEUE_FAMILY_IGNORED) ig = ic = i;
                if (!surface) break;
            }
            if (supportGraphics && ig == VK_QUEUE_FAMILY_IGNORED) ig = i;
            if (supportPresentation && ip == VK_QUEUE_FAMILY_IGNORED) ip = i;
            if (supportCompute && ic == VK_QUEUE_FAMILY_IGNORED) ic = i;
        }
        if (ig == VK_QUEUE_FAMILY_IGNORED && enableGraphicsQueue ||
            ip == VK_QUEUE_FAMILY_IGNORED && surface ||
            ic == VK_QUEUE_FAMILY_IGNORED && enableComputeQueue)
            // 如果需要 图形/显示/计算 但 ig/ip/ic 仍是无效值
            // 说明该物理设备的队列族不支持所有所需操作，则返回失败
            return VK_RESULT_MAX_ENUM;
        queueFamilyIndex_graphics = ig;
        queueFamilyIndex_presentation = ip;
        queueFamilyIndex_compute = ic;
        std::cout << "ig : " << queueFamilyIndex_graphics << std::endl;
        std::cout << "ip : " << queueFamilyIndex_presentation << std::endl;
        std::cout << "ic : " << queueFamilyIndex_compute << std::endl;
        return VK_SUCCESS;
    }
    result_t CreateSwapchain_Internal()
    {
        // 创建 swapchain
        if (VkResult result =
                vkCreateSwapchainKHR(device, &swapchainCreateInfo, nullptr, &swapchain)) {
            std::cout << std::format(
                "[ graphicsBase ] ERROR\nFailed to create a swapchain!\nError code: {}\n",
                int32_t(result));
            return result;
        }

        // 创建 image view
        // 先查询 swapchain 中的 image
        uint32_t swapchainImageCount;
        if (VkResult result =
                vkGetSwapchainImagesKHR(device, swapchain, &swapchainImageCount, nullptr)) {
            std::cout << std::format(
                "[ graphicsBase ] ERROR\nFailed to get the count of swapchain images!\nError code: "
                "{}\n",
                int32_t(result));
            return result;
        }
        swapchainImages.resize(swapchainImageCount);
        if (VkResult result = vkGetSwapchainImagesKHR(device, swapchain, &swapchainImageCount,
                                                      swapchainImages.data())) {
            std::cout << std::format(
                "[ graphicsBase ] ERROR\nFailed to get swapchain images!\nError code: {}\n",
                int32_t(result));
            return result;
        }

        // 然后为已存在的 image 创建其 image view，类似 C++ 的 std::string_view
        swapchainImageViews.resize(swapchainImageCount);
        VkImageViewCreateInfo imageViewCreateInfo = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = swapchainCreateInfo.imageFormat,
            //.components = {},
            .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
        for (size_t i = 0; i < swapchainImageCount; i++) {
            imageViewCreateInfo.image = swapchainImages[i];
            if (VkResult result = vkCreateImageView(device, &imageViewCreateInfo, nullptr,
                                                    &swapchainImageViews[i])) {
                std::cout << std::format(
                    "[ graphicsBase ] ERROR\nFailed to create a swapchain image view!\nError code: "
                    "{}\n",
                    int32_t(result));
                return result;
            }
        }
        return VK_SUCCESS;
    }
    result_t CreateDebugMessenger()
    {
        static PFN_vkDebugUtilsMessengerCallbackEXT DebugUtilsMessengerCallback =
            [](VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
               VkDebugUtilsMessageTypeFlagsEXT messageTypes,
               const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData,
               void *pUserData) -> VkBool32 {
            // 回调操作简单处理为输出到控制台
            std::cout << std::format("{}\n\n", pCallbackData->pMessage);
            return VK_FALSE;
        };
        VkDebugUtilsMessengerCreateInfoEXT debugUtilsMessengerCreateInfo = {
            .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,  // 指示该结构体的类型
            .messageSeverity =
                VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |  // 需要获取的哪些级别的
                                                                   // debug 信息
                VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
            .messageType =
                VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |  // 需要获取哪些类型的 debug 信息
                VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
            .pfnUserCallback = DebugUtilsMessengerCallback};  // 产生 debug 信息后所调用的回调函数
        // extension 提供的相关函数，大都通过 vkGetInstanceProcAddr 来获取
        PFN_vkCreateDebugUtilsMessengerEXT vkCreateDebugUtilsMessenger =
            reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
                vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));
        if (vkCreateDebugUtilsMessenger) {
            // para1: vulkan 实例的 handle
            // para2: 创建信息结构体的地址
            // para3: 有必要的话，自定义内存分配方式的结构体的地址
            // para4: 创建成功则将 debug message 的 handle 写入该参数
            VkResult result = vkCreateDebugUtilsMessenger(instance, &debugUtilsMessengerCreateInfo,
                                                          nullptr, &debugMessenger);
            if (result)
                std::cout << std::format(
                    "[ graphicsBase ] ERROR\nFailed to create a debug messenger!\nError code: {}\n",
                    int32_t(result));
            return result;
        }
        std::cout << std::format(
            "[ graphicsBase ] ERROR\nFailed to get the function pointer of "
            "vkCreateDebugUtilsMessengerEXT!\n");
        return VK_RESULT_MAX_ENUM;
    }
    // Static Function
    static void AddLayerOrExtension(std::vector<const char *> &container, const char *name)
    {
        for (auto &i : container)
            if (!strcmp(name, i)) return;
        container.push_back(name);
    }
    static void ExecuteCallbacks(std::vector<void (*)()> &callbacks)
    {
        for (size_t size = callbacks.size(), i = 0; i < size; i++) callbacks[i]();
        // for (auto& i : callbacks) i();                               //Not safe
        // for (size_t i = 0; i < callbacks.size(); i++) callbacks[i]();//Not safe
    }

public:
    // Getter
    uint32_t ApiVersion() const
    {
        return apiVersion;
    }
    VkInstance Instance() const
    {
        return instance;
    }
    VkPhysicalDevice PhysicalDevice() const
    {
        return physicalDevice;
    }
    constexpr const VkPhysicalDeviceProperties &PhysicalDeviceProperties() const
    {
        return physicalDeviceProperties;
    }
    constexpr const VkPhysicalDeviceMemoryProperties &PhysicalDeviceMemoryProperties() const
    {
        return physicalDeviceMemoryProperties;
    }
    VkPhysicalDevice AvailablePhysicalDevice(uint32_t index) const
    {
        return availablePhysicalDevices[index];
    }
    uint32_t AvailablePhysicalDeviceCount() const
    {
        return uint32_t(availablePhysicalDevices.size());
    }

    VkDevice Device() const
    {
        return device;
    }
    uint32_t QueueFamilyIndex_Graphics() const
    {
        return queueFamilyIndex_graphics;
    }
    uint32_t QueueFamilyIndex_Presentation() const
    {
        return queueFamilyIndex_presentation;
    }
    uint32_t QueueFamilyIndex_Compute() const
    {
        return queueFamilyIndex_compute;
    }
    VkQueue Queue_Graphics() const
    {
        return queue_graphics;
    }
    VkQueue Queue_Presentation() const
    {
        return queue_presentation;
    }
    VkQueue Queue_Compute() const
    {
        return queue_compute;
    }

    VkSurfaceKHR Surface() const
    {
        return surface;
    }
    VkFormat AvailableSurfaceFormat(uint32_t index) const
    {
        return availableSurfaceFormats[index].format;
    }
    VkColorSpaceKHR AvailableSurfaceColorSpace(uint32_t index) const
    {
        return availableSurfaceFormats[index].colorSpace;
    }
    uint32_t AvailableSurfaceFormatCount() const
    {
        return uint32_t(availableSurfaceFormats.size());
    }

    VkSwapchainKHR Swapchain() const
    {
        return swapchain;
    }
    VkImage SwapchainImage(uint32_t index) const
    {
        return swapchainImages[index];
    }
    VkImageView SwapchainImageView(uint32_t index) const
    {
        return swapchainImageViews[index];
    }
    uint32_t SwapchainImageCount() const
    {
        return uint32_t(swapchainImages.size());
    }
    uint32_t CurrentImageIndex() const
    {
        return currentImageIndex;
    }
    constexpr const VkSwapchainCreateInfoKHR &SwapchainCreateInfo() const
    {
        return swapchainCreateInfo;
    }

    const std::vector<const char *> &InstanceLayers() const
    {
        return instanceLayers;
    }
    const std::vector<const char *> &InstanceExtensions() const
    {
        return instanceExtensions;
    }
    const std::vector<const char *> &DeviceExtensions() const
    {
        return deviceExtensions;
    }

    // Const & Non-const Function
    void AddCallback_CreateSwapchain(void (*function)())
    {
        callbacks_createSwapchain.push_back(function);
    }
    void AddCallback_DestroySwapchain(void (*function)())
    {
        callbacks_destroySwapchain.push_back(function);
    }
    void AddCallback_CreateDevice(void (*function)())
    {
        callbacks_createDevice.push_back(function);
    }
    void AddCallback_DestroyDevice(void (*function)())
    {
        callbacks_destroyDevice.push_back(function);
    }
    //                    Create Instance
    void AddInstanceLayer(const char *layerName)
    {
        AddLayerOrExtension(instanceLayers, layerName);
    }
    void AddInstanceExtension(const char *extensionName)
    {
        AddLayerOrExtension(instanceExtensions, extensionName);
    }
    result_t UseLatestApiVersion()
    {
        if (vkGetInstanceProcAddr(VK_NULL_HANDLE, "vkEnumerateInstanceVersion"))
            return vkEnumerateInstanceVersion(&apiVersion);
        return VK_SUCCESS;
    }
    result_t CreateInstance(VkInstanceCreateFlags flags = 0)
    {
        if constexpr (ENABLE_DEBUG_MESSENGER) {
            // 添加 layer，启用 验证层
            // AddInstanceLayer("VK_LAYER_KHRONOS_validation");
            // 添加 extension，该扩展可使开发者可以获得更多信息，如可注册回调函数获取 debug 信息
            AddInstanceExtension(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        }
        VkApplicationInfo applicatianInfo = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
                                             .apiVersion = apiVersion};
        VkInstanceCreateInfo instanceCreateInfo = {
            .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
            .flags = flags,
            .pApplicationInfo = &applicatianInfo,
            .enabledLayerCount = uint32_t(instanceLayers.size()),
            .ppEnabledLayerNames = instanceLayers.data(),
            .enabledExtensionCount = uint32_t(instanceExtensions.size()),
            .ppEnabledExtensionNames = instanceExtensions.data()};
        // 创建 vulkan 实例
        // 该函数也会检验传入的所需 layer、extension 是否存在，都存在才会返回成功
        if (VkResult result = vkCreateInstance(&instanceCreateInfo, nullptr, &instance)) {
            std::cout << std::format(
                "[ graphicsBase ] ERROR\nFailed to create a vulkan instance!\nError code: {}\n",
                int32_t(result));
            return result;
        }
        std::cout << std::format("Vulkan API Version: {}.{}.{}\n", VK_VERSION_MAJOR(apiVersion),
                                 VK_VERSION_MINOR(apiVersion), VK_VERSION_PATCH(apiVersion));
        if constexpr (ENABLE_DEBUG_MESSENGER)
            // 用于获取验证层捕获到的 debug 信息
            CreateDebugMessenger();
        return VK_SUCCESS;
    }
    result_t CheckInstanceLayers(std::span<const char *> layersToCheck) const
    {
        uint32_t layerCount;
        std::vector<VkLayerProperties> availableLayers;
        if (VkResult result = vkEnumerateInstanceLayerProperties(&layerCount, nullptr)) {
            std::cout << std::format(
                "[ graphicsBase ] ERROR\nFailed to get the count of instance layers!\n");
            return result;
        }
        if (layerCount) {
            availableLayers.resize(layerCount);
            if (VkResult result =
                    vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data())) {
                std::cout << std::format(
                    "[ graphicsBase ] ERROR\nFailed to enumerate instance layer properties!\nError "
                    "code: {}\n",
                    int32_t(result));
                return result;
            }
            for (auto &i : layersToCheck) {
                bool found = false;
                for (auto &j : availableLayers)
                    if (!strcmp(i, j.layerName)) {
                        found = true;
                        break;
                    }
                if (!found) i = nullptr;
            }
        } else
            for (auto &i : layersToCheck) i = nullptr;
        return VK_SUCCESS;
    }
    result_t CheckInstanceExtensions(std::span<const char *> extensionsToCheck,
                                     const char *layerName) const
    {
        uint32_t extensionCount;
        std::vector<VkExtensionProperties> availableExtensions;
        if (VkResult result =
                vkEnumerateInstanceExtensionProperties(layerName, &extensionCount, nullptr)) {
            layerName
                ? std::cout << std::format(
                      "[ graphicsBase ] ERROR\nFailed to get the count of instance "
                      "extensions!\nLayer name:{}\n",
                      layerName)
                : std::cout << std::format(
                      "[ graphicsBase ] ERROR\nFailed to get the count of instance extensions!\n");
            return result;
        }
        if (extensionCount) {
            availableExtensions.resize(extensionCount);
            if (VkResult result = vkEnumerateInstanceExtensionProperties(
                    layerName, &extensionCount, availableExtensions.data())) {
                std::cout << std::format(
                    "[ graphicsBase ] ERROR\nFailed to enumerate instance extension "
                    "properties!\nError code: {}\n",
                    int32_t(result));
                return result;
            }
            for (auto &i : extensionsToCheck) {
                bool found = false;
                for (auto &j : availableExtensions)
                    if (!strcmp(i, j.extensionName)) {
                        found = true;
                        break;
                    }
                if (!found) i = nullptr;
            }
        } else
            for (auto &i : extensionsToCheck) i = nullptr;
        return VK_SUCCESS;
    }
    void InstanceLayers(const std::vector<const char *> &layerNames)
    {
        instanceLayers = layerNames;
    }
    void InstanceExtensions(const std::vector<const char *> &extensionNames)
    {
        instanceExtensions = extensionNames;
    }
    //                    Set Window Surface
    void Surface(VkSurfaceKHR surface)
    {
        if (!this->surface) this->surface = surface;
    }
    //                    Create Logical Device
    void AddDeviceExtension(const char *extensionName)
    {
        AddLayerOrExtension(deviceExtensions, extensionName);
    }
    result_t GetPhysicalDevices()
    {
        uint32_t deviceCount;
        if (VkResult result = vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr)) {
            std::cout << std::format(
                "[ graphicsBase ] ERROR\nFailed to get the count of physical devices!\nError code: "
                "{}\n",
                int32_t(result));
            return result;
        }
        if (!deviceCount)
            std::cout << std::format(
                "[ graphicsBase ] ERROR\nFailed to find any physical device supports vulkan!\n"),
                abort();
        availablePhysicalDevices.resize(deviceCount);
        VkResult result =
            vkEnumeratePhysicalDevices(instance, &deviceCount, availablePhysicalDevices.data());
        if (result)
            std::cout << std::format(
                "[ graphicsBase ] ERROR\nFailed to enumerate physical devices!\nError code: {}\n",
                int32_t(result));
        std::cout << "GetPhysicalDevices num : " << deviceCount << std::endl;
        return result;
    }
    result_t DeterminePhysicalDevice(uint32_t deviceIndex = 0, bool enableGraphicsQueue = true,
                                     bool enableComputeQueue = true)
    {
        // 定义一个特殊值用于标记一个队列族索引已被找过但未找到
        static constexpr uint32_t notFound = INT32_MAX;  //== VK_QUEUE_FAMILY_IGNORED & INT32_MAX
        struct queueFamilyIndexCombination {
            uint32_t graphics = VK_QUEUE_FAMILY_IGNORED;
            uint32_t presentation = VK_QUEUE_FAMILY_IGNORED;
            uint32_t compute = VK_QUEUE_FAMILY_IGNORED;
        };
        // queueFamilyIndices 用于为每个物理设备保存一份队列族所支持的操作索引
        static std::vector<queueFamilyIndexCombination> queueFamilyIndexCombinations(
            availablePhysicalDevices.size());
        auto &[ig, ip, ic] = queueFamilyIndexCombinations[deviceIndex];
        // 之前已获取过该物理设备队列族支持的操作，此处直接判别，若不满足所需操作，则直接返回失败
        if (ig == notFound && enableGraphicsQueue || ip == notFound && surface ||
            ic == notFound && enableComputeQueue)
            return VK_RESULT_MAX_ENUM;
        if (ig == VK_QUEUE_FAMILY_IGNORED && enableGraphicsQueue ||
            ip == VK_QUEUE_FAMILY_IGNORED && surface ||
            ic == VK_QUEUE_FAMILY_IGNORED && enableComputeQueue) {
            uint32_t indices[3];
            VkResult result =
                GetQueueFamilyIndices(availablePhysicalDevices[deviceIndex], enableGraphicsQueue,
                                      enableComputeQueue, indices);
            if (result == VK_SUCCESS || result == VK_RESULT_MAX_ENUM) {
                // 返回 VK_SUCCESS，说明物理设备的队列族满足所需操作
                // 返回 VK_RESULT_MAX_ENUM，说明物理设备的队列族不满足所需操作
                if (enableGraphicsQueue) ig = indices[0] & INT32_MAX;
                if (surface) ip = indices[1] & INT32_MAX;
                if (enableComputeQueue) ic = indices[2] & INT32_MAX;
            }
            if (result) return result;
        } else {
            queueFamilyIndex_graphics = enableGraphicsQueue ? ig : VK_QUEUE_FAMILY_IGNORED;
            queueFamilyIndex_presentation = surface ? ip : VK_QUEUE_FAMILY_IGNORED;
            queueFamilyIndex_compute = enableComputeQueue ? ic : VK_QUEUE_FAMILY_IGNORED;
        }
        physicalDevice = availablePhysicalDevices[deviceIndex];
        return VK_SUCCESS;
    }
    result_t CreateDevice(VkDeviceCreateFlags flags = 0)
    {
        float queuePriority = 1.f;
        VkDeviceQueueCreateInfo queueCreateInfos[3] = {
            {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,  // 指示结构体类型
             .queueCount = 1,  // 该队列族索引下，要创建的队列个数，需小于该队列族下的队列数量
             .pQueuePriorities = &queuePriority},  // 队列优先级，范围 [0,1]，1 优先级最高
            {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
             .queueCount = 1,
             .pQueuePriorities = &queuePriority},
            {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
             .queueCount = 1,
             .pQueuePriorities = &queuePriority}};
        uint32_t queueCreateInfoCount = 0;
        if (queueFamilyIndex_graphics != VK_QUEUE_FAMILY_IGNORED)
            queueCreateInfos[queueCreateInfoCount++].queueFamilyIndex =
                queueFamilyIndex_graphics;  // 队列族索引
        if (queueFamilyIndex_presentation != VK_QUEUE_FAMILY_IGNORED &&
            queueFamilyIndex_presentation != queueFamilyIndex_graphics)
            queueCreateInfos[queueCreateInfoCount++].queueFamilyIndex =
                queueFamilyIndex_presentation;
        if (queueFamilyIndex_compute != VK_QUEUE_FAMILY_IGNORED &&
            queueFamilyIndex_compute != queueFamilyIndex_graphics &&
            queueFamilyIndex_compute != queueFamilyIndex_presentation)
            queueCreateInfos[queueCreateInfoCount++].queueFamilyIndex = queueFamilyIndex_compute;
        VkPhysicalDeviceFeatures physicalDeviceFeatures;
        // 获取物理设备支持的特性
        vkGetPhysicalDeviceFeatures(physicalDevice, &physicalDeviceFeatures);
        VkDeviceCreateInfo deviceCreateInfo = {
            .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,  // 指示结构体类型
            .flags = flags,
            .queueCreateInfoCount = queueCreateInfoCount,  // 队列创建信息的个数
            .pQueueCreateInfos = queueCreateInfos,         // 队列创建信息结构体首地址
            .enabledExtensionCount =
                uint32_t(deviceExtensions.size()),               // （已弃用）设备级 layer 个数
            .ppEnabledExtensionNames = deviceExtensions.data(),  // （已弃用）设备级 layer 首地址
            .pEnabledFeatures = &physicalDeviceFeatures};        // 指明需要开启哪些特性
        // 创建逻辑设备
        if (VkResult result = vkCreateDevice(physicalDevice, &deviceCreateInfo, nullptr, &device)) {
            std::cout << std::format(
                "[ graphicsBase ] ERROR\nFailed to create a vulkan logical device!\nError code: "
                "{}\n",
                int32_t(result));
            return result;
        }
        if (queueFamilyIndex_graphics != VK_QUEUE_FAMILY_IGNORED)
            // 获取队列
            vkGetDeviceQueue(device, queueFamilyIndex_graphics, 0, &queue_graphics);
        if (queueFamilyIndex_presentation != VK_QUEUE_FAMILY_IGNORED)
            vkGetDeviceQueue(device, queueFamilyIndex_presentation, 0, &queue_presentation);
        if (queueFamilyIndex_compute != VK_QUEUE_FAMILY_IGNORED)
            vkGetDeviceQueue(device, queueFamilyIndex_compute, 0, &queue_compute);

        // 逻辑设备创建成功，说明物理设备已确定、不会变更，所以在这里获取物理设备的其他属性
        // 获取物理设备属性
        vkGetPhysicalDeviceProperties(physicalDevice, &physicalDeviceProperties);
        // 获取物理设备内存属性
        vkGetPhysicalDeviceMemoryProperties(physicalDevice, &physicalDeviceMemoryProperties);
        std::cout << std::format("Renderer: {}\n", physicalDeviceProperties.deviceName);
        return VK_SUCCESS;
    }
    result_t CheckDeviceExtensions(std::span<const char *> extensionsToCheck,
                                   const char *layerName = nullptr) const
    {
        return VK_SUCCESS;
    }
    void DeviceExtensions(const std::vector<const char *> &extensionNames)
    {
        deviceExtensions = extensionNames;
    }
    // Create Swapchain
    result_t GetSurfaceFormats()
    {
        uint32_t surfaceFormatCount;
        // 查询 surface 支持的 image 格式 和 色彩空间
        if (VkResult result = vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface,
                                                                   &surfaceFormatCount, nullptr)) {
            std::cout << std::format(
                "[ graphicsBase ] ERROR\nFailed to get the count of surface formats!\nError code: "
                "{}\n",
                int32_t(result));
            return result;
        }
        if (!surfaceFormatCount)
            std::cout << std::format(
                "[ graphicsBase ] ERROR\nFailed to find any supported surface format!\n"),
                abort();
        availableSurfaceFormats.resize(surfaceFormatCount);
        VkResult result = vkGetPhysicalDeviceSurfaceFormatsKHR(
            physicalDevice, surface, &surfaceFormatCount, availableSurfaceFormats.data());
        if (result)
            std::cout << std::format(
                "[ graphicsBase ] ERROR\nFailed to get surface formats!\nError code: {}\n",
                int32_t(result));
        return result;
    }
    result_t SetSurfaceFormat(VkSurfaceFormatKHR surfaceFormat)
    {
        bool formatIsAvailable = false;
        if (!surfaceFormat.format) {
            for (auto &i : availableSurfaceFormats)
                if (i.colorSpace == surfaceFormat.colorSpace) {
                    swapchainCreateInfo.imageFormat = i.format;
                    swapchainCreateInfo.imageColorSpace = i.colorSpace;
                    formatIsAvailable = true;
                    break;
                }
        } else
            for (auto &i : availableSurfaceFormats)
                if (i.format == surfaceFormat.format && i.colorSpace == surfaceFormat.colorSpace) {
                    swapchainCreateInfo.imageFormat = i.format;
                    swapchainCreateInfo.imageColorSpace = i.colorSpace;
                    formatIsAvailable = true;
                    break;
                }
        if (!formatIsAvailable) return VK_ERROR_FORMAT_NOT_SUPPORTED;
        // 考虑到该函数可能在运行过程中被调用，例如运行过程中开启/关闭 HDR 功能
        // 所以需要考虑到 swapchain 已存在的情况，已存在则重建
        if (swapchain) return RecreateSwapchain();
        return VK_SUCCESS;
    }
    result_t CreateSwapchain(bool limitFrameRate = true, VkSwapchainCreateFlagsKHR flags = 0)
    {
        // Get surface capabilities
        VkSurfaceCapabilitiesKHR surfaceCapabilities = {};
        // 查询 surface 的能力
        if (VkResult result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface,
                                                                        &surfaceCapabilities)) {
            std::cout << std::format(
                "[ graphicsBase ] ERROR\nFailed to get physical device surface "
                "capabilities!\nError code: {}\n",
                int32_t(result));
            return result;
        }
        // Set image count
        // 一般 surface 支持的 minImageCount 都会是 2，毕竟双缓冲
        swapchainCreateInfo.minImageCount =
            surfaceCapabilities.minImageCount +
            (surfaceCapabilities.maxImageCount > surfaceCapabilities.minImageCount);
        // Set image extent
        swapchainCreateInfo.imageExtent =
            surfaceCapabilities.currentExtent.width == -1  // width、height 都为 -1 表示当前未指定
                ? VkExtent2D {glm::clamp(defaultWindowSize.width,  // 未指定就设置一下
                                         surfaceCapabilities.minImageExtent.width,
                                         surfaceCapabilities.maxImageExtent.width),
                              glm::clamp(defaultWindowSize.height,
                                         surfaceCapabilities.minImageExtent.height,
                                         surfaceCapabilities.maxImageExtent.height)}
                : surfaceCapabilities.currentExtent;  // 已指定就沿用当前
        // Set transformation
        // 设置变换
        swapchainCreateInfo.preTransform = surfaceCapabilities.currentTransform;
        // Set alpha compositing mode
        // 设置混合模式
        if (surfaceCapabilities.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR)
            swapchainCreateInfo.compositeAlpha = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
        else
            for (size_t i = 0; i < 4; i++)
                if (surfaceCapabilities.supportedCompositeAlpha & 1 << i) {
                    swapchainCreateInfo.compositeAlpha = VkCompositeAlphaFlagBitsKHR(
                        surfaceCapabilities.supportedCompositeAlpha & 1 << i);
                    break;
                }
        // Set image usage
        // 可作为颜色附件
        swapchainCreateInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        if (surfaceCapabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT)
            // 可作为数据传送的 src
            swapchainCreateInfo.imageUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        if (surfaceCapabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT)
            // 可作为数据传送的 dst
            swapchainCreateInfo.imageUsage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        else
            std::cout << std::format(
                "[ graphicsBase ] WARNING\nVK_IMAGE_USAGE_TRANSFER_DST_BIT isn't supported!\n");

        // Get surface formats
        if (!availableSurfaceFormats.size())
            if (VkResult result = GetSurfaceFormats()) return result;
        // If surface format is not determined, select a a four-component UNORM format
        if (!swapchainCreateInfo.imageFormat)
            // R8G8B8A8: RGBA 四通道，每个通道 8 位 bit
            // UNORM: U 无符号整型，NORM 在着色器中使用时会归一化到 [0,1]
            if (SetSurfaceFormat({VK_FORMAT_R8G8B8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR}) &&
                SetSurfaceFormat({VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR})) {
                swapchainCreateInfo.imageFormat = availableSurfaceFormats[0].format;
                swapchainCreateInfo.imageColorSpace = availableSurfaceFormats[0].colorSpace;
                std::cout << std::format(
                    "[ graphicsBase ] WARNING\nFailed to select a four-component UNORM surface "
                    "format!\n");
            }

        // Get surface present modes
        // 查询 surface 支持的呈现模式
        uint32_t surfacePresentModeCount;
        if (VkResult result = vkGetPhysicalDeviceSurfacePresentModesKHR(
                physicalDevice, surface, &surfacePresentModeCount, nullptr)) {
            std::cout << std::format(
                "[ graphicsBase ] ERROR\nFailed to get the count of surface present modes!\nError "
                "code: {}\n",
                int32_t(result));
            return result;
        }
        if (!surfacePresentModeCount)
            std::cout << std::format(
                "[ graphicsBase ] ERROR\nFailed to find any surface present mode!\n"),
                abort();
        std::vector<VkPresentModeKHR> surfacePresentModes(surfacePresentModeCount);
        if (VkResult result = vkGetPhysicalDeviceSurfacePresentModesKHR(
                physicalDevice, surface, &surfacePresentModeCount, surfacePresentModes.data())) {
            std::cout << std::format(
                "[ graphicsBase ] ERROR\nFailed to get surface present modes!\nError code: {}\n",
                int32_t(result));
            return result;
        }
        // Set present mode to mailbox if available and necessary
        // 设置呈现模式
        swapchainCreateInfo.presentMode = VK_PRESENT_MODE_FIFO_KHR;
        if (!limitFrameRate)
            for (size_t i = 0; i < surfacePresentModeCount; i++)
                if (surfacePresentModes[i] == VK_PRESENT_MODE_MAILBOX_KHR) {
                    swapchainCreateInfo.presentMode = VK_PRESENT_MODE_MAILBOX_KHR;
                    break;
                }

        swapchainCreateInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;  // 指示结构体类型
        swapchainCreateInfo.flags = flags;
        swapchainCreateInfo.surface = surface;
        swapchainCreateInfo.imageArrayLayers = 1;  // 普通 2D 显示设备，设为 1
        swapchainCreateInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;  // 分享模式设为独占
        swapchainCreateInfo.clipped = VK_TRUE;                             // 舍弃

        if (VkResult result = CreateSwapchain_Internal()) return result;
        for (auto &i : callbacks_createSwapchain) i();
        return VK_SUCCESS;
    }

    //                    After Initialization
    void Terminate()
    {
        this->~graphicsBase();
        instance = VK_NULL_HANDLE;
        physicalDevice = VK_NULL_HANDLE;
        device = VK_NULL_HANDLE;
        surface = VK_NULL_HANDLE;
        swapchain = VK_NULL_HANDLE;
        swapchainImages.resize(0);
        swapchainImageViews.resize(0);
        swapchainCreateInfo = {};
        debugMessenger = VK_NULL_HANDLE;
    }

    // 重建逻辑设备
    result_t RecreateDevice(VkDeviceCreateFlags flags = 0)
    {
        if (device) {
            // 销毁原有的逻辑设备
            if (VkResult result = WaitIdle();
                result != VK_SUCCESS && result != VK_ERROR_DEVICE_LOST)
                return result;
            if (swapchain) {
                // 销毁原有 swapchain
                ExecuteCallbacks(callbacks_destroySwapchain);
                for (auto &i : swapchainImageViews)
                    if (i) vkDestroyImageView(device, i, nullptr);
                swapchainImageViews.resize(0);
                vkDestroySwapchainKHR(device, swapchain, nullptr);
                swapchain = VK_NULL_HANDLE;
                swapchainCreateInfo = {};
            }
            ExecuteCallbacks(callbacks_destroyDevice);
            vkDestroyDevice(device, nullptr);
            device = VK_NULL_HANDLE;
        }
        // 创建新的逻辑设备
        return CreateDevice(flags);
    }

    // 有些情况下会需要重建交换链 swapchain，比如开关 HDR，或者窗口大小改变。
    result_t RecreateSwapchain()
    {
        VkSurfaceCapabilitiesKHR surfaceCapabilities = {};
        if (VkResult result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface,
                                                                        &surfaceCapabilities)) {
            std::cout << std::format(
                "[ graphicsBase ] ERROR\nFailed to get physical device surface "
                "capabilities!\nError code: {}\n",
                int32_t(result));
            return result;
        }
        // 当前的宽、高为 0，常见于最小化到任务栏窗口，则此时不重建交换链
        if (surfaceCapabilities.currentExtent.width == 0 ||
            surfaceCapabilities.currentExtent.height == 0)
            return VK_SUBOPTIMAL_KHR;
        swapchainCreateInfo.imageExtent = surfaceCapabilities.currentExtent;
        swapchainCreateInfo.oldSwapchain = swapchain;  // 填入旧 swapchain，可能有利于重用一些资源
        // 重建 swapchain 前，需确保没有正在使用旧的 swapchain
        // 等待 图形 和 呈现 队列空闲（swapchain 被图形队列写入、被呈现队列读取）
        VkResult result = vkQueueWaitIdle(queue_graphics);
        if (!result && queue_graphics != queue_presentation)
            result = vkQueueWaitIdle(queue_presentation);
        if (result) {
            std::cout << std::format(
                "[ graphicsBase ] ERROR\nFailed to wait for the queue to be idle!\nError code: "
                "{}\n",
                int32_t(result));
            return result;
        }

        // 销毁 swapchain 时的回调函数
        for (auto &i : callbacks_destroySwapchain) i();
        // 销毁旧的 image view
        for (auto &i : swapchainImageViews)
            if (i) vkDestroyImageView(device, i, nullptr);
        swapchainImageViews.resize(0);
        // 创建新的 swapchain
        if (result = CreateSwapchain_Internal()) return result;
        // 创建 swapchain 时的回调函数
        for (auto &i : callbacks_createSwapchain) i();
        return VK_SUCCESS;
    }
    result_t WaitIdle() const
    {
        VkResult result = vkDeviceWaitIdle(device);
        if (result)
            std::cout << std::format(
                "[ graphicsBase ] ERROR\nFailed to wait for the device to be idle!\nError code: "
                "{}\n",
                int32_t(result));
        return result;
    }
    // 该函数用于获取交换链图像索引到currentImageIndex，以及在需要重建交换链时调用RecreateSwapchain()、重建交换链后销毁旧交换链
    result_t SwapImage(VkSemaphore semaphore_imageIsAvailable)
    {
        // 销毁旧交换链（若存在）
        if (swapchainCreateInfo.oldSwapchain && swapchainCreateInfo.oldSwapchain != swapchain) {
            vkDestroySwapchainKHR(device, swapchainCreateInfo.oldSwapchain, nullptr);
            swapchainCreateInfo.oldSwapchain = VK_NULL_HANDLE;
        }
        // 获取交换链图像索引
        // VkResult VKAPI_CALL vkAcquireNextImageKHR( // 获取下一个可用于呈现的图像的索引
        //     VkDevice                                    device,       // 指定逻辑设备
        //     VkSwapchainKHR                              swapchain,    // 指定交换链
        //     uint64_t                                    timeout,      // 如果没有 image 可用，可等待的超时时间，单位纳秒，UINT64_MAX 表示无限制
        //     VkSemaphore                                 semaphore,    // 当获取到 image 在可被安全读写后，会将该信号量置位。注意，并不是在这个函数被调用结束时就置位。
        //     VkFence                                     fence,        // 当获取到 image 在可被安全读写后，会将该栅栏置位。注意，并不是在这个函数被调用结束时就置位。
        //     uint32_t*                                   pImageIndex); // 返回获取到的图像索引
        // 个人理解：vkAcquireNextImageKHR 只是返回给你一个可用的 image，这里的意思更接近是 GPU 已经给你分配了这个 image，但是此时你未必能用，因为这个 image 可能还在被其他操作使用中。
        // 不过现在你已经可以开始提前给这个 image 安排后续的操作了。但你安排的操作，就要等这个 image 之前的操作完成了，才能真正开始执行你安排的操作，这个时候才置位 semaphore、fence，
        // 而不是在 vkAcquireNextImageKHR 被调用结束时就置位。
        while (VkResult result =
                   vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, semaphore_imageIsAvailable,
                                         VK_NULL_HANDLE, &currentImageIndex))
            switch (result) {
                case VK_SUBOPTIMAL_KHR:
                case VK_ERROR_OUT_OF_DATE_KHR:
                    if (VkResult result = RecreateSwapchain()) return result;
                    break;  // 注意重建交换链后仍需要获取图像，通过break递归，再次执行while的条件判定语句
                default:
                    outStream << std::format(
                        "[ graphicsBase ] ERROR\nFailed to acquire the next image!\nError code: "
                        "{}\n",
                        string_VkResult(result));
                    return result;
            }
        return VK_SUCCESS;
    }
    // 该函数用于将命令缓冲区提交到用于图形的队列
    result_t SubmitCommandBuffer_Graphics(VkSubmitInfo &submitInfo,
                                          VkFence fence = VK_NULL_HANDLE) const
    {
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        VkResult result = vkQueueSubmit(queue_graphics, 1, &submitInfo, fence);
        if (result)
            outStream << std::format(
                "[ graphicsBase ] ERROR\nFailed to submit the command buffer!\nError code: {}\n",
                string_VkResult(result));
        return result;
    }
    // 该函数用于在渲染循环中将命令缓冲区提交到图形队列的常见情形
    result_t SubmitCommandBuffer_Graphics(VkCommandBuffer commandBuffer,
                                          VkSemaphore semaphore_imageIsAvailable = VK_NULL_HANDLE,
                                          VkSemaphore semaphore_renderingIsOver = VK_NULL_HANDLE,
                                          VkFence fence = VK_NULL_HANDLE,
                                          VkPipelineStageFlags waitDstStage_imageIsAvailable =
                                              VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT) const
    {
        VkSubmitInfo submitInfo = {.commandBufferCount = 1,  // 提交的 cmd buffer 个数
                                   .pCommandBuffers = &commandBuffer // 提交的 cmd buffers
                                  };
        if (semaphore_imageIsAvailable)
            submitInfo.waitSemaphoreCount = 1, // pWaitSemaphores 和 pWaitDstStageMask 中元素的个数，都收 waitSemaphoreCount 控制，成对对应
            submitInfo.pWaitSemaphores = &semaphore_imageIsAvailable, // 执行 command buffer 中的命令前，需要等待的信号量
            submitInfo.pWaitDstStageMask = &waitDstStage_imageIsAvailable; // 在 pWaitDstStageMask 阶段开始前，才需要等待信号量
            // 合起来就是，cmd buffer 执行到 pWaitDstStageMask 阶段开始前时，需要等待 semaphore_imageIsAvailable 这个信号量被置位，才开始执行 pWaitDstStageMask
        if (semaphore_renderingIsOver)
            submitInfo.signalSemaphoreCount = 1,
            submitInfo.pSignalSemaphores = &semaphore_renderingIsOver; // command buffer 中的命令执行完后，需要发送的信号量
        return SubmitCommandBuffer_Graphics(submitInfo, fence);
    }
    // 该函数用于将命令缓冲区提交到用于图形的队列，且只使用栅栏的常见情形
    result_t SubmitCommandBuffer_Graphics(VkCommandBuffer commandBuffer,
                                          VkFence fence = VK_NULL_HANDLE) const
    {
        VkSubmitInfo submitInfo = {.commandBufferCount = 1, .pCommandBuffers = &commandBuffer};
        return SubmitCommandBuffer_Graphics(submitInfo, fence);
    }
    // 该函数用于将命令缓冲区提交到用于计算的队列
    result_t SubmitCommandBuffer_Compute(VkSubmitInfo &submitInfo,
                                         VkFence fence = VK_NULL_HANDLE) const
    {
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        VkResult result = vkQueueSubmit(queue_compute, 1, &submitInfo, fence);
        if (result)
            outStream << std::format(
                "[ graphicsBase ] ERROR\nFailed to submit the command buffer!\nError code: {}\n",
                string_VkResult(result));
        return result;
    }
    // 该函数用于将命令缓冲区提交到用于计算的队列，且只使用栅栏的常见情形
    result_t SubmitCommandBuffer_Compute(VkCommandBuffer commandBuffer,
                                         VkFence fence = VK_NULL_HANDLE) const
    {
        VkSubmitInfo submitInfo = {.commandBufferCount = 1, .pCommandBuffers = &commandBuffer};
        return SubmitCommandBuffer_Compute(submitInfo, fence);
    }
    result_t SubmitCommandBuffer_Presentation(
        VkCommandBuffer commandBuffer, VkSemaphore semaphore_renderingIsOver = VK_NULL_HANDLE,
        VkSemaphore semaphore_ownershipIsTransfered = VK_NULL_HANDLE,
        VkFence fence = VK_NULL_HANDLE) const
    {
        static constexpr VkPipelineStageFlags waitDstStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        VkSubmitInfo submitInfo = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                                   .commandBufferCount = 1,
                                   .pCommandBuffers = &commandBuffer};
        if (semaphore_renderingIsOver)
            submitInfo.waitSemaphoreCount = 1,
            submitInfo.pWaitSemaphores = &semaphore_renderingIsOver,
            submitInfo.pWaitDstStageMask = &waitDstStage;
        if (semaphore_ownershipIsTransfered)
            submitInfo.signalSemaphoreCount = 1,
            submitInfo.pSignalSemaphores = &semaphore_ownershipIsTransfered;
        VkResult result = vkQueueSubmit(queue_presentation, 1, &submitInfo, fence);
        if (result)
            outStream << std::format(
                "[ graphicsBase ] ERROR\nFailed to submit the presentation command buffer!\nError "
                "code: {}\n",
                string_VkResult(result));
        return result;
    }
    void CmdTransferImageOwnership(VkCommandBuffer commandBuffer) const
    {
        VkImageMemoryBarrier imageMemoryBarrier_g2p = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            .dstAccessMask = 0,
            .oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            .srcQueueFamilyIndex = queueFamilyIndex_graphics,
            .dstQueueFamilyIndex = queueFamilyIndex_presentation,
            .image = swapchainImages[currentImageIndex],
            .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                             VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1,
                             &imageMemoryBarrier_g2p);
    }
    result_t PresentImage(VkPresentInfoKHR &presentInfo)
    {
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        switch (VkResult result = vkQueuePresentKHR(queue_presentation, &presentInfo)) {
            case VK_SUCCESS:
                return VK_SUCCESS;
            case VK_SUBOPTIMAL_KHR:
            case VK_ERROR_OUT_OF_DATE_KHR:
                return RecreateSwapchain();
            default:
                outStream << std::format(
                    "[ graphicsBase ] ERROR\nFailed to queue the image for presentation!\nError "
                    "code: {}\n",
                    string_VkResult(result));
                return result;
        }
    }
    // 该函数用于在渲染循环中呈现图像的常见情形
    result_t PresentImage(VkSemaphore semaphore_renderingIsOver = VK_NULL_HANDLE)
    {
        VkPresentInfoKHR presentInfo = {
            .swapchainCount = 1, // 想要请求呈现图像的交换链数量
            .pSwapchains = &swapchain, // 想要请求呈现图像的交换链数量
            .pImageIndices = &currentImageIndex // 想要请求呈现的图像在交换链中的索引，这个数组元素与 pSwapchains 中的元素一一对应
        };
        if (semaphore_renderingIsOver)
            presentInfo.waitSemaphoreCount = 1, // 等待的信号量数组长度
            presentInfo.pWaitSemaphores = &semaphore_renderingIsOver; // 发出呈现请求 present request 前需要等待的信号量
        return PresentImage(presentInfo);
    }
    // Static Function
    static constexpr graphicsBase &Base()
    {
        return singleton;
    }
    static graphicsBasePlus &Plus()
    {
        return *singleton.pPlus;
    }
    static void Plus(graphicsBasePlus &plus)
    {
        if (!singleton.pPlus) singleton.pPlus = &plus;
    }
};
inline graphicsBase graphicsBase::singleton;

class fence {
    VkFence handle = VK_NULL_HANDLE;

public:
    // fence() = default;
    fence(VkFenceCreateInfo &createInfo)
    {
        Create(createInfo);
    }
    // 默认构造器创建未置位的栅栏
    fence(VkFenceCreateFlags flags = 0)
    {
        Create(flags);
    }
    fence(fence &&other) noexcept
    {
        MoveHandle;
    }
    ~fence()
    {
        DestroyHandleBy(vkDestroyFence);
    }
    // Getter
    DefineHandleTypeOperator;
    DefineAddressFunction;
    // Const Function
    result_t Wait() const
    {
        VkResult result =
            vkWaitForFences(graphicsBase::Base().Device(), 1, &handle, false, UINT64_MAX);
        if (result)
            outStream << std::format(
                "[ fence ] ERROR\nFailed to wait for the fence!\nError code: {}\n",
                string_VkResult(result));
        return result;
    }
    result_t Reset() const
    {
        VkResult result = vkResetFences(graphicsBase::Base().Device(), 1, &handle);
        if (result)
            outStream << std::format(
                "[ fence ] ERROR\nFailed to reset the fence!\nError code: {}\n",
                string_VkResult(result));
        return result;
    }
    // 因为“等待后立刻重置”的情形经常出现，定义此函数
    result_t WaitAndReset() const
    {
        VkResult result = Wait();
        result || (result = Reset());
        return result;
    }
    result_t Status() const
    {
        VkResult result = vkGetFenceStatus(graphicsBase::Base().Device(), handle);
        if (result < 0)  // vkGetFenceStatus(...)成功时有两种结果，所以不能仅仅判断result是否非0
            outStream << std::format(
                "[ fence ] ERROR\nFailed to get the status of the fence!\nError code: {}\n",
                string_VkResult(result));
        return result;
    }
    // Non-const Function
    result_t Create(VkFenceCreateInfo &createInfo)
    {
        createInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        VkResult result =
            vkCreateFence(graphicsBase::Base().Device(), &createInfo, nullptr, &handle);
        if (result)
            outStream << std::format("[ fence ] ERROR\nFailed to create a fence!\nError code: {}\n",
                                     string_VkResult(result));
        return result;
    }
    result_t Create(VkFenceCreateFlags flags = 0)
    {
        VkFenceCreateInfo createInfo = {.flags = flags};
        return Create(createInfo);
    }
};

class semaphore {
    VkSemaphore handle = VK_NULL_HANDLE;

public:
    // semaphore() = default;
    semaphore(VkSemaphoreCreateInfo &createInfo)
    {
        Create(createInfo);
    }
    // 默认构造器创建未置位的信号量
    semaphore(/*VkSemaphoreCreateFlags flags*/)
    {
        Create();
    }
    semaphore(semaphore &&other) noexcept
    {
        MoveHandle;
    }
    ~semaphore()
    {
        DestroyHandleBy(vkDestroySemaphore);
    }
    // Getter
    DefineHandleTypeOperator;
    DefineAddressFunction;
    // Non-const Function
    result_t Create(VkSemaphoreCreateInfo &createInfo)
    {
        createInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        VkResult result =
            vkCreateSemaphore(graphicsBase::Base().Device(), &createInfo, nullptr, &handle);
        if (result)
            outStream << std::format(
                "[ semaphore ] ERROR\nFailed to create a semaphore!\nError code: {}\n",
                string_VkResult(result));
        return result;
    }
    result_t Create(/*VkSemaphoreCreateFlags flags*/)
    {
        VkSemaphoreCreateInfo createInfo = {};
        return Create(createInfo);
    }
};

class commandBuffer {
    // 封装命令池的commandPool类负责分配和释放命令缓冲区，需要让其能访问私有成员handle
    friend class commandPool;
    VkCommandBuffer handle = VK_NULL_HANDLE;

public:
    commandBuffer() = default;
    commandBuffer(commandBuffer &&other) noexcept
    {
        MoveHandle;
    }
    // 因释放命令缓冲区的函数被我定义在封装命令池的commandPool类中，没析构器
    // Getter
    DefineHandleTypeOperator;
    DefineAddressFunction;
    // Const Function
    // 这里没给inheritanceInfo设定默认参数，因为C++标准中规定对空指针解引用是未定义行为（尽管运行期不必发生，且至少MSVC编译器允许这种代码），而我又一定要传引用而非指针，因而形成了两个Begin(...)
    result_t Begin(VkCommandBufferUsageFlags usageFlags,
                   VkCommandBufferInheritanceInfo &inheritanceInfo) const
    {
        inheritanceInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO;
        VkCommandBufferBeginInfo beginInfo = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                                              .flags = usageFlags,
                                              .pInheritanceInfo = &inheritanceInfo};
        VkResult result = vkBeginCommandBuffer(handle, &beginInfo);
        if (result)
            outStream << std::format(
                "[ commandBuffer ] ERROR\nFailed to begin a command buffer!\nError code: {}\n",
                string_VkResult(result));
        return result;
    }
    result_t Begin(VkCommandBufferUsageFlags usageFlags = 0) const
    {
        VkCommandBufferBeginInfo beginInfo = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .flags = usageFlags,
        };
        VkResult result = vkBeginCommandBuffer(handle, &beginInfo);
        if (result)
            outStream << std::format(
                "[ commandBuffer ] ERROR\nFailed to begin a command buffer!\nError code: {}\n",
                string_VkResult(result));
        return result;
    }
    result_t End() const
    {
        VkResult result = vkEndCommandBuffer(handle);
        if (result)
            outStream << std::format(
                "[ commandBuffer ] ERROR\nFailed to end a command buffer!\nError code: {}\n",
                string_VkResult(result));
        return result;
    }
};

class commandPool {
    VkCommandPool handle = VK_NULL_HANDLE;

public:
    commandPool() = default;
    commandPool(VkCommandPoolCreateInfo &createInfo)
    {
        Create(createInfo);
    }
    commandPool(uint32_t queueFamilyIndex, VkCommandPoolCreateFlags flags = 0)
    {
        Create(queueFamilyIndex, flags);
    }
    commandPool(commandPool &&other) noexcept
    {
        MoveHandle;
    }
    ~commandPool()
    {
        DestroyHandleBy(vkDestroyCommandPool);
    }
    // Getter
    DefineHandleTypeOperator;
    DefineAddressFunction;
    // Const Function
    result_t AllocateBuffers(arrayRef<VkCommandBuffer> buffers,
                             VkCommandBufferLevel level = VK_COMMAND_BUFFER_LEVEL_PRIMARY) const
    {
        VkCommandBufferAllocateInfo allocateInfo = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool = handle, // 从这个 command pool 中申j
            .level = level, // 指定 command buffer 的级别，primary 级别的 cmd buffer 可以直接提交到队列，secondary 级别的 cmd buffer 不能直接提交到队列，只能被 primary 级别的 cmd buffer 调用 vkCmdExecuteCommands 时执行
            .commandBufferCount = uint32_t(buffers.Count()) // 从 command pool 中分配的 command buffer 的数量
        };
        VkResult result = vkAllocateCommandBuffers(graphicsBase::Base().Device(), &allocateInfo,
                                                   buffers.Pointer());
        if (result)
            outStream << std::format(
                "[ commandPool ] ERROR\nFailed to allocate command buffers!\nError code: {}\n",
                string_VkResult(result));
        return result;
    }
    result_t AllocateBuffers(arrayRef<commandBuffer> buffers,
                             VkCommandBufferLevel level = VK_COMMAND_BUFFER_LEVEL_PRIMARY) const
    {
        return AllocateBuffers({&buffers[0].handle, buffers.Count()}, level);
    }
    void FreeBuffers(arrayRef<VkCommandBuffer> buffers) const
    {
        vkFreeCommandBuffers(graphicsBase::Base().Device(), handle, buffers.Count(),
                             buffers.Pointer());
        memset(buffers.Pointer(), 0, buffers.Count() * sizeof(VkCommandBuffer));
    }
    void FreeBuffers(arrayRef<commandBuffer> buffers) const
    {
        FreeBuffers({&buffers[0].handle, buffers.Count()});
    }
    void Trim(/*VkCommandPoolTrimFlags flags*/) const
    {
        vkTrimCommandPool(graphicsBase::Base().Device(), handle, 0);
    }
    // Non-const Function
    result_t Create(VkCommandPoolCreateInfo &createInfo)
    {
        createInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        VkResult result =
            vkCreateCommandPool(graphicsBase::Base().Device(), &createInfo, nullptr, &handle);
        if (result)
            outStream << std::format(
                "[ commandPool ] ERROR\nFailed to create a command pool!\nError code: {}\n",
                string_VkResult(result));
        return result;
    }
    result_t Create(uint32_t queueFamilyIndex, VkCommandPoolCreateFlags flags = 0)
    {
        VkCommandPoolCreateInfo createInfo = {.flags = flags, // VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT 表示允许将从池中分配的任何 command buffer 单独重置为初始状态；
                                                              // 可以通过调用 vkResetCommandBuffer 或通过调用 vkBeginCommandBuffer 时的隐式重置来实现。
                                                              // 如果池未设置此标志，则不得对从该池中分配的任何命令缓冲区调用 vkResetCommandBuffer。
                                              .queueFamilyIndex = queueFamilyIndex // 指定一个队列族，从这个 command pool 分配的所有 command buffer必须提交到同一队列族中的队列。
                                            };
        return Create(createInfo);
    }
};

class renderPass {
    VkRenderPass handle = VK_NULL_HANDLE;

public:
    renderPass() = default;
    renderPass(VkRenderPassCreateInfo &createInfo)
    {
        Create(createInfo);
    }
    renderPass(renderPass &&other) noexcept
    {
        MoveHandle;
    }
    ~renderPass()
    {
        DestroyHandleBy(vkDestroyRenderPass);
    }
    // Getter
    DefineHandleTypeOperator;
    DefineAddressFunction;
    // Const Function
    void CmdBegin(VkCommandBuffer commandBuffer, VkRenderPassBeginInfo &beginInfo,
                  VkSubpassContents subpassContents = VK_SUBPASS_CONTENTS_INLINE) const
    {
        beginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        beginInfo.renderPass = handle;
        vkCmdBeginRenderPass(commandBuffer, &beginInfo, subpassContents);
    }
    void CmdBegin(VkCommandBuffer commandBuffer, VkFramebuffer framebuffer, VkRect2D renderArea,
                  arrayRef<const VkClearValue> clearValues = {},
                  VkSubpassContents subpassContents = VK_SUBPASS_CONTENTS_INLINE) const
    {
        VkRenderPassBeginInfo beginInfo = {.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
                                           .renderPass = handle,
                                           .framebuffer = framebuffer,
                                           .renderArea = renderArea,
                                           .clearValueCount = uint32_t(clearValues.Count()),
                                           .pClearValues = clearValues.Pointer()};
        vkCmdBeginRenderPass(commandBuffer, &beginInfo, subpassContents);
    }
    void CmdNext(VkCommandBuffer commandBuffer,
                 VkSubpassContents subpassContents = VK_SUBPASS_CONTENTS_INLINE) const
    {
        vkCmdNextSubpass(commandBuffer, subpassContents);
    }
    void CmdEnd(VkCommandBuffer commandBuffer) const
    {
        vkCmdEndRenderPass(commandBuffer);
    }
    // Non-const Function
    result_t Create(VkRenderPassCreateInfo &createInfo)
    {
        createInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        VkResult result =
            vkCreateRenderPass(graphicsBase::Base().Device(), &createInfo, nullptr, &handle);
        if (result)
            outStream << std::format(
                "[ renderPass ] ERROR\nFailed to create a render pass!\nError code: {}\n",
                string_VkResult(result));
        return result;
    }
};

class framebuffer {
    VkFramebuffer handle = VK_NULL_HANDLE;

public:
    framebuffer() = default;
    framebuffer(VkFramebufferCreateInfo &createInfo)
    {
        Create(createInfo);
    }
    framebuffer(framebuffer &&other) noexcept
    {
        MoveHandle;
    }
    ~framebuffer()
    {
        DestroyHandleBy(vkDestroyFramebuffer);
    }
    // Getter
    DefineHandleTypeOperator;
    DefineAddressFunction;
    // Non-const Function
    result_t Create(VkFramebufferCreateInfo &createInfo)
    {
        createInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        VkResult result =
            vkCreateFramebuffer(graphicsBase::Base().Device(), &createInfo, nullptr, &handle);
        if (result)
            outStream << std::format(
                "[ framebuffer ] ERROR\nFailed to create a framebuffer!\nError code: {}\n",
                string_VkResult(result));
        return result;
    }
};

class shaderModule {
    VkShaderModule handle = VK_NULL_HANDLE;

public:
    shaderModule() = default;
    shaderModule(VkShaderModuleCreateInfo &createInfo)
    {
        Create(createInfo);
    }
    shaderModule(const char *filepath /*VkShaderModuleCreateFlags flags*/)
    {
        Create(filepath);
    }
    shaderModule(size_t codeSize, const uint32_t *pCode /*VkShaderModuleCreateFlags flags*/)
    {
        Create(codeSize, pCode);
    }
    shaderModule(shaderModule &&other) noexcept
    {
        MoveHandle;
    }
    ~shaderModule()
    {
        DestroyHandleBy(vkDestroyShaderModule);
    }
    // Getter
    DefineHandleTypeOperator;
    DefineAddressFunction;
    // Const Function
    VkPipelineShaderStageCreateInfo StageCreateInfo(VkShaderStageFlagBits stage,
                                                    const char *entry = "main") const
    {
        return {
            VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,  // sType
            nullptr,                                              // pNext
            0,                                                    // flags
            stage,                                                // stage
            handle,                                               // module
            entry,                                                // pName
            nullptr                                               // pSpecializationInfo
        };
    }
    // Non-const Function
    result_t Create(VkShaderModuleCreateInfo &createInfo)
    {
        createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        // 创建 shader module, 可用于创建 pipeline 的创建 shader stage 信息中
        VkResult result =
            vkCreateShaderModule(graphicsBase::Base().Device(), &createInfo, nullptr, &handle);
        if (result)
            outStream << std::format(
                "[ shader ] ERROR\nFailed to create a shader module!\nError code: {}\n",
                string_VkResult(result));
        return result;
    }
    result_t Create(const char *filepath /*VkShaderModuleCreateFlags flags*/)
    {
        std::ifstream file(filepath, std::ios::ate | std::ios::binary);
        if (!file) {
            outStream << std::format("[ shader ] ERROR\nFailed to open the file: {}\n", filepath);
            return VK_RESULT_MAX_ENUM;  // 没有合适的错误代码，别用VK_ERROR_UNKNOWN
        }
        size_t fileSize = size_t(file.tellg());
        std::vector<uint32_t> binaries(fileSize / 4);
        file.seekg(0);
        file.read(reinterpret_cast<char *>(binaries.data()), fileSize);
        file.close();
        return Create(fileSize, binaries.data());
    }
    result_t Create(size_t codeSize, const uint32_t *pCode /*VkShaderModuleCreateFlags flags*/)
    {
        VkShaderModuleCreateInfo createInfo = {.codeSize = codeSize, .pCode = pCode};
        return Create(createInfo);
    }
};

class pipelineLayout {
    VkPipelineLayout handle = VK_NULL_HANDLE;

public:
    pipelineLayout() = default;
    pipelineLayout(VkPipelineLayoutCreateInfo &createInfo)
    {
        Create(createInfo);
    }
    pipelineLayout(pipelineLayout &&other) noexcept
    {
        MoveHandle;
    }
    ~pipelineLayout()
    {
        DestroyHandleBy(vkDestroyPipelineLayout);
    }
    // Getter
    DefineHandleTypeOperator;
    DefineAddressFunction;
    // Non-const Function
    result_t Create(VkPipelineLayoutCreateInfo &createInfo)
    {
        createInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        VkResult result =
            vkCreatePipelineLayout(graphicsBase::Base().Device(), &createInfo, nullptr, &handle);
        if (result)
            outStream << std::format(
                "[ pipelineLayout ] ERROR\nFailed to create a pipeline layout!\nError code: {}\n",
                string_VkResult(result));
        return result;
    }
};

class pipeline {
    VkPipeline handle = VK_NULL_HANDLE;

public:
    pipeline() = default;
    pipeline(VkGraphicsPipelineCreateInfo &createInfo)
    {
        Create(createInfo);
    }
    pipeline(VkComputePipelineCreateInfo &createInfo)
    {
        Create(createInfo);
    }
    pipeline(pipeline &&other) noexcept
    {
        MoveHandle;
    }
    ~pipeline()
    {
        DestroyHandleBy(vkDestroyPipeline);
    }
    // Getter
    DefineHandleTypeOperator;
    DefineAddressFunction;
    // Non-const Function
    result_t Create(VkGraphicsPipelineCreateInfo &createInfo)
    {
        createInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        VkResult result = vkCreateGraphicsPipelines(graphicsBase::Base().Device(), VK_NULL_HANDLE,
                                                    1, &createInfo, nullptr, &handle);
        if (result)
            outStream << std::format(
                "[ pipeline ] ERROR\nFailed to create a graphics pipeline!\nError code: {}\n",
                string_VkResult(result));
        return result;
    }
    result_t Create(VkComputePipelineCreateInfo &createInfo)
    {
        createInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        VkResult result = vkCreateComputePipelines(graphicsBase::Base().Device(), VK_NULL_HANDLE, 1,
                                                   &createInfo, nullptr, &handle);
        if (result)
            outStream << std::format(
                "[ pipeline ] ERROR\nFailed to create a compute pipeline!\nError code: {}\n",
                string_VkResult(result));
        return result;
    }
};

class deviceMemory {
    VkDeviceMemory handle = VK_NULL_HANDLE;
    VkDeviceSize allocationSize = 0;             // 实际分配的内存大小
    VkMemoryPropertyFlags memoryProperties = 0;  // 内存属性
    //--------------------
    // 该函数用于在映射内存区时，调整非host-coherent的内存区域的范围
    VkDeviceSize AdjustNonCoherentMemoryRange(VkDeviceSize &size, VkDeviceSize &offset) const
    {
        const VkDeviceSize &nonCoherentAtomSize =
            graphicsBase::Base().PhysicalDeviceProperties().limits.nonCoherentAtomSize;
        VkDeviceSize _offset = offset;
        offset = offset / nonCoherentAtomSize * nonCoherentAtomSize;
        size = std::min((size + _offset + nonCoherentAtomSize - 1) / nonCoherentAtomSize *
                            nonCoherentAtomSize,
                        allocationSize) -
               offset;
        return _offset - offset;
    }

protected:
    // 用于bufferMemory或imageMemory，定义于此以节省8个字节
    class {
        friend class bufferMemory;
        friend class imageMemory;
        bool value = false;
        operator bool() const
        {
            return value;
        }
        auto &operator=(bool value)
        {
            this->value = value;
            return *this;
        }
    } areBound;

public:
    deviceMemory() = default;
    deviceMemory(VkMemoryAllocateInfo &allocateInfo)
    {
        Allocate(allocateInfo);
    }
    deviceMemory(deviceMemory &&other) noexcept
    {
        MoveHandle;
        allocationSize = other.allocationSize;
        memoryProperties = other.memoryProperties;
        other.allocationSize = 0;
        other.memoryProperties = 0;
    }
    ~deviceMemory()
    {
        DestroyHandleBy(vkFreeMemory);
        allocationSize = 0;
        memoryProperties = 0;
    }
    // Getter
    DefineHandleTypeOperator;
    DefineAddressFunction;
    VkDeviceSize AllocationSize() const
    {
        return allocationSize;
    }
    VkMemoryPropertyFlags MemoryProperties() const
    {
        return memoryProperties;
    }
    // Const Function
    // 映射host-visible的内存区
    result_t MapMemory(void *&pData, VkDeviceSize size, VkDeviceSize offset = 0) const
    {
        VkDeviceSize inverseDeltaOffset;
        if (!(memoryProperties & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
            inverseDeltaOffset = AdjustNonCoherentMemoryRange(size, offset);
        if (VkResult result =
                vkMapMemory(graphicsBase::Base().Device(), handle, offset, size, 0, &pData)) {
            outStream << std::format(
                "[ deviceMemory ] ERROR\nFailed to map the memory!\nError code: {}\n",
                string_VkResult(result));
            return result;
        }
        if (!(memoryProperties & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
            pData = static_cast<uint8_t *>(pData) + inverseDeltaOffset;
            VkMappedMemoryRange mappedMemoryRange = {.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
                                                     .memory = handle,
                                                     .offset = offset,
                                                     .size = size};
            if (VkResult result = vkInvalidateMappedMemoryRanges(graphicsBase::Base().Device(), 1,
                                                                 &mappedMemoryRange)) {
                outStream << std::format(
                    "[ deviceMemory ] ERROR\nFailed to flush the memory!\nError code: {}\n",
                    string_VkResult(result));
                return result;
            }
        }
        return VK_SUCCESS;
    }
    // 取消映射host-visible的内存区
    result_t UnmapMemory(VkDeviceSize size, VkDeviceSize offset = 0) const
    {
        if (!(memoryProperties & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
            AdjustNonCoherentMemoryRange(size, offset);
            VkMappedMemoryRange mappedMemoryRange = {.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
                                                     .memory = handle,
                                                     .offset = offset,
                                                     .size = size};
            if (VkResult result = vkFlushMappedMemoryRanges(graphicsBase::Base().Device(), 1,
                                                            &mappedMemoryRange)) {
                outStream << std::format(
                    "[ deviceMemory ] ERROR\nFailed to flush the memory!\nError code: {}\n",
                    string_VkResult(result));
                return result;
            }
        }
        vkUnmapMemory(graphicsBase::Base().Device(), handle);
        return VK_SUCCESS;
    }
    // BufferData(...)用于方便地更新设备内存区，适用于用memcpy(...)向内存区写入数据后立刻取消映射的情况
    result_t BufferData(const void *pData_src, VkDeviceSize size, VkDeviceSize offset = 0) const
    {
        void *pData_dst;
        if (VkResult result = MapMemory(pData_dst, size, offset)) return result;
        memcpy(pData_dst, pData_src, size_t(size));
        return UnmapMemory(size, offset);
    }
    result_t BufferData(const auto &data_src) const
    {
        return BufferData(&data_src, sizeof data_src);
    }
    // RetrieveData(...)用于方便地从设备内存区取回数据，适用于用memcpy(...)从内存区取得数据后立刻取消映射的情况
    result_t RetrieveData(void *pData_dst, VkDeviceSize size, VkDeviceSize offset = 0) const
    {
        void *pData_src;
        if (VkResult result = MapMemory(pData_src, size, offset)) return result;
        memcpy(pData_dst, pData_src, size_t(size));
        return UnmapMemory(size, offset);
    }
    // Non-const Function
    result_t Allocate(VkMemoryAllocateInfo &allocateInfo)
    {
        if (allocateInfo.memoryTypeIndex >=
            graphicsBase::Base().PhysicalDeviceMemoryProperties().memoryTypeCount) {
            outStream << std::format("[ deviceMemory ] ERROR\nInvalid memory type index!\n");
            return VK_RESULT_MAX_ENUM;  // 没有合适的错误代码，别用VK_ERROR_UNKNOWN
        }
        allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        if (VkResult result =
                vkAllocateMemory(graphicsBase::Base().Device(), &allocateInfo, nullptr, &handle)) {
            outStream << std::format(
                "[ deviceMemory ] ERROR\nFailed to allocate memory!\nError code: {}\n",
                string_VkResult(result));
            return result;
        }
        // 记录实际分配的内存大小
        allocationSize = allocateInfo.allocationSize;
        // 取得内存属性
        memoryProperties = graphicsBase::Base()
                               .PhysicalDeviceMemoryProperties()
                               .memoryTypes[allocateInfo.memoryTypeIndex]
                               .propertyFlags;
        return VK_SUCCESS;
    }
};

class buffer {
    VkBuffer handle = VK_NULL_HANDLE;

public:
    buffer() = default;
    buffer(VkBufferCreateInfo &createInfo)
    {
        Create(createInfo);
    }
    buffer(buffer &&other) noexcept
    {
        MoveHandle;
    }
    ~buffer()
    {
        DestroyHandleBy(vkDestroyBuffer);
    }
    // Getter
    DefineHandleTypeOperator;
    DefineAddressFunction;
    // Const Function
    VkMemoryAllocateInfo MemoryAllocateInfo(VkMemoryPropertyFlags desiredMemoryProperties) const
    {
        VkMemoryAllocateInfo memoryAllocateInfo = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        VkMemoryRequirements memoryRequirements;
        vkGetBufferMemoryRequirements(graphicsBase::Base().Device(), handle, &memoryRequirements);
        memoryAllocateInfo.allocationSize = memoryRequirements.size;
        memoryAllocateInfo.memoryTypeIndex = UINT32_MAX;
        auto &physicalDeviceMemoryProperties =
            graphicsBase::Base().PhysicalDeviceMemoryProperties();
        for (size_t i = 0; i < physicalDeviceMemoryProperties.memoryTypeCount; i++)
            if (memoryRequirements.memoryTypeBits & 1 << i &&
                (physicalDeviceMemoryProperties.memoryTypes[i].propertyFlags &
                 desiredMemoryProperties) == desiredMemoryProperties) {
                memoryAllocateInfo.memoryTypeIndex = i;
                break;
            }
        // 不在此检查是否成功取得内存类型索引，因为会把memoryAllocateInfo返回出去，交由外部检查
        // if (memoryAllocateInfo.memoryTypeIndex == UINT32_MAX)
        //     outStream << std::format("[ buffer ] ERROR\nFailed to find any memory type satisfies
        //     all desired memory properties!\n");
        return memoryAllocateInfo;
    }
    result_t BindMemory(VkDeviceMemory deviceMemory, VkDeviceSize memoryOffset = 0) const
    {
        VkResult result =
            vkBindBufferMemory(graphicsBase::Base().Device(), handle, deviceMemory, memoryOffset);
        if (result)
            outStream << std::format(
                "[ buffer ] ERROR\nFailed to attach the memory!\nError code: {}\n",
                string_VkResult(result));
        return result;
    }
    // Non-const Function
    result_t Create(VkBufferCreateInfo &createInfo)
    {
        createInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        VkResult result =
            vkCreateBuffer(graphicsBase::Base().Device(), &createInfo, nullptr, &handle);
        if (result)
            outStream << std::format(
                "[ buffer ] ERROR\nFailed to create a buffer!\nError code: {}\n",
                string_VkResult(result));
        return result;
    }
};

class bufferMemory : buffer, deviceMemory {
public:
    bufferMemory() = default;
    bufferMemory(VkBufferCreateInfo &createInfo, VkMemoryPropertyFlags desiredMemoryProperties)
    {
        Create(createInfo, desiredMemoryProperties);
    }
    bufferMemory(bufferMemory &&other) noexcept
        : buffer(std::move(other)), deviceMemory(std::move(other))
    {
        areBound = other.areBound;
        other.areBound = false;
    }
    ~bufferMemory()
    {
        areBound = false;
    }
    // Getter
    // 不定义到VkBuffer和VkDeviceMemory的转换函数，因为32位下这俩类型都是uint64_t的别名，会造成冲突（虽然，谁他妈还用32位PC！）
    VkBuffer Buffer() const
    {
        return static_cast<const buffer &>(*this);
    }
    const VkBuffer *AddressOfBuffer() const
    {
        return buffer::Address();
    }
    VkDeviceMemory Memory() const
    {
        return static_cast<const deviceMemory &>(*this);
    }
    const VkDeviceMemory *AddressOfMemory() const
    {
        return deviceMemory::Address();
    }
    // 若areBond为true，则成功分配了设备内存、创建了缓冲区，且成功绑定在一起
    bool AreBound() const
    {
        return areBound;
    }
    using deviceMemory::AllocationSize;
    using deviceMemory::MemoryProperties;
    // Const Function
    using deviceMemory::BufferData;
    using deviceMemory::MapMemory;
    using deviceMemory::RetrieveData;
    using deviceMemory::UnmapMemory;
    // Non-const Function
    // 以下三个函数仅用于Create(...)可能执行失败的情况
    result_t CreateBuffer(VkBufferCreateInfo &createInfo)
    {
        return buffer::Create(createInfo);
    }
    result_t AllocateMemory(VkMemoryPropertyFlags desiredMemoryProperties)
    {
        VkMemoryAllocateInfo allocateInfo = MemoryAllocateInfo(desiredMemoryProperties);
        if (allocateInfo.memoryTypeIndex >=
            graphicsBase::Base().PhysicalDeviceMemoryProperties().memoryTypeCount)
            return VK_RESULT_MAX_ENUM;  // 没有合适的错误代码，别用VK_ERROR_UNKNOWN
        return Allocate(allocateInfo);
    }
    result_t BindMemory()
    {
        if (VkResult result = buffer::BindMemory(Memory())) return result;
        areBound = true;
        return VK_SUCCESS;
    }
    // 分配设备内存、创建缓冲、绑定
    result_t Create(VkBufferCreateInfo &createInfo, VkMemoryPropertyFlags desiredMemoryProperties)
    {
        VkResult result;
        false ||                                    // 这行用来应对Visual Studio中代码的对齐
            (result = CreateBuffer(createInfo)) ||  // 用||短路执行
            (result = AllocateMemory(desiredMemoryProperties)) || (result = BindMemory());
        return result;
    }
};

class bufferView {
    VkBufferView handle = VK_NULL_HANDLE;

public:
    bufferView() = default;
    bufferView(VkBufferViewCreateInfo &createInfo)
    {
        Create(createInfo);
    }
    bufferView(VkBuffer buffer, VkFormat format, VkDeviceSize offset = 0,
               VkDeviceSize range = 0 /*VkBufferViewCreateFlags flags*/)
    {
        Create(buffer, format, offset, range);
    }
    bufferView(bufferView &&other) noexcept
    {
        MoveHandle;
    }
    ~bufferView()
    {
        DestroyHandleBy(vkDestroyBufferView);
    }
    // Getter
    DefineHandleTypeOperator;
    DefineAddressFunction;
    // Non-const Function
    result_t Create(VkBufferViewCreateInfo &createInfo)
    {
        createInfo.sType = VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO;
        VkResult result =
            vkCreateBufferView(graphicsBase::Base().Device(), &createInfo, nullptr, &handle);
        if (result)
            outStream << std::format(
                "[ bufferView ] ERROR\nFailed to create a buffer view!\nError code: {}\n",
                string_VkResult(result));
        return result;
    }
    result_t Create(VkBuffer buffer, VkFormat format, VkDeviceSize offset = 0,
                    VkDeviceSize range = 0 /*VkBufferViewCreateFlags flags*/)
    {
        VkBufferViewCreateInfo createInfo = {
            .buffer = buffer, .format = format, .offset = offset, .range = range};
        return Create(createInfo);
    }
};

class image {
    VkImage handle = VK_NULL_HANDLE;

public:
    image() = default;
    image(VkImageCreateInfo &createInfo)
    {
        Create(createInfo);
    }
    image(image &&other) noexcept
    {
        MoveHandle;
    }
    ~image()
    {
        DestroyHandleBy(vkDestroyImage);
    }
    // Getter
    DefineHandleTypeOperator;
    DefineAddressFunction;
    // Const Function
    VkMemoryAllocateInfo MemoryAllocateInfo(VkMemoryPropertyFlags desiredMemoryProperties) const
    {
        VkMemoryAllocateInfo memoryAllocateInfo = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        VkMemoryRequirements memoryRequirements;
        vkGetImageMemoryRequirements(graphicsBase::Base().Device(), handle, &memoryRequirements);
        memoryAllocateInfo.allocationSize = memoryRequirements.size;
        auto GetMemoryTypeIndex = [](uint32_t memoryTypeBits,
                                     VkMemoryPropertyFlags desiredMemoryProperties) -> size_t {
            auto &physicalDeviceMemoryProperties =
                graphicsBase::Base().PhysicalDeviceMemoryProperties();
            for (size_t i = 0; i < physicalDeviceMemoryProperties.memoryTypeCount; i++)
                if (memoryTypeBits & 1 << i &&
                    (physicalDeviceMemoryProperties.memoryTypes[i].propertyFlags &
                     desiredMemoryProperties) == desiredMemoryProperties)
                    return i;
            return UINT32_MAX;
        };
        memoryAllocateInfo.memoryTypeIndex =
            GetMemoryTypeIndex(memoryRequirements.memoryTypeBits, desiredMemoryProperties);
        if (memoryAllocateInfo.memoryTypeIndex == UINT32_MAX &&
            desiredMemoryProperties & VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT)
            memoryAllocateInfo.memoryTypeIndex = GetMemoryTypeIndex(
                memoryRequirements.memoryTypeBits,
                desiredMemoryProperties & ~VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT);
        // 不在此检查是否成功取得内存类型索引，因为会把memoryAllocateInfo返回出去，交由外部检查
        // if (memoryAllocateInfo.memoryTypeIndex == -1)
        //     outStream << std::format("[ image ] ERROR\nFailed to find any memory type satisfies
        //     all desired memory properties!\n");
        return memoryAllocateInfo;
    }
    result_t BindMemory(VkDeviceMemory deviceMemory, VkDeviceSize memoryOffset = 0) const
    {
        VkResult result =
            vkBindImageMemory(graphicsBase::Base().Device(), handle, deviceMemory, memoryOffset);
        if (result)
            outStream << std::format(
                "[ image ] ERROR\nFailed to attach the memory!\nError code: {}\n",
                string_VkResult(result));
        return result;
    }
    // Non-const Function
    result_t Create(VkImageCreateInfo &createInfo)
    {
        createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        VkResult result =
            vkCreateImage(graphicsBase::Base().Device(), &createInfo, nullptr, &handle);
        if (result)
            outStream << std::format(
                "[ image ] ERROR\nFailed to create an image!\nError code: {}\n",
                string_VkResult(result));
        return result;
    }
};

class imageMemory : image, deviceMemory {
public:
    imageMemory() = default;
    imageMemory(VkImageCreateInfo &createInfo, VkMemoryPropertyFlags desiredMemoryProperties)
    {
        Create(createInfo, desiredMemoryProperties);
    }
    imageMemory(imageMemory &&other) noexcept
        : image(std::move(other)), deviceMemory(std::move(other))
    {
        areBound = other.areBound;
        other.areBound = false;
    }
    ~imageMemory()
    {
        areBound = false;
    }
    // Getter
    VkImage Image() const
    {
        return static_cast<const image &>(*this);
    }
    const VkImage *AddressOfImage() const
    {
        return image::Address();
    }
    VkDeviceMemory Memory() const
    {
        return static_cast<const deviceMemory &>(*this);
    }
    const VkDeviceMemory *AddressOfMemory() const
    {
        return deviceMemory::Address();
    }
    bool AreBound() const
    {
        return areBound;
    }
    using deviceMemory::AllocationSize;
    using deviceMemory::MemoryProperties;
    // Non-const Function
    // 以下三个函数仅用于Create(...)可能执行失败的情况
    result_t CreateImage(VkImageCreateInfo &createInfo)
    {
        return image::Create(createInfo);
    }
    result_t AllocateMemory(VkMemoryPropertyFlags desiredMemoryProperties)
    {
        VkMemoryAllocateInfo allocateInfo = MemoryAllocateInfo(desiredMemoryProperties);
        if (allocateInfo.memoryTypeIndex >=
            graphicsBase::Base().PhysicalDeviceMemoryProperties().memoryTypeCount)
            return VK_RESULT_MAX_ENUM;  // 没有合适的错误代码，别用VK_ERROR_UNKNOWN
        return Allocate(allocateInfo);
    }
    result_t BindMemory()
    {
        if (VkResult result = image::BindMemory(Memory())) return result;
        areBound = true;
        return VK_SUCCESS;
    }
    // 分配设备内存、创建图像、绑定
    result_t Create(VkImageCreateInfo &createInfo, VkMemoryPropertyFlags desiredMemoryProperties)
    {
        VkResult result;
        false ||                                   // 这行用来应对Visual Studio中代码的对齐
            (result = CreateImage(createInfo)) ||  // 用||短路执行
            (result = AllocateMemory(desiredMemoryProperties)) || (result = BindMemory());
        return result;
    }
};

class imageView {
    VkImageView handle = VK_NULL_HANDLE;

public:
    imageView() = default;
    imageView(VkImageViewCreateInfo &createInfo)
    {
        Create(createInfo);
    }
    imageView(VkImage image, VkImageViewType viewType, VkFormat format,
              const VkImageSubresourceRange &subresourceRange, VkImageViewCreateFlags flags = 0)
    {
        Create(image, viewType, format, subresourceRange, flags);
    }
    imageView(imageView &&other) noexcept
    {
        MoveHandle;
    }
    ~imageView()
    {
        DestroyHandleBy(vkDestroyImageView);
    }
    // Getter
    DefineHandleTypeOperator;
    DefineAddressFunction;
    // Non-const Function
    result_t Create(VkImageViewCreateInfo &createInfo)
    {
        createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        VkResult result =
            vkCreateImageView(graphicsBase::Base().Device(), &createInfo, nullptr, &handle);
        if (result)
            outStream << std::format(
                "[ imageView ] ERROR\nFailed to create an image view!\nError code: {}\n",
                string_VkResult(result));
        return result;
    }
    result_t Create(VkImage image, VkImageViewType viewType, VkFormat format,
                    const VkImageSubresourceRange &subresourceRange,
                    VkImageViewCreateFlags flags = 0)
    {
        VkImageViewCreateInfo createInfo = {.flags = flags,
                                            .image = image,
                                            .viewType = viewType,
                                            .format = format,
                                            .subresourceRange = subresourceRange};
        return Create(createInfo);
    }
};

}  // namespace vulkan

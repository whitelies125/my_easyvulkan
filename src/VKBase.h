#include "EasyVKStart.h"

namespace vulkan {
constexpr VkExtent2D defaultWindowSize = {1280, 720};

class graphicsBase {
    uint32_t apiVersion = VK_API_VERSION_1_0;
    VkInstance instance;
    VkPhysicalDevice physicalDevice;
    VkPhysicalDeviceProperties physicalDeviceProperties;
    VkPhysicalDeviceMemoryProperties physicalDeviceMemoryProperties;
    std::vector<VkPhysicalDevice> availablePhysicalDevices;

    VkDevice device;
    uint32_t queueFamilyIndex_graphics = VK_QUEUE_FAMILY_IGNORED;
    uint32_t queueFamilyIndex_presentation = VK_QUEUE_FAMILY_IGNORED;
    uint32_t queueFamilyIndex_compute = VK_QUEUE_FAMILY_IGNORED;
    VkQueue queue_graphics;
    VkQueue queue_presentation;
    VkQueue queue_compute;

    VkSurfaceKHR surface;
    std::vector<VkSurfaceFormatKHR> availableSurfaceFormats;

    VkSwapchainKHR swapchain;
    std::vector<VkImage> swapchainImages;
    std::vector<VkImageView> swapchainImageViews;
    VkSwapchainCreateInfoKHR swapchainCreateInfo = {};

    std::vector<const char*> instanceLayers;
    std::vector<const char*> instanceExtensions;
    std::vector<const char*> deviceExtensions;

    VkDebugUtilsMessengerEXT debugMessenger;

    std::vector<void (*)()> callbacks_createSwapchain;
    std::vector<void (*)()> callbacks_destroySwapchain;
    std::vector<void (*)()> callbacks_createDevice;
    std::vector<void (*)()> callbacks_destroyDevice;
    // Static
    static graphicsBase singleton;
    //--------------------
    graphicsBase() = default;
    graphicsBase(graphicsBase&&) = delete;
    ~graphicsBase()
    {
        if (!instance) return;
        if (device) {
            WaitIdle();
            if (swapchain) {
                for (auto& i : callbacks_destroySwapchain) i();
                for (auto& i : swapchainImageViews)
                    if (i) vkDestroyImageView(device, i, nullptr);
                vkDestroySwapchainKHR(device, swapchain, nullptr);
            }
            for (auto& i : callbacks_destroyDevice) i();
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
    VkResult GetQueueFamilyIndices(VkPhysicalDevice physicalDevice, bool enableGraphicsQueue,
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
        auto& [ig, ip, ic] = queueFamilyIndices;
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
    VkResult CreateSwapchain_Internal()
    {
        if (VkResult result =
                vkCreateSwapchainKHR(device, &swapchainCreateInfo, nullptr, &swapchain)) {
            std::cout << std::format(
                "[ graphicsBase ] ERROR\nFailed to create a swapchain!\nError code: {}\n",
                int32_t(result));
            return result;
        }

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
    VkResult CreateDebugMessenger()
    {
        static PFN_vkDebugUtilsMessengerCallbackEXT DebugUtilsMessengerCallback =
            [](VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
               VkDebugUtilsMessageTypeFlagsEXT messageTypes,
               const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
               void* pUserData) -> VkBool32 {
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
    static void AddLayerOrExtension(std::vector<const char*>& container, const char* name)
    {
        for (auto& i : container)
            if (!strcmp(name, i)) return;
        container.push_back(name);
    }
    static void ExecuteCallbacks(std::vector<void (*)()>& callbacks)
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
    constexpr const VkPhysicalDeviceProperties& PhysicalDeviceProperties() const
    {
        return physicalDeviceProperties;
    }
    constexpr const VkPhysicalDeviceMemoryProperties& PhysicalDeviceMemoryProperties() const
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
    constexpr const VkSwapchainCreateInfoKHR& SwapchainCreateInfo() const
    {
        return swapchainCreateInfo;
    }

    const std::vector<const char*>& InstanceLayers() const
    {
        return instanceLayers;
    }
    const std::vector<const char*>& InstanceExtensions() const
    {
        return instanceExtensions;
    }
    const std::vector<const char*>& DeviceExtensions() const
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
    void AddInstanceLayer(const char* layerName)
    {
        AddLayerOrExtension(instanceLayers, layerName);
    }
    void AddInstanceExtension(const char* extensionName)
    {
        AddLayerOrExtension(instanceExtensions, extensionName);
    }
    VkResult UseLatestApiVersion()
    {
        if (vkGetInstanceProcAddr(VK_NULL_HANDLE, "vkEnumerateInstanceVersion"))
            return vkEnumerateInstanceVersion(&apiVersion);
        return VK_SUCCESS;
    }
    VkResult CreateInstance(VkInstanceCreateFlags flags = 0)
    {
#ifndef NDEBUG
        // 添加 layer，启用 验证层
        AddInstanceLayer("VK_LAYER_KHRONOS_validation");
        // 添加 extension，该扩展可使开发者可以获得更多信息，例如可以注册回调函数获取 debug 信息
        AddInstanceExtension(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
#endif
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
#ifndef NDEBUG
        // 用于获取验证层捕获到的 debug 信息
        CreateDebugMessenger();
#endif
        return VK_SUCCESS;
    }
    VkResult CheckInstanceLayers(std::span<const char*> layersToCheck) const
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
            for (auto& i : layersToCheck) {
                bool found = false;
                for (auto& j : availableLayers)
                    if (!strcmp(i, j.layerName)) {
                        found = true;
                        break;
                    }
                if (!found) i = nullptr;
            }
        } else
            for (auto& i : layersToCheck) i = nullptr;
        return VK_SUCCESS;
    }
    VkResult CheckInstanceExtensions(std::span<const char*> extensionsToCheck,
                                     const char* layerName) const
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
            for (auto& i : extensionsToCheck) {
                bool found = false;
                for (auto& j : availableExtensions)
                    if (!strcmp(i, j.extensionName)) {
                        found = true;
                        break;
                    }
                if (!found) i = nullptr;
            }
        } else
            for (auto& i : extensionsToCheck) i = nullptr;
        return VK_SUCCESS;
    }
    void InstanceLayers(const std::vector<const char*>& layerNames)
    {
        instanceLayers = layerNames;
    }
    void InstanceExtensions(const std::vector<const char*>& extensionNames)
    {
        instanceExtensions = extensionNames;
    }
    //                    Set Window Surface
    void Surface(VkSurfaceKHR surface)
    {
        if (!this->surface) this->surface = surface;
    }
    //                    Create Logical Device
    void AddDeviceExtension(const char* extensionName)
    {
        AddLayerOrExtension(deviceExtensions, extensionName);
    }
    VkResult GetPhysicalDevices()
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
    VkResult DeterminePhysicalDevice(uint32_t deviceIndex = 0, bool enableGraphicsQueue = true,
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
        auto& [ig, ip, ic] = queueFamilyIndexCombinations[deviceIndex];
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
    VkResult CreateDevice(VkDeviceCreateFlags flags = 0)
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
    VkResult CheckDeviceExtensions(std::span<const char*> extensionsToCheck,
                                   const char* layerName = nullptr) const
    {
        return VK_SUCCESS;
    }
    void DeviceExtensions(const std::vector<const char*>& extensionNames)
    {
        deviceExtensions = extensionNames;
    }
    //                    Create Swapchain
    VkResult GetSurfaceFormats()
    {
        uint32_t surfaceFormatCount;
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
    VkResult SetSurfaceFormat(VkSurfaceFormatKHR surfaceFormat)
    {
        bool formatIsAvailable = false;
        if (!surfaceFormat.format) {
            for (auto& i : availableSurfaceFormats)
                if (i.colorSpace == surfaceFormat.colorSpace) {
                    swapchainCreateInfo.imageFormat = i.format;
                    swapchainCreateInfo.imageColorSpace = i.colorSpace;
                    formatIsAvailable = true;
                    break;
                }
        } else
            for (auto& i : availableSurfaceFormats)
                if (i.format == surfaceFormat.format && i.colorSpace == surfaceFormat.colorSpace) {
                    swapchainCreateInfo.imageFormat = i.format;
                    swapchainCreateInfo.imageColorSpace = i.colorSpace;
                    formatIsAvailable = true;
                    break;
                }
        if (!formatIsAvailable) return VK_ERROR_FORMAT_NOT_SUPPORTED;
        if (swapchain) return RecreateSwapchain();
        return VK_SUCCESS;
    }
    VkResult CreateSwapchain(bool limitFrameRate = true, VkSwapchainCreateFlagsKHR flags = 0)
    {
        // Get surface capabilities
        VkSurfaceCapabilitiesKHR surfaceCapabilities = {};
        if (VkResult result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface,
                                                                        &surfaceCapabilities)) {
            std::cout << std::format(
                "[ graphicsBase ] ERROR\nFailed to get physical device surface "
                "capabilities!\nError code: {}\n",
                int32_t(result));
            return result;
        }
        // Set image count
        swapchainCreateInfo.minImageCount =
            surfaceCapabilities.minImageCount +
            (surfaceCapabilities.maxImageCount > surfaceCapabilities.minImageCount);
        // Set image extent
        swapchainCreateInfo.imageExtent =
            surfaceCapabilities.currentExtent.width == -1
                ? VkExtent2D {glm::clamp(defaultWindowSize.width,
                                         surfaceCapabilities.minImageExtent.width,
                                         surfaceCapabilities.maxImageExtent.width),
                              glm::clamp(defaultWindowSize.height,
                                         surfaceCapabilities.minImageExtent.height,
                                         surfaceCapabilities.maxImageExtent.height)}
                : surfaceCapabilities.currentExtent;
        // Set transformation
        swapchainCreateInfo.preTransform = surfaceCapabilities.currentTransform;
        // Set alpha compositing mode
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
        swapchainCreateInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        if (surfaceCapabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT)
            swapchainCreateInfo.imageUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        if (surfaceCapabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT)
            swapchainCreateInfo.imageUsage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        else
            std::cout << std::format(
                "[ graphicsBase ] WARNING\nVK_IMAGE_USAGE_TRANSFER_DST_BIT isn't supported!\n");

        // Get surface formats
        if (!availableSurfaceFormats.size())
            if (VkResult result = GetSurfaceFormats()) return result;
        // If surface format is not determined, select a a four-component UNORM format
        if (!swapchainCreateInfo.imageFormat)
            if (SetSurfaceFormat({VK_FORMAT_R8G8B8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR}) &&
                SetSurfaceFormat({VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR})) {
                swapchainCreateInfo.imageFormat = availableSurfaceFormats[0].format;
                swapchainCreateInfo.imageColorSpace = availableSurfaceFormats[0].colorSpace;
                std::cout << std::format(
                    "[ graphicsBase ] WARNING\nFailed to select a four-component UNORM surface "
                    "format!\n");
            }

        // Get surface present modes
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
        swapchainCreateInfo.presentMode = VK_PRESENT_MODE_FIFO_KHR;
        if (!limitFrameRate)
            for (size_t i = 0; i < surfacePresentModeCount; i++)
                if (surfacePresentModes[i] == VK_PRESENT_MODE_MAILBOX_KHR) {
                    swapchainCreateInfo.presentMode = VK_PRESENT_MODE_MAILBOX_KHR;
                    break;
                }

        swapchainCreateInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        swapchainCreateInfo.flags = flags;
        swapchainCreateInfo.surface = surface;
        swapchainCreateInfo.imageArrayLayers = 1;
        swapchainCreateInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        swapchainCreateInfo.clipped = VK_TRUE;

        if (VkResult result = CreateSwapchain_Internal()) return result;
        for (auto& i : callbacks_createSwapchain) i();
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
    VkResult RecreateDevice(VkDeviceCreateFlags flags = 0)
    {
        if (device) {
            if (VkResult result = WaitIdle();
                result != VK_SUCCESS && result != VK_ERROR_DEVICE_LOST)
                return result;
            if (swapchain) {
                ExecuteCallbacks(callbacks_destroySwapchain);
                for (auto& i : swapchainImageViews)
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
        return CreateDevice(flags);
    }
    VkResult RecreateSwapchain()
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
        if (surfaceCapabilities.currentExtent.width == 0 ||
            surfaceCapabilities.currentExtent.height == 0)
            return VK_SUBOPTIMAL_KHR;
        swapchainCreateInfo.imageExtent = surfaceCapabilities.currentExtent;
        swapchainCreateInfo.oldSwapchain = swapchain;
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

        for (auto& i : callbacks_destroySwapchain) i();
        for (auto& i : swapchainImageViews)
            if (i) vkDestroyImageView(device, i, nullptr);
        swapchainImageViews.resize(0);
        if (result = CreateSwapchain_Internal()) return result;
        for (auto& i : callbacks_createSwapchain) i();
        return VK_SUCCESS;
    }
    VkResult WaitIdle() const
    {
        VkResult result = vkDeviceWaitIdle(device);
        if (result)
            std::cout << std::format(
                "[ graphicsBase ] ERROR\nFailed to wait for the device to be idle!\nError code: "
                "{}\n",
                int32_t(result));
        return result;
    }

    // Static Function
    static constexpr graphicsBase& Base()
    {
        return singleton;
    }
};
inline graphicsBase graphicsBase::singleton;
}  // namespace vulkan

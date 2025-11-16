#include "VKBase.h"
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#pragma comment(lib, "glfw3.lib")

GLFWwindow* pWindow;
GLFWmonitor* pMonitor;
const char* windowTitle = "EasyVK";

bool InitializeWindow(VkExtent2D size, bool fullScreen = false, bool isResizable = true,
                      bool limitFrameRate = true)
{
    using namespace vulkan;

    if (!glfwInit()) {
        std::cout << std::format("[ InitializeWindow ] ERROR\nFailed to initialize GLFW!\n");
        return false;
    }
    // 设置 glfw 不使用 opengl api
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    // 设置 glfw 窗口可调整大小
    glfwWindowHint(GLFW_RESIZABLE, isResizable);
    // 获取主显示器 handle
    pMonitor = glfwGetPrimaryMonitor();
    // 获得显示器的一些属性
    const GLFWvidmode* pMode = glfwGetVideoMode(pMonitor);
    pWindow = fullScreen
                  // 创建窗口，第四个参数为 nullptr 则为从窗口模式，否则为全屏模式
                  // 全屏一般设置为显示器分辨率相同的宽、高;
                  ? glfwCreateWindow(pMode->width, pMode->height, windowTitle, pMonitor, nullptr)
                  : glfwCreateWindow(size.width, size.height, windowTitle, nullptr, nullptr);
    if (!pWindow) {
        std::cout << std::format("[ InitializeWindow ]\nFailed to create a glfw window!\n");
        // 清理并推出 glfw
        glfwTerminate();
        return false;
    }
#ifdef _WIN32  // 已知是 win32 平台，就直接添加该平台所需扩展好啦
    // vulkan 是可以不显示的，例如仅用于计算、云游戏服务器不需要在服务器上显示，
    // 所以关于窗口显示部分的机制是通过 extension 来实现的
    // 添加 extension，该扩展用于 vulkan 与窗口系统进行交互的部分，但该扩展仍与具体平台无关
    graphicsBase::Base().AddInstanceExtension(VK_KHR_SURFACE_EXTENSION_NAME);
    // 添加 extension，该扩展用于 vulkan 在 win32 平台与窗口系统的交互，是与平台相关的
    graphicsBase::Base().AddInstanceExtension(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
#else
    uint32_t extensionCount = 0;
    const char** extensionNames;
    // glfw 返回它所需的 vulkan extension
    extensionNames = glfwGetRequiredInstanceExtensions(&extensionCount);
    if (!extensionNames) {
        std::cout << std::format(
            "[ InitializeWindow ]\nVulkan is not available on this machine!\n");
        glfwTerminate();
        return false;
    }
    for (size_t i = 0; i < extensionCount; i++)
        // 添加 glfw 所需的 extension
        graphicsBase::Base().AddInstanceExtension(extensionNames[i]);
#endif
    // 添加交换链 extension，vulkan 中没有默认帧缓冲区 default framebuffer 的概念
    // 所以需要手动添加 extension 以使用该功能，交换链本质上是一个等待呈现到屏幕上的队列
    graphicsBase::Base().AddDeviceExtension(VK_KHR_SWAPCHAIN_EXTENSION_NAME);

    // 尝试使用 vulkan 的最新版本
    graphicsBase::Base().UseLatestApiVersion();
    // 创建 vulkan 实例
    if (graphicsBase::Base().CreateInstance()) return false;

    VkSurfaceKHR surface = VK_NULL_HANDLE;
    // 创建一个 vulkan 的 window surface // 需要先创建 vulkan 实例
    if (VkResult result = glfwCreateWindowSurface(vulkan::graphicsBase::Base().Instance(), pWindow,
                                                  nullptr, &surface)) {
        std::cout << std::format(
            "[ InitializeWindow ] ERROR\nFailed to create a window surface!\nError code: {}\n",
            int32_t(result));
        glfwTerminate();
        return false;
    }
    graphicsBase::Base().Surface(surface);

    // 查询获取物理设备
    // 选择一个物理设备
    // 创建逻辑设备
    if (vulkan::graphicsBase::Base().GetPhysicalDevices() ||
        vulkan::graphicsBase::Base().DeterminePhysicalDevice(0, true, false) ||
        vulkan::graphicsBase::Base().CreateDevice())
        return false;

    // 创建交换链
    if (graphicsBase::Base().CreateSwapchain(limitFrameRate)) return false;

    return true;
}
void TerminateWindow()
{
    vulkan::graphicsBase::Base().WaitIdle();
    glfwTerminate();
}
void MakeWindowFullScreen()
{
    const GLFWvidmode* pMode = glfwGetVideoMode(pMonitor);
    glfwSetWindowMonitor(pWindow, pMonitor, 0, 0, pMode->width, pMode->height, pMode->refreshRate);
}
void MakeWindowWindowed(VkOffset2D position, VkExtent2D size)
{
    const GLFWvidmode* pMode = glfwGetVideoMode(pMonitor);
    glfwSetWindowMonitor(pWindow, nullptr, position.x, position.y, size.width, size.height,
                         pMode->refreshRate);
}
void TitleFps()
{
    static double time0 = glfwGetTime();
    static double time1;
    static double dt;
    static int dframe = -1;
    static std::stringstream info;
    time1 = glfwGetTime();
    dframe++;
    if ((dt = time1 - time0) >= 1) {
        info.precision(1);
        info << windowTitle << "    " << std::fixed << dframe / dt << " FPS";
        glfwSetWindowTitle(pWindow, info.str().c_str());
        info.str("");
        time0 = time1;
        dframe = 0;
    }
}

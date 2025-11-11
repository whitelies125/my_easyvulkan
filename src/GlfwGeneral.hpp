#include "VKBase.h"
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#pragma comment(lib, "glfw3.lib")  // 链接编译所需的静态库

// 窗口的指针，全局变量自动初始化为NULL
GLFWwindow* pWindow;
// 显示器信息的指针
GLFWmonitor* pMonitor;
// 窗口标题
const char* windowTitle = "EasyVK";

bool InitializeWindow(VkExtent2D size, bool fullScreen = false, bool isResizable = true,
                      bool limitFrameRate = true)
{
    if (!glfwInit()) {
        std::cout << std::format("[ InitializeWindow ] ERROR\nFailed to initialize GLFW!\n");
        return false;
    }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, isResizable);
    pMonitor = glfwGetPrimaryMonitor();
    const GLFWvidmode* pMode = glfwGetVideoMode(pMonitor);
    pWindow = fullScreen
                  ? glfwCreateWindow(pMode->width, pMode->height, windowTitle, pMonitor, nullptr)
                  : glfwCreateWindow(size.width, size.height, windowTitle, nullptr, nullptr);
    if (!pWindow) {
        std::cout << std::format("[ InitializeWindow ]\nFailed to create a glfw window!\n");
        glfwTerminate();
        return false;
    }
    /*待Ch1-3和Ch1-4填充*/
    return true;
}

void TerminateWindow()
{
    /*待Ch1-4填充*/
    glfwTerminate();
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
        info.str("");  // 别忘了在设置完窗口标题后清空所用的stringstream
        time0 = time1;
        dframe = 0;
    }
}

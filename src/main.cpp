#include "EasyVulkan.hpp"
#include "GlfwGeneral.hpp"
#include "VKBase+.h"

using namespace vulkan;  // main.cpp里会写一堆vulkan命名空间下的类型，using命名空间以省事
                         //
struct vertex {
    glm::vec2 position;
    glm::vec4 color;
};

pipelineLayout pipelineLayout_triangle;  // 管线布局
pipeline pipeline_triangle;              // 管线

// 该函数调用easyVulkan::CreateRpwf_Screen()并存储返回的引用到静态变量
const auto &RenderPassAndFramebuffers()
{
    static const auto &rpwf = easyVulkan::CreateRpwf_Screen();
    return rpwf;
}
// 该函数用于创建管线布局
void CreateLayout()
{
    VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo {};
    // 创建管线布局
    pipelineLayout_triangle.Create(pipelineLayoutCreateInfo);
}

// 该函数用于创建管线
void CreatePipeline()
{
    static shaderModule vert("shader/vertexBuffer.vert.spv");
    static shaderModule frag("shader/vertexBuffer.frag.spv");
    static VkPipelineShaderStageCreateInfo shaderStageCreateInfos_triangle[2] = {
        vert.StageCreateInfo(VK_SHADER_STAGE_VERTEX_BIT),
        frag.StageCreateInfo(VK_SHADER_STAGE_FRAGMENT_BIT)};
    auto Create = [] {
        graphicsPipelineCreateInfoPack pipelineCiPack;

        //数据来自0号顶点缓冲区，输入频率是逐顶点输入
        pipelineCiPack.vertexInputBindings.emplace_back(0, sizeof(vertex), VK_VERTEX_INPUT_RATE_VERTEX);
        //location为0，数据来自0号顶点缓冲区，vec2对应VK_FORMAT_R32G32_SFLOAT，用offsetof计算position在vertex中的起始位置
        pipelineCiPack.vertexInputAttributes.emplace_back(0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(vertex, position));
        //location为1，数据来自0号顶点缓冲区，vec4对应VK_FORMAT_R32G32B32A32_SFLOAT，用offsetof计算color在vertex中的起始位置
        pipelineCiPack.vertexInputAttributes.emplace_back(1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(vertex, color));

        pipelineCiPack.createInfo.layout = pipelineLayout_triangle; // 指定管线布局
        pipelineCiPack.createInfo.renderPass = RenderPassAndFramebuffers().renderPass; // 指定管线要使用的渲染通道
        pipelineCiPack.inputAssemblyStateCi.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST; // 告知图元 primitive 的拓扑结构类型，这里指定为三角形
        // 设定视口变换，这里设定为宽高与窗口宽高一致
        pipelineCiPack.viewports.emplace_back(0.f, 0.f, float(windowSize.width),
                                              float(windowSize.height), 0.f, 1.f);
        // 设定裁剪变换，这里设定为与视口一致的矩形区域
        pipelineCiPack.scissors.emplace_back(VkOffset2D {}, windowSize); // 指定起始点和基于起始点开始的宽高得到的范围
        pipelineCiPack.multisampleStateCi.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT; // 不使用多重采样，每个像素点只采样一次
        pipelineCiPack.colorBlendAttachmentStates.push_back({.colorWriteMask = 0b1111}); // 指定颜色混合状态，这里指定为不使用混合且 RGBA 四通道都写入
        pipelineCiPack.UpdateAllArrays();
        pipelineCiPack.createInfo.stageCount = 2; // 指定 shader stage 的数量
        pipelineCiPack.createInfo.pStages = shaderStageCreateInfos_triangle; // 要创建的 shader stage 的信息
        // 创建 pipeline
        pipeline_triangle.Create(pipelineCiPack);
    };
    auto Destroy = [] { pipeline_triangle.~pipeline(); };
    graphicsBase::Base().AddCallback_CreateSwapchain(Create);
    graphicsBase::Base().AddCallback_DestroySwapchain(Destroy);
    Create();
}

int main()
{
    if (!InitializeWindow({1280, 720})) return -1;

    vertex vertices[] = {
        { {  .0f, -.5f }, { 1, 0, 0, 1 } }, //红色
        { { -.5f,  .5f }, { 0, 1, 0, 1 } }, //绿色
        { {  .5f,  .5f }, { 0, 0, 1, 1 } }  //蓝色
    };
    vertexBuffer vertexBuffer(sizeof vertices);
    vertexBuffer.TransferData(vertices);

    // 创建渲染通道 render pass 和 帧缓冲 framebuffer
    const auto &[renderPass, framebuffers] = RenderPassAndFramebuffers();
    CreateLayout();
    CreatePipeline();

    fence fence;  // 以非置位状态创建栅栏
    semaphore semaphore_imageIsAvailable;
    semaphore semaphore_renderingIsOver;

    // 创建 command pool
    commandPool commandPool(graphicsBase::Base().QueueFamilyIndex_Graphics(),
                            VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
    // 从 command pool 中分配 command buffer
    commandBuffer commandBuffer;
    commandPool.AllocateBuffers(commandBuffer);

    VkClearValue clearColor = {.color = {1.f, 0.f, 0.f, 1.f}};  // 红色

    while (!glfwWindowShouldClose(pWindow)) {
        // 如果窗口是最小化状态，就等待事件直到窗口恢复
        while (glfwGetWindowAttrib(pWindow, GLFW_ICONIFIED)) glfwWaitEvents();
        // 获取交换链图像索引
        graphicsBase::Base().SwapImage(semaphore_imageIsAvailable);
        auto i = graphicsBase::Base().CurrentImageIndex();

        // command buffer 开始录制 command，将这个 cmd buffer 的状态修改为 recording 状态，才可向这个 cmd buffer 录制命令（允许写入命令）
        commandBuffer.Begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT); // VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT 指示这个 cmd buffer 的记录只会被提交一次，然后在下次提交前会被重置和重新录制
        // 开始渲染通道，vkCmdBeginRenderPass 的 pRenderPassBegin 中含有 framebuffer，可见此处将 framebuffer 和 renderpass 关联上了。
        renderPass.CmdBegin(commandBuffer, framebuffers[i], {{}, windowSize}, clearColor);
        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffer.Address(), &offset);
        // 绑定图形管线
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_triangle);
        vkCmdDraw(commandBuffer, 3, 1, 0, 0);
        // 结束渲染通道
        renderPass.CmdEnd(commandBuffer);
        // command buffer 结束录制，将这个 cmd buffer 的状态修改为 executable 状态，不处于 recording 状态了，不允许向这个 cmd buffer 写入命令（禁止写入）
        commandBuffer.End();

        // 调用 vkQueueSubmit 提交 command buffer
        graphicsBase::Base().SubmitCommandBuffer_Graphics(commandBuffer, semaphore_imageIsAvailable,
                                                          semaphore_renderingIsOver, fence);
        graphicsBase::Base().PresentImage(semaphore_renderingIsOver);

        TitleFps();
        glfwPollEvents();

        // 等待并重置fence
        fence.WaitAndReset();
    }
    TerminateWindow();
    return 0;
}

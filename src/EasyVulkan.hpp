#include "VKBase.h"

using namespace vulkan;
const VkExtent2D &windowSize = graphicsBase::Base().SwapchainCreateInfo().imageExtent;

namespace easyVulkan {
using namespace vulkan;
struct renderPassWithFramebuffers {
    renderPass renderPass;
    std::vector<framebuffer> framebuffers;
};
const auto &CreateRpwf_Screen()
{
    // 渲染通道（VkRenderPass）由附件描述（VkAttachmentDescription）、子通道描述（VkSubpassDescription）、以及子通道依赖（VkSubpassDependency）组合而成。
    // 渲染通道 render pass 是对渲染流程的抽象描述，描述了 attachments 在 subpass 中的使用方式
    static renderPassWithFramebuffers rpwf;

    // 附件描述，用来描述渲染过程中要使用的 attachment 的属性以及对 attachment 的操作
    VkAttachmentDescription attachmentDescription = {
        .format = graphicsBase::Base().SwapchainCreateInfo().imageFormat, // 该 attachment 将使用的格式
        .samples = VK_SAMPLE_COUNT_1_BIT,  // 该 attachment 将使用的采样数。此处为 1 表示不使用多重采样
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,  // 用于指定在 subpass 首次使用该 attachment 开始时如何处理 attachment 的内容。此处为 clear 清空内容
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,  // 用于指定在 subpass 最后一次使用 attachment 结束时如何处理 attachment 内容。此处为 store 保存到内存
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,  // 在渲染通道实例开始时，附件图形子资源(attachment image subresource)的 layout。
                                                     // 此处为 undefined，因为 loadOp 已经指定了为 clear，因此反正都是空的，不关心 attachment 初始的内容，也就不关心其布局
        .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR};  // 在渲染通道实例开始时，附件图形子资源(attachment image subresource)将被转换为的 layout。渲染完成后会自动转换为该 layout

    VkAttachmentReference attachmentReference = { 0,  // 一个整数值，用于标识 VkRenderPassCreateInfo::pAttachments 中相应索引处的附件。此处为 0 即第一个
                                                  VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}; // 用于指定 attachment 在 subpass 过程中使用的 layout。这个 layout 转换发生在
    // 子过程描述，描述
    VkSubpassDescription subpassDescription = {.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS, // 指定该子通道支持的管线。此处为支持图形管线
                                               .colorAttachmentCount = 1,
                                               .pColorAttachments = &attachmentReference};  // 指定该子通道要使用的颜色附件(color attachment)及其布局
    // 子通道依赖，描述 子过程对 之间的依赖关系。
    VkSubpassDependency subpassDependency = {
        .srcSubpass = VK_SUBPASS_EXTERNAL, // 依赖关系中的第一个 subpass。此处表示外部依赖
        .dstSubpass = 0, // 依赖关系中的第二个 subpass，此处为 0 即 VkRenderPassCreateInfo::subpassDescription 中的第一个 subpass
        .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,  // 指定阶段的依赖关系。dstSubpass 执行到 dstStageMask 阶段开始前，若 srcSubpass 此时已完成了 srcStageMask 阶段，则 dstSubpass 继续执行，否则等待其完成
        .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,  // 不早于提交命令缓冲区时等待 semaphore 对应的 waitDstStageMask
        .srcAccessMask = 0, // 指定内存可见性的依赖关系。Stage 结束并不意味着数据已写入内存，防止 src 只写入了缓存a，dst 只从缓存b读取了数据等这类情况导致的数据不一致。因此再从内存可见性的角度指明约束关系
        .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, // 在 srcSubpass 完成了 srcAccessMask 内存操作后，dstSubPass 才可进行 dstAccessMask 内存操作，否则等待其完成
        .dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT}; // 指定依赖关系的其他选项。此处为 by region，表示依赖关系是基于图像区域的，即 srcSubpass 和 dstSubpass 之间的依赖关系仅适用于它们访问的图像区域重叠的情况
    VkRenderPassCreateInfo renderPassCreateInfo = {.attachmentCount = 1, 
                                                   .pAttachments = &attachmentDescription, // 描述图形附件的结构体，图形附件是渲染过程中被绑定到帧缓冲的图像视图
                                                   .subpassCount = 1,
                                                   .pSubpasses = &subpassDescription, // 描述子通道的结构体，子通道是渲染过程中一个独立的阶段，包含一系列的渲染命令
                                                   .dependencyCount = 1,
                                                   .pDependencies = &subpassDependency}; // 描述子通道依赖的结构体，子通道依赖定义了渲染过程中不同子通道之间的执行和内存依赖关系
    rpwf.renderPass.Create(renderPassCreateInfo);
    rpwf.framebuffers.resize(graphicsBase::Base().SwapchainImageCount());
    VkFramebufferCreateInfo framebufferCreateInfo = {.renderPass = rpwf.renderPass,
                                                     .attachmentCount = 1,
                                                     .width = windowSize.width,
                                                     .height = windowSize.height,
                                                     .layers = 1};
    for (size_t i = 0; i < graphicsBase::Base().SwapchainImageCount(); i++) {
        VkImageView attachment = graphicsBase::Base().SwapchainImageView(i);
        framebufferCreateInfo.pAttachments = &attachment;
        rpwf.framebuffers[i].Create(framebufferCreateInfo);
    }
    auto CreateFramebuffers = [] {
        rpwf.framebuffers.resize(graphicsBase::Base().SwapchainImageCount());
        VkFramebufferCreateInfo framebufferCreateInfo = {.renderPass = rpwf.renderPass,
                                                         .attachmentCount = 1,
                                                         .width = windowSize.width,
                                                         .height = windowSize.height,
                                                         .layers = 1};
        for (size_t i = 0; i < graphicsBase::Base().SwapchainImageCount(); i++) {
            VkImageView attachment = graphicsBase::Base().SwapchainImageView(i);
            framebufferCreateInfo.pAttachments = &attachment;
            rpwf.framebuffers[i].Create(framebufferCreateInfo);
        }
    };
    auto DestroyFramebuffers = [] {
        rpwf.framebuffers.clear();  // 清空vector中的元素时会逐一执行析构函数
    };
    CreateFramebuffers();

    ExecuteOnce(rpwf); //防止再次调用本函数时，重复添加回调函数
    graphicsBase::Base().AddCallback_CreateSwapchain(CreateFramebuffers);
    graphicsBase::Base().AddCallback_DestroySwapchain(DestroyFramebuffers);

    return rpwf;
}
}  // namespace easyVulkan

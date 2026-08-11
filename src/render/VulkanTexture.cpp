#include "render/VulkanTexture.h"

#include <imgui.h>
#include <imgui_impl_vulkan.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace imtube {

namespace {

/* Abort with a diagnostic when a Vulkan call fails. These calls run on the
 * render thread with no sensible recovery path, so failing loudly is better
 * than continuing with a half-initialized texture. */
void vk_check(VkResult result, const char* what)
{
    if (result != VK_SUCCESS)
    {
        fprintf(stderr, "[vulkan] %s failed: %d\n", what, (int)result);
        abort();
    }
}

} /* namespace */

VulkanTexture::VulkanTexture(VulkanTexture&& other) noexcept
    : m_ctx(other.m_ctx), m_image(other.m_image), m_image_memory(other.m_image_memory),
      m_view(other.m_view), m_descriptor_set(other.m_descriptor_set), m_staging(other.m_staging),
      m_staging_memory(other.m_staging_memory), m_staging_size(other.m_staging_size),
      m_fence(other.m_fence), m_width(other.m_width), m_height(other.m_height)
{
    other.m_image = VK_NULL_HANDLE;
    other.m_image_memory = VK_NULL_HANDLE;
    other.m_view = VK_NULL_HANDLE;
    other.m_descriptor_set = VK_NULL_HANDLE;
    other.m_staging = VK_NULL_HANDLE;
    other.m_staging_memory = VK_NULL_HANDLE;
    other.m_staging_size = 0;
    other.m_fence = VK_NULL_HANDLE;
    other.m_width = 0;
    other.m_height = 0;
}

VulkanTexture& VulkanTexture::operator=(VulkanTexture&& other) noexcept
{
    if (this == &other)
        return *this;
    destroy();
    m_ctx = other.m_ctx;
    m_image = other.m_image;
    m_image_memory = other.m_image_memory;
    m_view = other.m_view;
    m_descriptor_set = other.m_descriptor_set;
    m_staging = other.m_staging;
    m_staging_memory = other.m_staging_memory;
    m_staging_size = other.m_staging_size;
    m_fence = other.m_fence;
    m_width = other.m_width;
    m_height = other.m_height;
    other.m_image = VK_NULL_HANDLE;
    other.m_image_memory = VK_NULL_HANDLE;
    other.m_view = VK_NULL_HANDLE;
    other.m_descriptor_set = VK_NULL_HANDLE;
    other.m_staging = VK_NULL_HANDLE;
    other.m_staging_memory = VK_NULL_HANDLE;
    other.m_staging_size = 0;
    other.m_fence = VK_NULL_HANDLE;
    other.m_width = 0;
    other.m_height = 0;
    return *this;
}

uint32_t VulkanTexture::find_memory_type(uint32_t type_bits, VkMemoryPropertyFlags properties) const
{
    VkPhysicalDeviceMemoryProperties mem_props;
    vkGetPhysicalDeviceMemoryProperties(m_ctx.physical_device, &mem_props);
    for (uint32_t i = 0; i < mem_props.memoryTypeCount; i++)
    {
        if ((type_bits & (1u << i)) &&
            (mem_props.memoryTypes[i].propertyFlags & properties) == properties)
            return i;
    }
    return ~0u;
}

bool VulkanTexture::allocate_image_memory(VkDeviceSize size, VkMemoryPropertyFlags properties, VkDeviceMemory* out) const
{
    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(m_ctx.device, m_image, &req);

    const uint32_t type = find_memory_type(req.memoryTypeBits, properties);
    if (type == ~0u)
        return false;

    VkMemoryAllocateInfo alloc_info = {};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = size;
    alloc_info.memoryTypeIndex = type;
    const VkResult res = vkAllocateMemory(m_ctx.device, &alloc_info, nullptr, out);
    return res == VK_SUCCESS;
}

bool VulkanTexture::ensure_staging(VkDeviceSize size)
{
    if (m_staging_size >= size)
        return true;

    /* A larger staging buffer is needed: release the old one first. */
    if (m_staging != VK_NULL_HANDLE)
    {
        vkDestroyBuffer(m_ctx.device, m_staging, nullptr);
        vkFreeMemory(m_ctx.device, m_staging_memory, nullptr);
        m_staging = VK_NULL_HANDLE;
        m_staging_memory = VK_NULL_HANDLE;
        m_staging_size = 0;
    }

    VkBufferCreateInfo buf_info = {};
    buf_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buf_info.size = size;
    buf_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    buf_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(m_ctx.device, &buf_info, nullptr, &m_staging) != VK_SUCCESS)
        return false;

    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(m_ctx.device, m_staging, &req);
    const uint32_t type = find_memory_type(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
    if (type == ~0u)
        return false;

    VkMemoryAllocateInfo alloc_info = {};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = req.size;
    alloc_info.memoryTypeIndex = type;
    if (vkAllocateMemory(m_ctx.device, &alloc_info, nullptr, &m_staging_memory) != VK_SUCCESS)
        return false;

    vkBindBufferMemory(m_ctx.device, m_staging, m_staging_memory, 0);
    m_staging_size = size;
    return true;
}

bool VulkanTexture::create(const GpuContext& ctx, int width, int height)
{
    if (ctx.device == VK_NULL_HANDLE || width <= 0 || height <= 0)
        return false;

    /* Recreate only when the size actually changes. */
    if (m_ctx.device == ctx.device && valid() && m_width == width && m_height == height)
        return true;

    destroy();
    m_ctx = ctx;

    VkImageCreateInfo img_info = {};
    img_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    img_info.imageType = VK_IMAGE_TYPE_2D;
    img_info.format = VK_FORMAT_R8G8B8A8_UNORM;
    img_info.extent.width = (uint32_t)width;
    img_info.extent.height = (uint32_t)height;
    img_info.extent.depth = 1;
    img_info.mipLevels = 1;
    img_info.arrayLayers = 1;
    img_info.samples = VK_SAMPLE_COUNT_1_BIT;
    img_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    img_info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    img_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    img_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    vk_check(vkCreateImage(m_ctx.device, &img_info, nullptr, &m_image), "vkCreateImage");

    if (!allocate_image_memory(0, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &m_image_memory))
    {
        destroy();
        return false;
    }
    vkBindImageMemory(m_ctx.device, m_image, m_image_memory, 0);

    VkImageViewCreateInfo view_info = {};
    view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image = m_image;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = VK_FORMAT_R8G8B8A8_UNORM;
    view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    view_info.subresourceRange.levelCount = 1;
    view_info.subresourceRange.layerCount = 1;
    vk_check(vkCreateImageView(m_ctx.device, &view_info, nullptr, &m_view), "vkCreateImageView");

    /* Register the texture with the ImGui Vulkan backend so ImGui::Image can
     * sample it (the backend provides its own sampler; the descriptor type is
     * SAMPLED_IMAGE and the set comes from the app-provided descriptor pool). */
    m_descriptor_set = ImGui_ImplVulkan_AddTexture(m_view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    if (!ensure_staging((VkDeviceSize)width * height * 4))
    {
        destroy();
        return false;
    }

    VkFenceCreateInfo fence_info = {};
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    vk_check(vkCreateFence(m_ctx.device, &fence_info, nullptr, &m_fence), "vkCreateFence");

    m_width = width;
    m_height = height;
    return true;
}

bool VulkanTexture::upload(const uint8_t* rgba_pixels)
{
    if (!valid() || rgba_pixels == nullptr)
        return false;

    const VkDeviceSize bytes = (VkDeviceSize)m_width * m_height * 4;
    if (!ensure_staging(bytes))
        return false;

    void* mapped = nullptr;
    vk_check(vkMapMemory(m_ctx.device, m_staging_memory, 0, bytes, 0, &mapped),
             "vkMapMemory");
    memcpy(mapped, rgba_pixels, (size_t)bytes);
    vkUnmapMemory(m_ctx.device, m_staging_memory);

    VkCommandBuffer cmdbuf = VK_NULL_HANDLE;
    {
        VkCommandBufferAllocateInfo alloc = {};
        alloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        alloc.commandPool = m_ctx.command_pool;
        alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        alloc.commandBufferCount = 1;
        vk_check(vkAllocateCommandBuffers(m_ctx.device, &alloc, &cmdbuf), "vkAllocateCommandBuffers");
    }

    {
        VkCommandBufferBeginInfo begin = {};
        begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vk_check(vkBeginCommandBuffer(cmdbuf, &begin), "vkBeginCommandBuffer");
    }

    /* UNDEFINED -> TRANSFER_DST */
    VkImageMemoryBarrier pre = {};
    pre.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    pre.srcAccessMask = 0;
    pre.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    pre.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    pre.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    pre.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    pre.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    pre.image = m_image;
    pre.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    pre.subresourceRange.levelCount = 1;
    pre.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier(cmdbuf, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &pre);

    VkBufferImageCopy region = {};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageExtent.width = (uint32_t)m_width;
    region.imageExtent.height = (uint32_t)m_height;
    region.imageExtent.depth = 1;
    vkCmdCopyBufferToImage(cmdbuf, m_staging, m_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    /* TRANSFER_DST -> SHADER_READ_ONLY_OPTIMAL */
    VkImageMemoryBarrier post = {};
    post.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    post.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    post.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    post.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    post.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    post.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    post.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    post.image = m_image;
    post.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    post.subresourceRange.levelCount = 1;
    post.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier(cmdbuf, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &post);

    vk_check(vkEndCommandBuffer(cmdbuf), "vkEndCommandBuffer");
    submit_single(cmdbuf);
    return true;
}

void VulkanTexture::submit_single(VkCommandBuffer cmdbuf)
{
    VkSubmitInfo submit = {};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmdbuf;
    vk_check(vkQueueSubmit(m_ctx.queue, 1, &submit, m_fence), "vkQueueSubmit");
    vk_check(vkWaitForFences(m_ctx.device, 1, &m_fence, VK_TRUE, UINT64_MAX), "vkWaitForFences");
    vk_check(vkResetFences(m_ctx.device, 1, &m_fence), "vkResetFences");

    vkFreeCommandBuffers(m_ctx.device, m_ctx.command_pool, 1, &cmdbuf);
}

void VulkanTexture::destroy()
{
    if (m_ctx.device != VK_NULL_HANDLE)
    {
        if (m_descriptor_set != VK_NULL_HANDLE)
        {
            ImGui_ImplVulkan_RemoveTexture(m_descriptor_set);
            m_descriptor_set = VK_NULL_HANDLE;
        }
        if (m_fence != VK_NULL_HANDLE)
        {
            vkDestroyFence(m_ctx.device, m_fence, nullptr);
            m_fence = VK_NULL_HANDLE;
        }
        if (m_view != VK_NULL_HANDLE)
        {
            vkDestroyImageView(m_ctx.device, m_view, nullptr);
            m_view = VK_NULL_HANDLE;
        }
        if (m_image != VK_NULL_HANDLE)
        {
            vkDestroyImage(m_ctx.device, m_image, nullptr);
            m_image = VK_NULL_HANDLE;
        }
        if (m_image_memory != VK_NULL_HANDLE)
        {
            vkFreeMemory(m_ctx.device, m_image_memory, nullptr);
            m_image_memory = VK_NULL_HANDLE;
        }
        if (m_staging != VK_NULL_HANDLE)
        {
            vkDestroyBuffer(m_ctx.device, m_staging, nullptr);
            m_staging = VK_NULL_HANDLE;
        }
        if (m_staging_memory != VK_NULL_HANDLE)
        {
            vkFreeMemory(m_ctx.device, m_staging_memory, nullptr);
            m_staging_memory = VK_NULL_HANDLE;
        }
        m_staging_size = 0;
    }
    m_width = 0;
    m_height = 0;
}

} /* namespace imtube */

#include "framebuffer.h"

#include <cstring>
#include <limits>

bool VideoFrameBuffer::valid(const VideoFrameView& source)
{
    // 限制单帧分配；首版 4:2:0 仅接受偶数尺寸。
    if (source.width == 0 || source.height == 0 || source.width > 8192 || source.height > 8192 ||
        (source.matrix != VideoColorMatrix::BT601 && source.matrix != VideoColorMatrix::BT709))
    {
        return false;
    }
    const bool bgra = source.format == VideoPixelFormat::BGRA;
    if (!bgra && source.format != VideoPixelFormat::I420 && source.format != VideoPixelFormat::NV12)
    {
        return false;
    }
    if (!bgra && ((source.width | source.height) & 1))
    {
        return false;
    }
    const unsigned count = bgra ? 1 : source.format == VideoPixelFormat::I420 ? 3 : 2;
    std::array<std::size_t, 3> widths {source.width, source.width / 2, source.width / 2};
    if (bgra)
    {
        widths[0] *= 4;
    }
    else if (source.format == VideoPixelFormat::NV12)
    {
        widths[1] = source.width;
    }
    for (unsigned i = 0; i < count; ++i)
    {
        const auto& plane = source.planes[i];
        const std::size_t rows = i == 0 ? source.height : source.height / 2;
        if (!plane.data || plane.stride < widths[i] || plane.stride > std::numeric_limits<uint32_t>::max() ||
            (rows > 1 && plane.stride > (std::numeric_limits<std::size_t>::max() - widths[i]) / (rows - 1)) ||
            plane.size < (rows - 1) * plane.stride + widths[i])
        {
            return false;
        }
    }
    return true;
}

bool VideoFrameBuffer::copy(const VideoFrameView& source)
{
    if (!valid(source))
    {
        return false;
    }
    const unsigned count = source.format == VideoPixelFormat::BGRA   ? 1
                           : source.format == VideoPixelFormat::I420 ? 3
                                                                     : 2;
    std::array<std::size_t, 3> widths {source.width, source.width / 2, source.width / 2};
    if (source.format == VideoPixelFormat::BGRA)
    {
        widths[0] *= 4;
    }
    else if (source.format == VideoPixelFormat::NV12)
    {
        widths[1] = source.width;
    }
    std::size_t size = widths[0] * source.height;
    for (unsigned i = 1; i < count; ++i)
    {
        size += widths[i] * (source.height / 2);
    }
    if (capacity < size)
    {
        // 每个有效字节随后都会被覆盖，无需先清零整帧内存。
        storage.reset(new uint8_t[size]);
        capacity = size;
    }
    frame = source;
    frame.planes = {};
    auto* destination = storage.get();
    for (unsigned i = 0; i < count; ++i)
    {
        const std::size_t rows = i == 0 ? source.height : source.height / 2;
        const auto bytes = widths[i] * rows;
        if (source.planes[i].stride == widths[i])
        {
            std::memcpy(destination, source.planes[i].data, bytes);
        }
        else
        {
            for (std::size_t row = 0; row < rows; ++row)
            {
                std::memcpy(destination + row * widths[i], source.planes[i].data + row * source.planes[i].stride,
                            widths[i]);
            }
        }
        frame.planes[i].data = destination;
        frame.planes[i].stride = widths[i];
        frame.planes[i].size = bytes;
        destination += bytes;
    }
    return true;
}

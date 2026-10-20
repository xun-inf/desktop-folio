#include "services/stream_preview/stream_preview.h"
#include "stream_preview_p.h"
#include "gpudevice.h"

#include <algorithm>
#include <atomic>
#include <exception>
#include <stdexcept>
#include <utility>

StreamPreview::StreamPreview(FrameReady ready, ErrorCallback error)
    : d_ptr(new StreamPreviewPrivate(std::move(ready), std::move(error)))
{
}

StreamPreview::~StreamPreview() = default;

VideoSubmitResult StreamPreview::submitFrame(const VideoFrameView& frame)
{
    return d_ptr->submitFrame(frame);
}

VideoSubmitResult StreamPreview::submitFrame(const VideoFrameView& frame, std::shared_ptr<const void> owner)
{
    if (!owner)
    {
        return VideoSubmitResult::InvalidFrame;
    }
    return d_ptr->submitFrame(frame, std::move(owner));
}

void StreamPreview::releaseFrame(uint64_t token)
{
    d_ptr->releaseFrame(token);
}

void StreamPreview::close()
{
    d_ptr->close();
}

StreamPreviewPrivate::StreamPreviewPrivate(StreamPreview::FrameReady ready, StreamPreview::ErrorCallback error)
    : m_ready(std::move(ready)), m_error(std::move(error)), m_worker([this] { run(); })
{
}

StreamPreviewPrivate::~StreamPreviewPrivate()
{
    close();
}

VideoSubmitResult StreamPreviewPrivate::submitFrame(const VideoFrameView& frame, std::shared_ptr<const void> owner)
{
    std::unique_ptr<VideoFrameBuffer> buffer;
    std::shared_ptr<const void> previousOwner;
    std::lock_guard<std::mutex> submitLock(m_submitMutex);
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_closed)
        {
            return VideoSubmitResult::Closed;
        }
    }
    if (!VideoFrameBuffer::valid(frame))
    {
        return VideoSubmitResult::InvalidFrame;
    }
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        buffer = m_pending ? std::move(m_pending) : std::move(m_spare);
    }
    if (!buffer)
    {
        buffer.reset(new VideoFrameBuffer());
    }
    previousOwner = std::move(buffer->owner);
    if (owner)
    {
        buffer->frame = frame;
        buffer->owner = std::move(owner);
    }
    else
    {
        buffer->copy(frame);
    }
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_closed)
        {
            return VideoSubmitResult::Closed;
        }
        m_pending = std::move(buffer);
    }
    m_changed.notify_one();
    return VideoSubmitResult::Accepted;
}

void StreamPreviewPrivate::releaseFrame(uint64_t token)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_closed && token != 0)
    {
        for (std::size_t i = 0; i < m_inFlight.size(); ++i)
        {
            if (m_inFlight[i] == token)
            {
                m_released[i] = token;
                m_changed.notify_one();
                break;
            }
        }
    }
}

void StreamPreviewPrivate::close()
{
    std::unique_ptr<VideoFrameBuffer> pending;
    std::unique_ptr<VideoFrameBuffer> spare;
    std::lock_guard<std::mutex> closeLock(m_closeMutex);
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_closed = true;
        pending = std::move(m_pending);
        spare = std::move(m_spare);
        m_changed.notify_one();
    }
    if (m_worker.joinable())
    {
        m_worker.join();
    }
}

void StreamPreviewPrivate::run()
{
    // 全进程唯一，销毁并重建同一路 presenter 后旧回执不会命中新帧。
    static std::atomic<uint64_t> nextToken {1};
    try
    {
        GpuDevice gpu;
        for (;;)
        {
            std::unique_ptr<VideoFrameBuffer> buffer;
            std::array<uint64_t, 3> released {};
            {
                std::unique_lock<std::mutex> lock(m_mutex);
                m_changed.wait(lock,
                               [this]
                               {
                                   return m_closed ||
                                          (m_pending &&
                                           std::find(m_inFlight.begin(), m_inFlight.end(), 0) != m_inFlight.end()) ||
                                          std::any_of(m_released.begin(), m_released.end(),
                                                      [](uint64_t token) { return token != 0; });
                               });
                if (m_closed)
                {
                    return;
                }
                buffer = std::move(m_pending);
                released.swap(m_released);
                for (std::size_t i = 0; i < released.size(); ++i)
                {
                    if (released[i] != 0)
                    {
                        m_inFlight[i] = 0;
                    }
                }
            }
            for (const auto token : released)
            {
                if (token != 0)
                {
                    gpu.release(token);
                }
            }
            SharedVideoFrame output;
            if (buffer && gpu.present(buffer->frame, nextToken.fetch_add(1), output))
            {
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    const auto slot = std::find(m_inFlight.begin(), m_inFlight.end(), 0);
                    if (slot == m_inFlight.end())
                    {
                        throw std::logic_error("shared texture tracking exhausted");
                    }
                    *slot = output.token;
                }
                if (!m_ready || !m_ready(output))
                {
                    releaseFrame(output.token);
                }
            }
            if (buffer)
            {
                buffer->owner.reset();
                std::lock_guard<std::mutex> lock(m_mutex);
                if (!m_closed)
                {
                    buffer.swap(m_spare);
                }
            }
        }
    }
    catch (const std::exception& error)
    {
        std::unique_ptr<VideoFrameBuffer> pending;
        std::unique_ptr<VideoFrameBuffer> spare;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_closed = true;
            pending = std::move(m_pending);
            spare = std::move(m_spare);
        }
        if (m_error)
        {
            m_error(error.what());
        }
    }
}

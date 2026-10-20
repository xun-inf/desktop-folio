#include "services/stream_preview/stream_preview_service.h"

#include "base/logger.h"
#include "ipc_server.h"
#include "services/stream_preview/stream_preview.h"

#include <limits>
#include <vector>

namespace
{
void increment(uint64_t& value, uint64_t amount = 1)
{
    const uint64_t maximum = std::numeric_limits<uint64_t>::max();
    value = amount > maximum - value ? maximum : value + amount;
}
} // namespace

std::shared_ptr<Service> StreamPreviewService::createService(IpcServer* server, EventLoop*)
{
    return std::shared_ptr<Service>(new StreamPreviewService(server));
}

StreamPreviewService::StreamPreviewService(IpcServer* server) : ServiceBase(server)
{
    this->registerMethod<services::CreateStreamPreviewArgs>("createStreamPreview",
                                                            &StreamPreviewService::createStreamPreview);
    this->registerMethod<services::ReleaseStreamPreviewArgs>("releaseStreamPreview",
                                                             &StreamPreviewService::releaseStreamPreview);
    this->registerMethod<services::ReleaseVideoFrameArgs>("releaseVideoFrame",
                                                          &StreamPreviewService::releaseVideoFrame);
    initialize();
}

StreamPreviewService::~StreamPreviewService()
{
    closeAll();
}

bool StreamPreviewService::createStreamPreview(const services::CreateStreamPreviewArgs& args, ReplyWriter& reply)
{
    if (create(args.id()) == nullptr)
    {
        replyError(reply, "Preview creation failed");
        return true;
    }
    replyOk(reply);
    return true;
}

bool StreamPreviewService::releaseStreamPreview(const services::ReleaseStreamPreviewArgs& args, ReplyWriter& reply)
{
    release(args.id());
    replyOk(reply);
    return true;
}

bool StreamPreviewService::releaseVideoFrame(const services::ReleaseVideoFrameArgs& args, ReplyWriter& reply)
{
    releaseFrame(args.id(), args.token());
    replyOk(reply);
    return true;
}

void StreamPreviewService::emitFrameEvent(std::uint32_t id, std::uint64_t token, std::uint64_t handle,
                                          std::uint32_t width, std::uint32_t height, std::int64_t timestampUs)
{
    this->emit<services::FrameEvent>(
        "frame", [id, token, handle, width, height, timestampUs](flatbuffers::FlatBufferBuilder& builder)
        { return services::CreateFrameEvent(builder, id, token, handle, width, height, timestampUs); });
}

// --- 预览管理（原 StreamPreviewMgr）---
bool StreamPreviewService::initialize()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_stats.closing)
        return false;
    if (!m_stats.terminal)
        return true;
    if (m_stats.activeSubmissions != 0 || !m_presenters.empty() || !m_frames.empty() ||
        m_stats.generation == std::numeric_limits<uint64_t>::max())
        return false;
    const uint64_t generation = m_stats.generation + 1;
    m_stats = Stats();
    m_stats.generation = generation;
    return true;
}

std::shared_ptr<StreamPreview> StreamPreviewService::create(uint32_t id)
{
    uint64_t serial = 0, generation = 0, session = 0;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_stats.terminal)
            return {};
        const auto current = m_presenters.find(id);
        if (current != m_presenters.end())
            return current->second.value;
        if (m_presenters.size() >= 32 || m_nextSerial == 0)
            return {};
        serial = m_nextSerial++;
        generation = m_stats.generation;
        session = server()->session();
    }
    // The constructor starts its worker, but it cannot publish before a frame
    // is submitted. Construction, insertion failure, and race-loser close must
    // never run GPU/user cleanup while the mutex is held.
    auto candidate = std::make_shared<StreamPreview>(
        [this, id, serial, session](const SharedVideoFrame& frame) noexcept
        {
            bool inserted = false, sent = false;
            std::shared_ptr<void> retiredResource;
            try
            {
                {
                    std::lock_guard<std::mutex> guard(m_mutex);
                    const auto active = m_presenters.find(id);
                    if (m_stats.terminal || active == m_presenters.end() || active->second.serial != serial ||
                        session != server()->session() || frame.token == 0 || m_frames.size() >= 96 ||
                        !m_frames.emplace(frame.token, PublishedFrame {id, frame.resource}).second)
                    {
                        increment(m_stats.droppedPublications);
                        return false;
                    }
                    inserted = true;
                }
                this->emitFrameEvent(id, frame.token, frame.handle, frame.width, frame.height, frame.timestampUs);
                sent = true;
                {
                    std::lock_guard<std::mutex> guard(m_mutex);
                    if (sent)
                        increment(m_stats.published);
                    else
                    {
                        const auto published = m_frames.find(frame.token);
                        if (published != m_frames.end())
                        {
                            retiredResource = std::move(published->second.resource);
                            m_frames.erase(published);
                        }
                        increment(m_stats.droppedPublications);
                    }
                }
                return sent;
            }
            catch (...)
            {
                // Includes the first map allocation and lock acquisition. Only
                // unpublished resources may be rolled back: after a real send,
                // the consumer still owns its original id/token receipt.
                try
                {
                    std::lock_guard<std::mutex> guard(m_mutex);
                    if (!sent)
                    {
                        if (inserted)
                        {
                            const auto published = m_frames.find(frame.token);
                            if (published != m_frames.end())
                            {
                                retiredResource = std::move(published->second.resource);
                                m_frames.erase(published);
                            }
                        }
                        increment(m_stats.droppedPublications);
                    }
                }
                catch (...)
                { /* Broken mutex: final lifecycle still owns records. */
                }
                return sent;
            }
        },
        [id](const std::string& message) noexcept
        {
            try
            {
                mediaservice::logger().logError("streampreview", std::to_string(id) + ": " + message);
            }
            catch (...)
            { /* Error reporting cannot escape the GPU worker. */
            }
        });
    std::shared_ptr<StreamPreview> selected;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_stats.terminal && m_stats.generation == generation && server()->session() == session)
        {
            const auto current = m_presenters.find(id);
            if (current != m_presenters.end())
                selected = current->second.value;
            else if (m_presenters.size() < 32)
            {
                m_presenters.emplace(id, Presenter {candidate, serial});
                selected = candidate;
            }
        }
    }
    if (selected != candidate)
        candidate->close();
    return selected;
}

StreamPreviewService::Submission StreamPreviewService::submitFrameToAll(const VideoFrameView& frame,
                                                                        std::shared_ptr<const void> owner)
{
    std::vector<std::shared_ptr<StreamPreview>> presenters;
    Submission result;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        increment(m_stats.submittedFrames);
        result.terminal = m_stats.terminal;
        if (result.terminal)
            return result;
        presenters.reserve(m_presenters.size());
        for (const auto& entry : m_presenters)
            presenters.push_back(entry.second.value);
        ++m_stats.activeSubmissions;
    }
    for (const auto& presenter : presenters)
    {
        VideoSubmitResult outcome = VideoSubmitResult::InvalidFrame;
        try
        {
            outcome = presenter->submitFrame(frame, owner);
        }
        catch (...)
        { /* A failed consumer cannot prevent other consumers. */
        }
        if (outcome == VideoSubmitResult::Accepted)
            ++result.accepted;
        else if (outcome == VideoSubmitResult::Closed)
            ++result.retired;
        else
            ++result.rejected;
    }
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        increment(m_stats.submitted, static_cast<uint64_t>(presenters.size()));
        increment(m_stats.accepted, result.accepted);
        increment(m_stats.rejected, result.rejected);
        increment(m_stats.retired, result.retired);
        --m_stats.activeSubmissions;
    }
    return result;
}

StreamPreviewService::Stats StreamPreviewService::stats() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    Stats result = m_stats;
    result.activePresenters = static_cast<uint32_t>(m_presenters.size());
    result.publishedOutstanding = static_cast<uint32_t>(m_frames.size());
    return result;
}

void StreamPreviewService::release(uint32_t id)
{
    std::shared_ptr<StreamPreview> presenter;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        const auto it = m_presenters.find(id);
        if (it == m_presenters.end())
            return;
        presenter = std::move(it->second.value);
        m_presenters.erase(it);
    }
    presenter->close();
    // Published resources stay alive until the original id/token receipt.
}

void StreamPreviewService::releaseFrame(uint32_t id, uint64_t token)
{
    std::shared_ptr<void> resource;
    std::shared_ptr<StreamPreview> presenter;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        const auto frame = m_frames.find(token);
        if (frame == m_frames.end() || frame->second.id != id)
            return;
        resource = std::move(frame->second.resource);
        m_frames.erase(frame);
        const auto current = m_presenters.find(id);
        if (current != m_presenters.end())
            presenter = current->second.value;
    }
    if (presenter)
        presenter->releaseFrame(token);
}

void StreamPreviewService::closeAll()
{
    std::lock_guard<std::mutex> closeLock(m_closeMutex);
    decltype(m_presenters) presenters;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stats.terminal = true;
        m_stats.closing = true;
        presenters.swap(m_presenters);
    }
    for (auto& entry : presenters)
        entry.second.value->close();
    decltype(m_frames) frames;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        frames.swap(m_frames);
    }
    // GPU resources and custom owner deleters never run under m_mutex.
    frames.clear();
    presenters.clear();
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stats.closing = false;
    }
}

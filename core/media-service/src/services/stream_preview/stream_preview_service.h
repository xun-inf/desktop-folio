#pragma once

#include "service.h"

#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>

class IpcServer;
class StreamPreview;
struct VideoFrameView;

// 流预览业务：IPC Service 与预览管理合一（原 StreamPreviewMgr 收编进本类）。
// service_id = "StreamPreviewService"，与 addon 侧 ServiceProxy 同名映射。
class StreamPreviewService : public ServiceBase<StreamPreviewService>
{
public:
    explicit StreamPreviewService(IpcServer* server);
    ~StreamPreviewService() override;

    StreamPreviewService(const StreamPreviewService&) = delete;
    StreamPreviewService& operator=(const StreamPreviewService&) = delete;

    const char* serviceId() const override
    {
        return "StreamPreviewService";
    }

    // 统一工厂（services::registerAll 经 Service::Factory 调用）。
    static std::shared_ptr<Service> createService(IpcServer* server, EventLoop* eventLoop);

    // 帧事件发送（FrameReady 工作线程回调；线程安全）。
    void emitFrameEvent(std::uint32_t id, std::uint64_t token, std::uint64_t handle, std::uint32_t width,
                        std::uint32_t height, std::int64_t timestampUs);

    // 应用生命周期线程调用：停止预览并清理在途帧（可重复调用）。
    void closeAll() override;

private:
    // --- 业务方法（invoke 分发由 ServiceBase 完成）---
    bool createStreamPreview(const services::CreateStreamPreviewArgs& args, ReplyWriter& reply);
    bool releaseStreamPreview(const services::ReleaseStreamPreviewArgs& args, ReplyWriter& reply);
    bool releaseVideoFrame(const services::ReleaseVideoFrameArgs& args, ReplyWriter& reply);

    // --- 预览管理（原 StreamPreviewMgr 逻辑）---
    struct Submission
    {
        uint32_t accepted = 0, rejected = 0, retired = 0;
        bool terminal = false;
    };
    struct Stats
    {
        bool terminal = false, closing = false;
        uint64_t generation = 1, submittedFrames = 0;
        // submitted counts presenter attempts; retired is a Closed outcome.
        uint64_t submitted = 0, accepted = 0, rejected = 0, retired = 0;
        uint64_t published = 0, droppedPublications = 0;
        uint32_t activePresenters = 0, publishedOutstanding = 0, activeSubmissions = 0;
    };

    bool initialize();
    std::shared_ptr<StreamPreview> create(uint32_t id);
    Submission submitFrameToAll(const VideoFrameView& frame, std::shared_ptr<const void> owner);
    Stats stats() const;
    void release(uint32_t id);
    void releaseFrame(uint32_t id, uint64_t token);

    mutable std::mutex m_mutex;
    std::mutex m_closeMutex;
    struct Presenter
    {
        std::shared_ptr<StreamPreview> value;
        uint64_t serial;
    };
    std::unordered_map<uint32_t, Presenter> m_presenters;
    Stats m_stats;
    uint64_t m_nextSerial = 1;
    struct PublishedFrame
    {
        uint32_t id;
        std::shared_ptr<void> resource;
    };
    std::unordered_map<uint64_t, PublishedFrame> m_frames;
};

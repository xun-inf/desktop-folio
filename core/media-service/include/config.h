#pragma once

#include <string>
#include <cstdint>

// 命令行配置：media-service 由 Electron 拉起，参数都在命令行里
class Config
{
public:
    Config(int argc, char** argv);
    ~Config();

    // 命名管道名（\\.\pipe\xxx）
    std::string pipeName();

    // 可选日志文件路径（UTF-8）；空路径表示仅输出到控制台和调试器。
    const std::string& logPath() const;

    // An explicit local-file probe is separate from the default IPC service.
    bool sdkProbeRequested() const
    {
        return m_sdkProbeRequested && !m_sdkSceneServiceRequested && !m_sdkDeviceServiceRequested &&
               !m_sdkLiveServiceRequested;
    }
    bool sdkRuntimeRequested() const
    {
        return m_sdkProbeRequested;
    }
    bool sdkSceneServiceRequested() const
    {
        return m_sdkSceneServiceRequested;
    }
    bool sdkDeviceServiceRequested() const
    {
        return m_sdkDeviceServiceRequested;
    }
    bool sdkLiveServiceRequested() const
    {
        return m_sdkLiveServiceRequested;
    }
    bool sdkSceneMetadataVerified() const
    {
        return m_sdkSceneMetadataVerified;
    }
    bool sdkLifecycleProbeRequested() const
    {
        return m_sdkLifecycleProbeRequested;
    }
    const std::string& sdkLifecycleStage() const
    {
        return m_sdkLifecycleStage;
    }
    bool sdkDeviceProbeRequested() const
    {
        return m_sdkDeviceProbeRequested;
    }
    bool sdkSceneFramesRequested() const
    {
        return m_sdkSceneFramesRequested;
    }
    bool sdkSceneProbeRequested() const
    {
        return m_sdkSceneProbeRequested;
    }
    bool sdkProbeOptionsValid() const
    {
        return m_sdkProbeOptionsValid;
    }
    const std::string& sdkRoot() const
    {
        return m_sdkRoot;
    }
    const std::string& cryptoRoot() const
    {
        return m_cryptoRoot;
    }
    const std::string& sdkFile() const
    {
        return m_sdkFile;
    }
    const std::string& sdkReport() const
    {
        return m_sdkReport;
    }
    uint32_t sdkTargetFrames() const
    {
        return m_sdkTargetFrames;
    }
    uint32_t sdkTimeoutMs() const
    {
        return m_sdkTimeoutMs;
    }
    uint32_t sdkCycles() const
    {
        return m_sdkCycles;
    }

private:
    std::string m_pipeName;
    std::string m_logPath;
    bool m_sdkProbeRequested = false;
    bool m_sdkSceneServiceRequested = false;
    bool m_sdkDeviceServiceRequested = false;
    bool m_sdkLiveServiceRequested = false;
    bool m_sdkSceneProbeRequested = false;
    bool m_sdkSceneFramesRequested = false;
    bool m_sdkSceneMetadataVerified = false;
    bool m_sdkDeviceProbeRequested = false;
    bool m_sdkLifecycleProbeRequested = false;
    std::string m_sdkLifecycleStage;
    bool m_sdkProbeOptionsValid = true;
    std::string m_sdkRoot;
    std::string m_cryptoRoot;
    std::string m_sdkFile;
    std::string m_sdkReport;
    uint32_t m_sdkTargetFrames = 10;
    uint32_t m_sdkTimeoutMs = 15000;
    uint32_t m_sdkCycles = 1;
};

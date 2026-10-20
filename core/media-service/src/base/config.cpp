#include "config.h"
#include "utils/stringutil.h"

#if defined(_WIN32)
#include <windows.h>
#include <vector>
#endif

namespace
{

std::string argValue(int argc, char** argv, const std::string& name)
{
    const std::string prefix = "--" + name + "=";
    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i] != nullptr ? argv[i] : "";
        if (arg.rfind(prefix, 0) == 0)
        {
            return arg.substr(prefix.size());
        }
    }
    return std::string();
}

bool argPresent(int argc, char** argv, const std::string& name)
{
    const std::string prefix = "--" + name + "=";
    for (int i = 1; i < argc; ++i)
    {
        if (argv[i] != nullptr && std::string(argv[i]).rfind(prefix, 0) == 0)
            return true;
    }
    return false;
}

bool boundedNumber(const std::string& text, uint32_t minimum, uint32_t maximum, uint32_t& value)
{
    if (text.empty())
        return false;
    uint64_t number = 0;
    for (char digit : text)
    {
        if (digit < '0' || digit > '9')
            return false;
        number = number * 10 + static_cast<unsigned int>(digit - '0');
        if (number > maximum)
            return false;
    }
    if (number < minimum)
        return false;
    value = static_cast<uint32_t>(number);
    return true;
}

bool absolutePath(std::string& value)
{
    if (value.empty())
        return false;
#if defined(_WIN32)
    try
    {
        const std::wstring input = liveutils::utf8ToWide(value);
        const DWORD length = ::GetFullPathNameW(input.c_str(), 0, nullptr, nullptr);
        if (!length)
            return false;
        std::vector<wchar_t> buffer(length);
        const DWORD written = ::GetFullPathNameW(input.c_str(), length, buffer.data(), nullptr);
        if (!written || written >= length)
            return false;
        value = liveutils::wideToUtf8(std::wstring(buffer.data(), written));
        return true;
    }
    catch (...)
    {
        return false;
    }
#else
    return value[0] == '/';
#endif
}

} // namespace

Config::Config(int argc, char** argv)
{
    m_logPath = argValue(argc, argv, "log");
    m_pipeName = argValue(argc, argv, "pipe-name");
    const std::string sdkMode = argValue(argc, argv, "sdk-mode");
    m_sdkSceneServiceRequested = sdkMode == "scene-service";
    m_sdkDeviceServiceRequested = sdkMode == "device-service";
    m_sdkLiveServiceRequested = sdkMode == "live-service";
    m_sdkLifecycleProbeRequested = sdkMode == "lifecycle-probe";
    m_sdkLifecycleStage = argValue(argc, argv, "sdk-lifecycle-stage");
    m_sdkDeviceProbeRequested = sdkMode == "device-probe";
    m_sdkSceneMetadataVerified = argValue(argc, argv, "sdk-scene-metadata") == "601-full-ms";
    m_sdkSceneFramesRequested = sdkMode == "scene-frames";
    m_sdkSceneProbeRequested = sdkMode == "scene-probe" || m_sdkSceneFramesRequested || m_sdkDeviceProbeRequested ||
                               m_sdkLifecycleProbeRequested || m_sdkSceneServiceRequested;
    m_sdkRoot = argValue(argc, argv, "sdk-root");
    m_cryptoRoot = argValue(argc, argv, "sdk-crypto-root");
    m_sdkFile = argValue(argc, argv, "sdk-file");
    m_sdkReport = argValue(argc, argv, "sdk-report");
    const std::string frames = argValue(argc, argv, "sdk-target-frames");
    const std::string timeout = argValue(argc, argv, "sdk-timeout-ms");
    const std::string cycles = argValue(argc, argv, "sdk-cycles");
    m_sdkProbeRequested = argPresent(argc, argv, "sdk-mode") || argPresent(argc, argv, "sdk-root") ||
                          argPresent(argc, argv, "sdk-crypto-root") || argPresent(argc, argv, "sdk-file") ||
                          argPresent(argc, argv, "sdk-report") || argPresent(argc, argv, "sdk-target-frames") ||
                          argPresent(argc, argv, "sdk-timeout-ms") || argPresent(argc, argv, "sdk-cycles") ||
                          argPresent(argc, argv, "sdk-scene-metadata");
    if (m_sdkProbeRequested)
    {
        // Resolve every path before SDK initialization may change the child cwd.
        m_sdkProbeOptionsValid =
            (!argPresent(argc, argv, "sdk-mode") || sdkMode == "file-probe" || m_sdkSceneProbeRequested ||
             m_sdkDeviceServiceRequested || m_sdkLiveServiceRequested) &&
            absolutePath(m_sdkRoot);
        m_sdkProbeOptionsValid = absolutePath(m_cryptoRoot) && m_sdkProbeOptionsValid;
        if (!m_sdkDeviceProbeRequested && !m_sdkDeviceServiceRequested)
            m_sdkProbeOptionsValid = absolutePath(m_sdkFile) && m_sdkProbeOptionsValid;
        m_sdkProbeOptionsValid = absolutePath(m_sdkReport) && m_sdkProbeOptionsValid;
        if (m_sdkLifecycleProbeRequested)
            m_sdkProbeOptionsValid =
                (m_sdkLifecycleStage == "settings" || m_sdkLifecycleStage == "scene" ||
                 m_sdkLifecycleStage == "image" || m_sdkLifecycleStage == "scene-remove" ||
                 m_sdkLifecycleStage == "scene-remove-all" || m_sdkLifecycleStage == "image-operations") &&
                m_sdkProbeOptionsValid;
        else if (argPresent(argc, argv, "sdk-lifecycle-stage"))
            m_sdkProbeOptionsValid = false;
        if (argPresent(argc, argv, "sdk-scene-metadata"))
            m_sdkProbeOptionsValid = m_sdkSceneProbeRequested && !m_sdkDeviceProbeRequested &&
                                     !m_sdkSceneServiceRequested && m_sdkSceneMetadataVerified &&
                                     m_sdkProbeOptionsValid;
        if ((m_sdkSceneServiceRequested || m_sdkLiveServiceRequested) &&
            (argPresent(argc, argv, "sdk-target-frames") || argPresent(argc, argv, "sdk-timeout-ms") ||
             argPresent(argc, argv, "sdk-cycles")))
            m_sdkProbeOptionsValid = false;
        if (m_sdkDeviceServiceRequested &&
            (argPresent(argc, argv, "sdk-file") || argPresent(argc, argv, "sdk-target-frames") ||
             argPresent(argc, argv, "sdk-timeout-ms") || argPresent(argc, argv, "sdk-cycles") ||
             argPresent(argc, argv, "sdk-scene-metadata")))
            m_sdkProbeOptionsValid = false;
        if (!m_logPath.empty())
            m_sdkProbeOptionsValid = absolutePath(m_logPath) && m_sdkProbeOptionsValid;
        m_sdkProbeOptionsValid =
            (!argPresent(argc, argv, "sdk-target-frames") || boundedNumber(frames, 10, 300, m_sdkTargetFrames)) &&
            m_sdkProbeOptionsValid;
        m_sdkProbeOptionsValid =
            (!argPresent(argc, argv, "sdk-timeout-ms") || boundedNumber(timeout, 1000, 60000, m_sdkTimeoutMs)) &&
            m_sdkProbeOptionsValid;
        m_sdkProbeOptionsValid = (!argPresent(argc, argv, "sdk-cycles") || boundedNumber(cycles, 1, 3, m_sdkCycles)) &&
                                 m_sdkProbeOptionsValid;
    }
}

Config::~Config()
{
}

std::string Config::pipeName()
{
    return m_pipeName;
}

const std::string& Config::logPath() const
{
    return m_logPath;
}

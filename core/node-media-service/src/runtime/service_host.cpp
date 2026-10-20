#include "runtime/service_host.h"

#include "runtime/napi_util.h"
#include "services/service_registry.h"
#include "protos/service.h"

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace node_media_service
{
namespace
{

// ---- 小工具 ----

std::wstring utf8ToWide(const std::string& value)
{
    if (value.empty())
    {
        return std::wstring();
    }
    const int count = ::MultiByteToWideChar(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), nullptr, 0);
    std::wstring result(static_cast<std::size_t>(count), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), &result[0], count);
    return result;
}

std::string wideToUtf8(const std::wstring& value)
{
    if (value.empty())
    {
        return std::string();
    }
    const int count =
        ::WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<std::size_t>(count), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), &result[0], count, nullptr,
                          nullptr);
    return result;
}

ServiceHost* hostFromInfo(napi_env env, napi_callback_info info)
{
    void* data = nullptr;
    NMS_NAPI(napi_get_cb_info)(env, info, nullptr, nullptr, nullptr, &data);
    return static_cast<ServiceHost*>(data);
}

void addHostMethod(napi_env env, napi_value target, const char* name, napi_callback callback, ServiceHost* host)
{
    napi_value function = nullptr;
    NMS_NAPI(napi_create_function)(env, name, NAPI_AUTO_LENGTH, callback, host, &function);
    NMS_NAPI(napi_set_named_property)(env, target, name, function);
}

// ---- root 方法回调 ----

napi_value hostInitialize(napi_env env, napi_callback_info info)
{
    ServiceHost* host = hostFromInfo(env, info);
    return host->initialize(env);
}

napi_value hostGetStatus(napi_env env, napi_callback_info info)
{
    ServiceHost* host = hostFromInfo(env, info);
    return host->getStatus(env);
}

napi_value hostSubscribeStatus(napi_env env, napi_callback_info info)
{
    ServiceHost* host = hostFromInfo(env, info);
    napi_value args[1] = {};
    std::size_t count = 1;
    NMS_NAPI(napi_get_cb_info)(env, info, &count, args, nullptr, nullptr);
    if (count < 1)
    {
        throw std::runtime_error("Missing status callback");
    }
    return host->subscribeStatus(env, args[0]);
}

napi_value hostStop(napi_env env, napi_callback_info info)
{
    ServiceHost* host = hostFromInfo(env, info);
    return host->stop(env);
}

// ---- 状态订阅 dispose ----

struct StatusDisposeContext
{
    ServiceHost* host = nullptr;
    StatusCallback* entry = nullptr;
    bool disposed = false;
};

napi_value statusDispose(napi_env env, napi_callback_info info)
{
    void* data = nullptr;
    NMS_NAPI(napi_get_cb_info)(env, info, nullptr, nullptr, nullptr, &data);
    StatusDisposeContext* context = static_cast<StatusDisposeContext*>(data);
    if (context != nullptr && !context->disposed)
    {
        context->disposed = true;
        if (context->entry != nullptr && !context->entry->disposed)
        {
            context->entry->disposed = true;
            NMS_NAPI(napi_delete_reference)(env, context->entry->callback);
        }
        context->host->removeStatusCallback(context->entry);
    }
    napi_value result = nullptr;
    NMS_NAPI(napi_get_undefined)(env, &result);
    return result;
}

void statusDisposeFinalizer(napi_env, void* data, void*)
{
    delete static_cast<StatusDisposeContext*>(data);
}

// 状态通知 TSFN 的 JS 线程入口。
void statusCallJs(napi_env env, napi_value, void* context, void*)
{
    // Aborted TSFN queues are drained without a live JavaScript environment.
    if (env == nullptr || context == nullptr)
        return;
    ServiceHost* host = static_cast<ServiceHost*>(context);
    host->notifyStatusOnJs();
}

// ---- root finalizer ----

struct RootFinalizerData
{
    ServiceHost* host = nullptr;
};

void rootFinalizer(napi_env, void* data, void*)
{
    RootFinalizerData* finalizer = static_cast<RootFinalizerData*>(data);
    if (finalizer == nullptr)
    {
        return;
    }
    finalizer->host->shutdown();
    delete finalizer->host;
    delete finalizer;
}

// ---- stop 的 async work ----

struct StopContext
{
    ServiceHost* host = nullptr;
    napi_env env = nullptr;
    napi_deferred deferred = nullptr;
    napi_async_work work = nullptr;
    bool completed = false;
};

void stopExecute(napi_env, void* data)
{
    StopContext* context = static_cast<StopContext*>(data);
    context->host->shutdown();
}

void stopComplete(napi_env env, napi_status, void* data)
{
    StopContext* context = static_cast<StopContext*>(data);
    if (context->deferred != nullptr)
    {
        NMS_NAPI(napi_resolve_deferred)(env, context->deferred, undefined(env));
    }
    if (context->work != nullptr)
    {
        NMS_NAPI(napi_delete_async_work)(env, context->work);
    }
    delete context;
}

} // namespace

// 供 statusDispose 使用：从订阅表移除并释放。
void ServiceHost::removeStatusCallback(StatusCallback* entry)
{
    if (entry == nullptr)
    {
        return;
    }
    std::lock_guard<std::mutex> lock(m_subMutex);
    for (auto it = m_statusCallbacks.begin(); it != m_statusCallbacks.end(); ++it)
    {
        if (it->get() == entry)
        {
            m_statusCallbacks.erase(it);
            return;
        }
    }
}

ServiceHost::ServiceHost(napi_env env) : m_env(env)
{
    napi_value resourceName = nullptr;
    NMS_NAPI(napi_create_string_utf8)(env, "media-service-status", NAPI_AUTO_LENGTH, &resourceName);
    NMS_NAPI(napi_create_threadsafe_function)(env, nullptr, nullptr, resourceName, 0, 1, nullptr, nullptr, this,
                                              statusCallJs, &m_statusTsfn);
}

ServiceHost::~ServiceHost()
{
    shutdown();
    if (m_statusTsfn != nullptr)
    {
        NMS_NAPI(napi_release_threadsafe_function)(m_statusTsfn, napi_tsfn_abort);
        m_statusTsfn = nullptr;
    }
}

bool ServiceHost::start(const ServiceHostOptions& options)
{
    if (m_stopped.load() || m_ready.load())
    {
        return false;
    }
    m_shutdownTimeoutMs = options.shutdownTimeoutMs;
    m_transport.reset(new Transport(m_env));
    m_services.clear();
    for (std::size_t index = 0; index < serviceFactoryCount(); ++index)
    {
        const ServiceFactory& factory = serviceFactories()[index];
        m_services.push_back(std::unique_ptr<ServiceProxy>(factory.create(m_transport.get())));
    }
    setPhase("starting", "starting media service");
    if (!startProcess(options))
    {
        setPhase("stopped", "failed to start media service process");
        return false;
    }
    setPhase("handshaking", "connecting to media service");
    if (!m_transport->connect(options.pipePath, options.startupTimeoutMs))
    {
        setPhase("stopped", "failed to connect to media service pipe");
        shutdown();
        return false;
    }
    if (!handshake(options.startupTimeoutMs))
    {
        setPhase("stopped", "media service handshake failed");
        shutdown();
        return false;
    }
    m_ready.store(true);
    m_connected.store(true);
    setPhase("ready", "media service ready");
    return true;
}

std::vector<ServiceProxy*> ServiceHost::services() const
{
    std::vector<ServiceProxy*> result;
    result.reserve(m_services.size());
    for (const auto& service : m_services)
    {
        result.push_back(service.get());
    }
    return result;
}

bool ServiceHost::startProcess(const ServiceHostOptions& options)
{
    std::wstring commandLine = L"\"" + options.executable + L"\" --pipe-name=\"" + options.pipePath + L"\"";
    if (!options.logPath.empty())
    {
        commandLine += L" --log=\"" + options.logPath + L"\"";
    }
    for (const std::wstring& argument : options.arguments)
    {
        commandLine += L" \"";
        commandLine += argument;
        commandLine += L"\"";
    }
    STARTUPINFOW startupInfo {};
    startupInfo.cb = sizeof(startupInfo);
    PROCESS_INFORMATION processInfo {};
    if (!::CreateProcessW(options.executable.c_str(), &commandLine[0], nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                          nullptr, nullptr, &startupInfo, &processInfo))
    {
        return false;
    }
    ::CloseHandle(processInfo.hThread);
    m_process = processInfo.hProcess;
    m_pid = processInfo.dwProcessId;
    m_transport->setChildPid(m_pid);
    m_processMonitor = std::thread(
        [this]()
        {
            ::WaitForSingleObject(m_process, INFINITE);
            if (!m_stopped.load())
            {
                onDisconnected();
            }
        });
    return true;
}

bool ServiceHost::handshake(DWORD timeoutMs)
{
    flatbuffers::FlatBufferBuilder builder;
    const auto args = services::CreateHelloArgs(builder, 1);
    builder.Finish(args);
    const std::vector<std::uint8_t> frame =
        m_transport->invokeSync("SessionService", "hello", builder.GetBufferPointer(), builder.GetSize(), timeoutMs);
    if (frame.empty())
    {
        return false;
    }
    flatbuffers::Verifier verifier(frame.data(), frame.size());
    if (!services::VerifyInvokeRspBuffer(verifier))
    {
        return false;
    }
    const services::InvokeRsp* reply = services::GetInvokeRsp(frame.data());
    if (!reply->success())
    {
        return false;
    }
    const flatbuffers::Vector<std::uint8_t>* result = reply->result();
    if (result == nullptr || result->size() == 0)
    {
        return false;
    }
    flatbuffers::Verifier resultVerifier(result->data(), result->size());
    if (!services::VerifyHelloResultBuffer(resultVerifier))
    {
        return false;
    }
    const services::HelloResult* hello = services::GetHelloResult(result->data());
    if (!hello->compatible())
    {
        return false;
    }
    m_transport->setSession(hello->session_id());
    return true;
}

void ServiceHost::shutdown()
{
    if (m_stopped.exchange(true))
    {
        return;
    }
    setPhase("stopped", "stopping");
    m_connected.store(false);
    if (m_transport)
    {
        m_transport->disconnect();
    }
    if (m_process != nullptr)
    {
        ::WaitForSingleObject(m_process, m_shutdownTimeoutMs);
        if (::WaitForSingleObject(m_process, 0) == WAIT_TIMEOUT)
        {
            ::TerminateProcess(m_process, 1);
            ::WaitForSingleObject(m_process, INFINITE);
        }
        ::CloseHandle(m_process);
        m_process = nullptr;
    }
    if (m_processMonitor.joinable())
    {
        m_processMonitor.join();
    }
    // Release while stop still owns a live environment. Waiting for the root
    // finalizer leaves the TSFN's AsyncResource alive past Electron teardown.
    if (m_statusTsfn != nullptr)
    {
        NMS_NAPI(napi_release_threadsafe_function)(m_statusTsfn, napi_tsfn_abort);
        m_statusTsfn = nullptr;
    }
}

void ServiceHost::onDisconnected()
{
    if (m_stopped.load())
    {
        return;
    }
    m_connected.store(false);
    if (m_transport)
    {
        m_transport->disconnect();
    }
    setPhase("disconnected", "media service process exited");
    notifyStatus();
}

void ServiceHost::setPhase(const std::string& phase, const std::string& message)
{
    std::lock_guard<std::mutex> lock(m_statusMutex);
    m_phase = phase;
    m_message = message;
}

void ServiceHost::notifyStatus()
{
    if (m_statusTsfn != nullptr && !m_stopped.load())
    {
        NMS_NAPI(napi_call_threadsafe_function)(m_statusTsfn, nullptr, napi_tsfn_blocking);
    }
}

void ServiceHost::notifyStatusOnJs()
{
    napi_value status = buildStatus(m_env);
    std::vector<napi_ref> callbacks;
    {
        std::lock_guard<std::mutex> lock(m_subMutex);
        for (const auto& entry : m_statusCallbacks)
        {
            if (!entry->disposed)
            {
                callbacks.push_back(entry->callback);
            }
        }
    }
    napi_value global = nullptr;
    NMS_NAPI(napi_get_global)(m_env, &global);
    for (napi_ref ref : callbacks)
    {
        napi_value callback = nullptr;
        NMS_NAPI(napi_get_reference_value)(m_env, ref, &callback);
        napi_value argv[] = {status};
        napi_value ignored = nullptr;
        NMS_NAPI(napi_call_function)(m_env, global, callback, 1, argv, &ignored);
    }
}

napi_value ServiceHost::initialize(napi_env env)
{
    if (m_ready.load())
    {
        return resolved(env, undefined(env), "");
    }
    return rejected(env, "Media service is not ready");
}

napi_value ServiceHost::stop(napi_env env)
{
    StopContext* context = new StopContext();
    context->host = this;
    context->env = env;
    napi_value promise = nullptr;
    NMS_NAPI(napi_create_promise)(env, &context->deferred, &promise);
    napi_value resourceName = nullptr;
    NMS_NAPI(napi_create_string_utf8)(env, "media-service-stop", NAPI_AUTO_LENGTH, &resourceName);
    NMS_NAPI(napi_create_async_work)(env, nullptr, resourceName, stopExecute, stopComplete, context, &context->work);
    NMS_NAPI(napi_queue_async_work)(env, context->work);
    return promise;
}

napi_value ServiceHost::getStatus(napi_env env)
{
    return buildStatus(env);
}

napi_value ServiceHost::buildStatus(napi_env env)
{
    napi_value status = object(env);
    std::string phase;
    std::string message;
    {
        std::lock_guard<std::mutex> lock(m_statusMutex);
        phase = m_phase;
        message = m_message;
    }
    const bool connected = m_connected.load() && m_ready.load();
    property(env, status, "connected", boolean(env, connected));
    property(env, status, "message", string(env, message));
    property(env, status, "phase", string(env, phase));
    property(env, status, "pid", number(env, static_cast<double>(m_pid)));
    property(env, status, "session", string(env, std::to_string(m_transport ? m_transport->session() : 0)));
    napi_value nativeClose = object(env);
    property(env, nativeClose, "success", boolean(env, m_stopped.load()));
    property(env, nativeClose, "processPresent", boolean(env, m_process != nullptr));
    property(env, nativeClose, "forced", boolean(env, false));
    property(env, nativeClose, "exitCodeKnown", boolean(env, false));
    property(env, nativeClose, "waitResult", number(env, 0));
    property(env, nativeClose, "exitCode", number(env, 0));
    property(env, nativeClose, "jobAccountingKnown", boolean(env, false));
    property(env, nativeClose, "jobActiveAtExit", number(env, 0));
    property(env, nativeClose, "jobActiveAfterDrain", number(env, 0));
    property(env, status, "nativeClose", nativeClose);
    return status;
}

napi_value ServiceHost::subscribeStatus(napi_env env, napi_value callback)
{
    napi_valuetype type = napi_undefined;
    NMS_NAPI(napi_typeof)(env, callback, &type);
    if (type != napi_function)
    {
        throw std::runtime_error("Expected a status callback");
    }
    std::unique_ptr<StatusCallback> entry(new StatusCallback());
    NMS_NAPI(napi_create_reference)(env, callback, 1, &entry->callback);
    StatusCallback* raw = entry.get();
    {
        std::lock_guard<std::mutex> lock(m_subMutex);
        m_statusCallbacks.push_back(std::move(entry));
    }
    StatusDisposeContext* context = new StatusDisposeContext();
    context->host = this;
    context->entry = raw;
    napi_value dispose = nullptr;
    NMS_NAPI(napi_create_function)(env, "dispose", NAPI_AUTO_LENGTH, statusDispose, context, &dispose);
    NMS_NAPI(napi_add_finalizer)(env, dispose, context, statusDisposeFinalizer, nullptr, nullptr);
    keepOwner(env, dispose, callback);
    napi_value result = object(env);
    property(env, result, "dispose", dispose);
    property(env, result, "ready", resolved(env, undefined(env), ""));
    return result;
}

napi_value createServiceRoot(napi_env env, ServiceHost* host)
{
    napi_value root = object(env);
    addHostMethod(env, root, "initialize", hostInitialize, host);
    addHostMethod(env, root, "getStatus", hostGetStatus, host);
    addHostMethod(env, root, "subscribeStatus", hostSubscribeStatus, host);
    addHostMethod(env, root, "stop", hostStop, host);
    const std::vector<ServiceProxy*> services = host->services();
    installAllServices(env, root, root, services);
    RootFinalizerData* finalizer = new RootFinalizerData();
    finalizer->host = host;
    NMS_NAPI(napi_add_finalizer)(env, root, finalizer, rootFinalizer, nullptr, nullptr);
    return root;
}
} // namespace node_media_service

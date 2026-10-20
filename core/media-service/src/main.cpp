#include "application.h"
#include "base/logger.h"

#if defined(_WIN32)
#include <windows.h>

#include <string>
#include <utility>
#include <vector>
#endif

namespace
{

// media-service：Electron 拉起的子进程，提供 IPC 服务与可视组件。
int runApplication(int argc, char** argv)
{
    MediaServiceApplication app(argc, argv);
    if (!app.initialize())
        return 1;
    return app.exec() ? 0 : 1;
}

} // namespace

#if defined(_WIN32)
int wmain(int argc, wchar_t** argv)
{
    std::vector<std::vector<char>> utf8Args;
    utf8Args.reserve(argc);
    for (int i = 0; i < argc; ++i)
    {
        const int size =
            ::WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, argv[i], -1, nullptr, 0, nullptr, nullptr);
        if (size == 0)
        {
            mediaservice::logger().logError("app", "failed to encode command-line arguments as UTF-8");
            return 1;
        }

        std::vector<char> arg(static_cast<std::size_t>(size), '\0');
        if (::WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, argv[i], -1, arg.data(), size, nullptr, nullptr) !=
            size)
        {
            mediaservice::logger().logError("app", "failed to encode command-line arguments as UTF-8");
            return 1;
        }
        utf8Args.push_back(std::move(arg));
    }

    std::vector<char*> utf8Argv;
    utf8Argv.reserve(static_cast<std::size_t>(argc) + 1);
    for (std::vector<char>& arg : utf8Args)
    {
        utf8Argv.push_back(arg.data());
    }
    utf8Argv.push_back(nullptr);
    return runApplication(argc, utf8Argv.data());
}
#else
int main(int argc, char** argv)
{
    return runApplication(argc, argv);
}
#endif

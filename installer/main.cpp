#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <shlobj.h>
#include <shellapi.h>
#include <gdiplus.h>
#include <uxtheme.h>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>
#include <memory>
#include <algorithm>

namespace fs = std::filesystem;
using namespace Gdiplus;
namespace
{
constexpr UINT WM_PHASE = WM_APP + 1, WM_DONE = WM_APP + 2;
constexpr int Width = 640, Height = 483;
constexpr int Primary = 100, Browse = 101, Close = 102, Minimize = 103, PathEdit = 104;
enum class Page
{
    Welcome,
    Installing,
    Complete,
    Failed
};
HWND windowHandle {}, pathEdit {}, primaryButton {}, browseButton {}, closeButton {}, minButton {};
HINSTANCE instance {};
HFONT font {};
HBRUSH editBrush {};
HICON appIcon {};
UINT dpi = 96;
Page page = Page::Welcome;
std::wstring installDir, statusText, errorText;
std::thread worker;
DWORD exitCode = 0;
bool preview = false;
int animation = 0;

int px(int value)
{
    return MulDiv(value, dpi, 96);
}
std::wstring systemError(DWORD code)
{
    wchar_t* text = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr,
                   code, 0, reinterpret_cast<LPWSTR>(&text), 0, nullptr);
    std::wstring result = text ? text : L"未知错误";
    if (text)
        LocalFree(text);
    return result;
}
std::wstring readInstallDir(HKEY root)
{
    wchar_t value[1024] {};
    DWORD size = sizeof(value);
    const auto key = std::wstring(L"Software\\") + SETUP_GUID;
    if (RegGetValueW(root, key.c_str(), L"InstallLocation", RRF_RT_REG_SZ | RRF_SUBKEY_WOW6464KEY, nullptr, value,
                     &size) == ERROR_SUCCESS)
        return value;
    return {};
}
std::wstring defaultDir()
{
    auto previous = readInstallDir(HKEY_CURRENT_USER);
    if (!previous.empty())
        return previous;
    PWSTR local = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local)))
        throw std::runtime_error("LocalAppData is unavailable");
    auto result = (fs::path(local) / L"Programs" / L"Folio").wstring();
    CoTaskMemFree(local);
    return result;
}
std::wstring validateDir(const std::wstring& value)
{
    if (value.size() < 4 || value.size() > 180 || !iswalpha(value[0]) || value[1] != L':' || value[2] != L'\\' ||
        value.find_first_of(L"<>\"|?*\r\n/") != std::wstring::npos || value.find(L':', 2) != std::wstring::npos)
        return L"请选择本地磁盘中的应用文件夹，例如 D:\\Apps\\Folio。";
    fs::path target(value);
    for (const auto& part : target.relative_path())
    {
        auto name = part.wstring();
        if (name == L"." || name == L".." || (!name.empty() && (name.back() == L'.' || name.back() == L' ')))
            return L"安装路径不能包含相对路径、末尾空格或末尾句点。";
    }
    std::error_code ec;
    if (fs::exists(target, ec))
    {
        if (!fs::is_directory(target, ec))
            return L"安装位置已被文件占用，请选择其他文件夹。";
        if (!fs::is_empty(target, ec) && !fs::exists(target / L"folio.exe", ec))
            return L"请选择空文件夹或已有的 Folio 安装目录。";
    }
    auto ancestor = target;
    while (!ancestor.empty() && !fs::exists(ancestor, ec))
    {
        auto parent = ancestor.parent_path();
        if (parent == ancestor)
            break;
        ancestor = parent;
    }
    auto capacity = fs::space(ancestor, ec);
    if (ec)
        return L"无法访问这个磁盘，请检查安装路径。";
    if (capacity.available < SETUP_REQUIRED_BYTES)
        return L"目标磁盘剩余空间不足，请选择其他安装位置。";
    return {};
}

// The embedded NSIS payload owns installation, shortcuts, upgrade and uninstall registration.
// This process only presents the UI and waits for its real exit code.
DWORD install(const std::wstring& target, HWND notify)
{
    fs::path temporary;
    HANDLE file = INVALID_HANDLE_VALUE;
    DWORD result = ERROR_INSTALL_FAILURE;
    try
    {
        auto resource = FindResourceW(instance, MAKEINTRESOURCEW(101), RT_RCDATA);
        if (!resource)
            throw GetLastError();
        auto loaded = LoadResource(instance, resource);
        auto bytes = static_cast<const BYTE*>(LockResource(loaded));
        const DWORD size = SizeofResource(instance, resource);
        if (!bytes || size == 0)
            throw DWORD(ERROR_RESOURCE_DATA_NOT_FOUND);
        GUID guid {};
        if (FAILED(CoCreateGuid(&guid)))
            throw DWORD(ERROR_GEN_FAILURE);
        wchar_t id[40] {};
        StringFromGUID2(guid, id, 40);
        temporary = fs::temp_directory_path() / (std::wstring(L"FolioSetup-") + id);
        fs::create_directory(temporary);
        if (fs::space(temporary).available < static_cast<uintmax_t>(size) + 32 * 1024 * 1024)
            throw DWORD(ERROR_DISK_FULL);
        const auto payload = temporary / L"install.exe";
        file = CreateFileW(payload.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
            throw GetLastError();
        for (DWORD offset = 0; offset < size;)
        {
            DWORD written = 0;
            const DWORD count = std::min<DWORD>(size - offset, 1024 * 1024);
            if (!WriteFile(file, bytes + offset, count, &written, nullptr) || written != count)
                throw DWORD(ERROR_WRITE_FAULT);
            offset += written;
        }
        CloseHandle(file);
        file = INVALID_HANDLE_VALUE;
        if (notify)
            PostMessageW(notify, WM_PHASE, 1, 0);
        // /D must be the last NSIS argument, and is intentionally not quoted.
        std::wstring command = L"\"" + payload.wstring() + L"\" /S /currentuser /D=" + target;
        STARTUPINFOW startup {sizeof(startup)};
        startup.dwFlags = STARTF_USESHOWWINDOW;
        startup.wShowWindow = SW_HIDE;
        PROCESS_INFORMATION process {};
        if (!CreateProcessW(payload.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr, temporary.c_str(),
                            &startup, &process))
            throw GetLastError();
        CloseHandle(process.hThread);
        WaitForSingleObject(process.hProcess, INFINITE);
        if (!GetExitCodeProcess(process.hProcess, &result))
            result = GetLastError();
        CloseHandle(process.hProcess);
        if (result == 0 && !fs::exists(fs::path(target) / L"folio.exe"))
            result = ERROR_FILE_NOT_FOUND;
    }
    catch (DWORD code)
    {
        result = code;
    }
    catch (const fs::filesystem_error& error)
    {
        result = static_cast<DWORD>(error.code().value());
    }
    catch (...)
    {
        result = ERROR_INSTALL_FAILURE;
    }
    if (file != INVALID_HANDLE_VALUE)
        CloseHandle(file);
    if (!temporary.empty())
    {
        std::error_code ignored;
        // Only this invocation's unique extraction directory is ever removed.
        fs::remove_all(temporary, ignored);
    }
    return result;
}

void rounded(Graphics& graphics, Color color, RectF rect, float radius)
{
    GraphicsPath shape;
    const float diameter = radius * 2;
    shape.AddArc(rect.X, rect.Y, diameter, diameter, 180, 90);
    shape.AddArc(rect.GetRight() - diameter, rect.Y, diameter, diameter, 270, 90);
    shape.AddArc(rect.GetRight() - diameter, rect.GetBottom() - diameter, diameter, diameter, 0, 90);
    shape.AddArc(rect.X, rect.GetBottom() - diameter, diameter, diameter, 90, 90);
    shape.CloseFigure();
    SolidBrush brush(color);
    graphics.FillPath(&brush, &shape);
}
void text(Graphics& graphics, const std::wstring& value, float size, Color color, RectF rect,
          StringAlignment align = StringAlignmentCenter, bool bold = false)
{
    FontFamily family(L"Microsoft YaHei UI");
    Gdiplus::Font face(&family, size, bold ? FontStyleBold : FontStyleRegular, UnitPixel);
    StringFormat format;
    format.SetAlignment(align);
    format.SetLineAlignment(StringAlignmentCenter);
    format.SetTrimming(StringTrimmingEllipsisCharacter);
    SolidBrush brush(color);
    graphics.DrawString(value.c_str(), -1, &face, rect, &format, &brush);
}
void setPage(Page next)
{
    page = next;
    ShowWindow(pathEdit, next == Page::Welcome ? SW_SHOW : SW_HIDE);
    ShowWindow(browseButton, next == Page::Welcome ? SW_SHOW : SW_HIDE);
    ShowWindow(primaryButton, next == Page::Installing ? SW_HIDE : SW_SHOW);
    EnableWindow(closeButton, next != Page::Installing);
    SetWindowTextW(primaryButton, next == Page::Complete ? L"立即体验"
                                  : next == Page::Failed ? L"返回重试"
                                                         : L"一键安装");
    if (next == Page::Installing)
        SetTimer(windowHandle, 1, 30, nullptr);
    else
        KillTimer(windowHandle, 1);
    InvalidateRect(windowHandle, nullptr, FALSE);
}
void layout()
{
    if (font)
        DeleteObject(font);
    font = CreateFontW(-px(13), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                       CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
    SendMessageW(pathEdit, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    MoveWindow(pathEdit, px(215), px(328), px(244), px(22), TRUE);
    MoveWindow(browseButton, px(467), px(320), px(40), px(38), TRUE);
    MoveWindow(primaryButton, px(132), px(379), px(376), px(44), TRUE);
    MoveWindow(minButton, px(560), px(10), px(32), px(30), TRUE);
    MoveWindow(closeButton, px(596), px(10), px(32), px(30), TRUE);
}
void paint(HWND hwnd, HDC target = nullptr)
{
    PAINTSTRUCT ps {};
    HDC dc = target ? target : BeginPaint(hwnd, &ps);
    RECT client {};
    GetClientRect(hwnd, &client);
    HDC buffer = CreateCompatibleDC(dc);
    HBITMAP bitmap = CreateCompatibleBitmap(dc, client.right, client.bottom);
    HGDIOBJ previous = SelectObject(buffer, bitmap);
    {
        Graphics graphics(buffer);
        graphics.SetSmoothingMode(SmoothingModeAntiAlias);
        graphics.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
        graphics.ScaleTransform(dpi / 96.f, dpi / 96.f);
        graphics.Clear(Color(255, 34, 34, 34));
        Pen border(Color(255, 68, 68, 68), 1);
        graphics.DrawRectangle(&border, 0.5f, 0.5f, 639.f, 482.f);
        text(graphics, L"FOLIO", 11, Color(255, 150, 150, 150), RectF(24, 18, 200, 18), StringAlignmentNear, true);
        rounded(graphics, Color(255, 43, 43, 43), RectF(272, 83, 96, 96), 24);
        if (appIcon)
        {
            HDC iconDc = graphics.GetHDC();
            DrawIconEx(iconDc, px(288), px(99), appIcon, px(64), px(64), 0, nullptr, DI_NORMAL);
            graphics.ReleaseHDC(iconDc);
        }
        text(graphics, L"Folio", 29, Color(255, 247, 247, 248), RectF(60, 196, 520, 42), StringAlignmentCenter, true);
        const wchar_t* subtitle = page == Page::Complete ? L"安装完成，开启你的桌面工作空间"
                                  : page == Page::Failed ? L"安装未完成"
                                                         : L"让桌面工作，更从容";
        text(graphics, subtitle, 14, Color(255, 158, 158, 166), RectF(60, 246, 520, 24));
        if (page == Page::Welcome)
        {
            rounded(graphics, Color(255, 48, 48, 48), RectF(132, 320, 376, 38), 6);
            text(graphics, L"安装位置", 12, Color(255, 153, 153, 161), RectF(146, 320, 65, 38), StringAlignmentNear);
            text(graphics, L"仅为当前用户安装 · 创建桌面快捷方式", 11, Color(255, 125, 125, 134),
                 RectF(100, 435, 440, 20));
        }
        else if (page == Page::Installing)
        {
            rounded(graphics, Color(255, 53, 53, 58), RectF(132, 355, 376, 5), 2.5f);
            const float offset = static_cast<float>(animation % 140) / 139.f * 266;
            rounded(graphics, Color(255, 255, 58, 98), RectF(132 + offset, 355, 110, 5), 2.5f);
            text(graphics, statusText, 13, Color(255, 208, 208, 214), RectF(100, 377, 440, 24));
            text(graphics, L"请稍候，完成后即可开始使用", 11, Color(255, 125, 125, 134), RectF(100, 415, 440, 22));
        }
        else if (page == Page::Complete)
        {
            text(graphics, L"已就绪", 16, Color(255, 100, 216, 176), RectF(100, 312, 440, 32));
        }
        else
        {
            text(graphics, errorText, 12, Color(255, 240, 154, 164), RectF(92, 289, 456, 70));
        }
        text(graphics, L"v" SETUP_VERSION L"  /  Windows 64 位", 10, Color(255, 106, 106, 116), RectF(24, 459, 260, 15),
             StringAlignmentNear);
    }
    BitBlt(dc, 0, 0, client.right, client.bottom, buffer, 0, 0, SRCCOPY);
    SelectObject(buffer, previous);
    DeleteObject(bitmap);
    DeleteDC(buffer);
    if (!target)
        EndPaint(hwnd, &ps);
}
void paintButton(const DRAWITEMSTRUCT* item)
{
    Graphics graphics(item->hDC);
    graphics.SetSmoothingMode(SmoothingModeAntiAlias);
    graphics.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
    graphics.ScaleTransform(dpi / 96.f, dpi / 96.f);
    const float width = (item->rcItem.right - item->rcItem.left) * 96.f / dpi;
    const float height = (item->rcItem.bottom - item->rcItem.top) * 96.f / dpi;
    graphics.Clear(Color(255, 34, 34, 34));
    const bool down = (item->itemState & ODS_SELECTED) != 0;
    POINT cursor {};
    GetCursorPos(&cursor);
    ScreenToClient(item->hwndItem, &cursor);
    const bool hot = IsWindowVisible(item->hwndItem) && PtInRect(&item->rcItem, cursor);
    if (item->CtlID == Primary)
    {
        rounded(graphics,
                down  ? Color(255, 225, 37, 78)
                : hot ? Color(255, 255, 78, 114)
                      : Color(255, 255, 58, 98),
                RectF(0, 0, width, height), 7);
        wchar_t label[64] {};
        GetWindowTextW(item->hwndItem, label, 64);
        text(graphics, label, 15, Color(255, 255, 255, 255), RectF(0, 0, width, height), StringAlignmentCenter, true);
    }
    else
    {
        if (item->CtlID == Browse)
            graphics.Clear(Color(255, 48, 48, 48));
        if (hot && IsWindowEnabled(item->hwndItem))
            rounded(graphics, Color(255, 62, 62, 66), RectF(3, 3, width - 6, height - 6), 4);
        Pen pen((item->itemState & ODS_DISABLED) ? Color(255, 80, 80, 86) : Color(255, 182, 182, 190), 1.3f);
        if (item->CtlID == Close)
        {
            graphics.DrawLine(&pen, 12.f, 11.f, 21.f, 20.f);
            graphics.DrawLine(&pen, 21.f, 11.f, 12.f, 20.f);
        }
        else if (item->CtlID == Minimize)
            graphics.DrawLine(&pen, 11.f, 16.f, 22.f, 16.f);
        else
        {
            graphics.DrawRectangle(&pen, 11.f, 14.f, 17.f, 12.f);
            graphics.DrawLine(&pen, 11.f, 14.f, 11.f, 11.f);
            graphics.DrawLine(&pen, 11.f, 11.f, 19.f, 11.f);
            graphics.DrawLine(&pen, 19.f, 11.f, 22.f, 14.f);
        }
    }
    if (item->itemState & ODS_FOCUS)
    {
        Pen focus(Color(255, 239, 239, 244), 1);
        focus.SetDashStyle(DashStyleDot);
        graphics.DrawRectangle(&focus, 3.f, 3.f, width - 6, height - 6);
    }
}
bool capturePreview(HWND hwnd, const std::wstring& destination)
{
    Bitmap image(px(Width), px(Height), PixelFormat32bppARGB);
    {
        Graphics graphics(&image);
        HDC dc = graphics.GetHDC();
        paint(hwnd, dc);
        for (HWND child : {primaryButton, browseButton, minButton, closeButton, pathEdit})
        {
            if (!(GetWindowLongW(child, GWL_STYLE) & WS_VISIBLE))
                continue;
            RECT rect {};
            GetWindowRect(child, &rect);
            MapWindowPoints(nullptr, hwnd, reinterpret_cast<POINT*>(&rect), 2);
            int saved = SaveDC(dc);
            SetViewportOrgEx(dc, rect.left, rect.top, nullptr);
            IntersectClipRect(dc, 0, 0, rect.right - rect.left, rect.bottom - rect.top);
            if (child == pathEdit)
            {
                // Native EDIT skips painting while its parent is hidden. Render its value
                // with the same HFONT for the offscreen preview only.
                wchar_t value[512] {};
                GetWindowTextW(child, value, 512);
                SelectObject(dc, font);
                SetBkMode(dc, TRANSPARENT);
                SetTextColor(dc, RGB(225, 225, 230));
                RECT textRect {1, 0, rect.right - rect.left, rect.bottom - rect.top};
                DrawTextW(dc, value, -1, &textRect, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
            }
            else
            {
                DRAWITEMSTRUCT item {};
                item.hDC = dc;
                item.hwndItem = child;
                item.CtlID = GetDlgCtrlID(child);
                item.rcItem = {0, 0, rect.right - rect.left, rect.bottom - rect.top};
                if (!IsWindowEnabled(child))
                    item.itemState = ODS_DISABLED;
                paintButton(&item);
            }
            RestoreDC(dc, saved);
        }
        graphics.ReleaseHDC(dc);
    }
    const CLSID png {0x557cf406, 0x1a04, 0x11d3, {0x9a, 0x73, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e}};
    return image.Save(destination.c_str(), &png) == Ok;
}
LRESULT CALLBACK buttonProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR)
{
    if (message == WM_MOUSEMOVE)
    {
        TRACKMOUSEEVENT track {sizeof(track), TME_LEAVE, hwnd, 0};
        TrackMouseEvent(&track);
        InvalidateRect(hwnd, nullptr, FALSE);
    }
    else if (message == WM_MOUSELEAVE)
        InvalidateRect(hwnd, nullptr, FALSE);
    return DefSubclassProc(hwnd, message, wParam, lParam);
}
void browse()
{
    IFileDialog* dialog = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog))))
        return;
    DWORD options {};
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    dialog->SetTitle(L"选择安装位置（将在所选目录下创建 Folio 文件夹）");
    if (SUCCEEDED(dialog->Show(windowHandle)))
    {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dialog->GetResult(&item)))
        {
            PWSTR selected = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &selected)))
            {
                fs::path target(selected);
                if (_wcsicmp(target.filename().c_str(), L"Folio") != 0)
                    target /= L"Folio";
                SetWindowTextW(pathEdit, target.c_str());
                CoTaskMemFree(selected);
            }
            item->Release();
        }
    }
    dialog->Release();
}
void beginInstall()
{
    wchar_t value[512] {};
    GetWindowTextW(pathEdit, value, 512);
    installDir = value;
    while (installDir.size() > 3 && installDir.back() == L'\\')
        installDir.pop_back();
    auto error = validateDir(installDir);
    if (!error.empty())
    {
        MessageBoxW(windowHandle, error.c_str(), L"请检查安装位置", MB_OK | MB_ICONINFORMATION);
        return;
    }
    if (!readInstallDir(HKEY_LOCAL_MACHINE).empty())
    {
        MessageBoxW(windowHandle,
                    L"检测到为所有用户安装的旧版本。请先从 Windows 设置中卸载旧版本，再安装当前用户版本。",
                    L"检测到已有安装", MB_OK | MB_ICONINFORMATION);
        return;
    }
    statusText = L"正在准备安装文件…";
    setPage(Page::Installing);
    if (worker.joinable())
        worker.join();
    worker = std::thread(
        [target = installDir]
        {
            auto code = install(target, windowHandle);
            PostMessageW(windowHandle, WM_DONE, code, 0);
        });
}
void launchApp()
{
    const auto executable = fs::path(installDir) / L"folio.exe";
    auto command = L"\"" + executable.wstring() + L"\"";
    STARTUPINFOW startup {sizeof(startup)};
    PROCESS_INFORMATION process {};
    SetEnvironmentVariableW(L"ELECTRON_RUN_AS_NODE", nullptr);
    if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr, installDir.c_str(),
                        &startup, &process))
    {
        MessageBoxW(windowHandle, systemError(GetLastError()).c_str(), L"无法启动，请从桌面快捷方式重试",
                    MB_OK | MB_ICONERROR);
        return;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    DestroyWindow(windowHandle);
}
LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_CREATE:
    {
        windowHandle = hwnd;
        dpi = GetDpiForWindow(hwnd);
        auto button = [&](const wchar_t* label, int id)
        {
            auto control = CreateWindowW(L"BUTTON", label, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 0,
                                         0, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance, nullptr);
            SetWindowSubclass(control, buttonProc, 1, 0);
            return control;
        };
        primaryButton = button(L"一键安装", Primary);
        browseButton = button(L"浏览安装位置", Browse);
        minButton = button(L"最小化", Minimize);
        closeButton = button(L"关闭", Close);
        pathEdit =
            CreateWindowExW(0, L"EDIT", installDir.c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0,
                            0, 0, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(PathEdit)), instance, nullptr);
        SendMessageW(pathEdit, EM_SETLIMITTEXT, 180, 0);
        SetWindowTheme(pathEdit, L"", L"");
        layout();
        return 0;
    }
    case WM_PAINT:
        paint(hwnd);
        return 0;
    case WM_PRINTCLIENT:
        paint(hwnd, reinterpret_cast<HDC>(wParam));
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_DRAWITEM:
        paintButton(reinterpret_cast<DRAWITEMSTRUCT*>(lParam));
        return TRUE;
    case WM_CTLCOLOREDIT:
        SetTextColor(reinterpret_cast<HDC>(wParam), RGB(225, 225, 230));
        SetBkColor(reinterpret_cast<HDC>(wParam), RGB(48, 48, 48));
        return reinterpret_cast<LRESULT>(editBrush);
    case WM_NCHITTEST:
    {
        POINT point {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        ScreenToClient(hwnd, &point);
        if (point.y < px(52) && point.x < px(550))
            return HTCAPTION;
        return HTCLIENT;
    }
    case WM_DPICHANGED:
    {
        dpi = HIWORD(wParam);
        auto rect = reinterpret_cast<RECT*>(lParam);
        SetWindowPos(hwnd, nullptr, rect->left, rect->top, px(Width), px(Height), SWP_NOZORDER | SWP_NOACTIVATE);
        layout();
        InvalidateRect(hwnd, nullptr, TRUE);
        return 0;
    }
    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case Close:
            SendMessageW(hwnd, WM_CLOSE, 0, 0);
            break;
        case Minimize:
            ShowWindow(hwnd, SW_MINIMIZE);
            break;
        case Browse:
            browse();
            break;
        case Primary:
            if (preview)
            {
                DestroyWindow(hwnd);
                break;
            }
            if (page == Page::Welcome)
                beginInstall();
            else if (page == Page::Complete)
                launchApp();
            else if (page == Page::Failed)
                setPage(Page::Welcome);
            break;
        }
        return 0;
    case WM_TIMER:
    {
        animation++;
        RECT rect {px(132), px(355), px(508), px(361)};
        InvalidateRect(hwnd, &rect, FALSE);
        return 0;
    }
    case WM_PHASE:
        statusText = L"正在安装 Folio…";
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_DONE:
        exitCode = static_cast<DWORD>(wParam);
        errorText = L"安装失败（" + std::to_wstring(exitCode) + L"）。请检查目录权限和磁盘空间后重试。";
        setPage(exitCode == 0 ? Page::Complete : Page::Failed);
        return 0;
    case WM_QUERYENDSESSION:
        return page != Page::Installing;
    case WM_CLOSE:
        if (page != Page::Installing || preview)
        {
            if (page == Page::Welcome)
                exitCode = ERROR_CANCELLED;
            DestroyWindow(hwnd);
        }
        return 0;
    case WM_DESTROY:
        PostQuitMessage(static_cast<int>(exitCode));
        return 0;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}
} // namespace

int WINAPI wWinMain(HINSTANCE current, HINSTANCE, PWSTR, int show)
{
    instance = current;
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    bool silent = false;
    std::wstring capture;
    Page previewPage = Page::Welcome;
    try
    {
        installDir = defaultDir();
    }
    catch (...)
    {
        return ERROR_PATH_NOT_FOUND;
    }
    int count = 0;
    auto args = CommandLineToArgvW(GetCommandLineW(), &count);
    for (int index = 1; index < count; ++index)
    {
        std::wstring arg = args[index];
        if (arg == L"/S")
            silent = true;
        else if (arg.starts_with(L"/D="))
            installDir = arg.substr(3);
        else if (arg.starts_with(L"--capture="))
            capture = arg.substr(10);
        else if (arg.starts_with(L"--preview="))
        {
            preview = true;
            if (arg == L"--preview=progress")
                previewPage = Page::Installing;
            if (arg == L"--preview=complete")
                previewPage = Page::Complete;
            if (arg == L"--preview=error")
                previewPage = Page::Failed;
        }
    }
    LocalFree(args);
    // Keep one outer UI active while the payload has its own NSIS instance guard.
    HANDLE mutex = CreateMutexW(nullptr, FALSE, L"Local\\ZXB.Folio.Setup");
    if (!mutex)
        return static_cast<int>(GetLastError());
    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        CloseHandle(mutex);
        return ERROR_INSTALL_ALREADY_RUNNING;
    }
    if (silent)
    {
        DWORD code = ERROR_INVALID_PARAMETER;
        if (validateDir(installDir).empty() && readInstallDir(HKEY_LOCAL_MACHINE).empty())
            code = install(installDir, nullptr);
        CloseHandle(mutex);
        CoUninitialize();
        return static_cast<int>(code);
    }
    ULONG_PTR token {};
    GdiplusStartupInput input;
    GdiplusStartup(&token, &input, nullptr);
    editBrush = CreateSolidBrush(RGB(48, 48, 48));
    appIcon = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(1), IMAGE_ICON, 128, 128, LR_DEFAULTCOLOR));
    WNDCLASSW cls {};
    cls.lpfnWndProc = windowProc;
    cls.hInstance = instance;
    cls.lpszClassName = L"FolioSetupWindow";
    cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    cls.hIcon = appIcon;
    RegisterClassW(&cls);
    dpi = GetDpiForSystem();
    RECT work {};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    auto hwnd = CreateWindowExW(WS_EX_APPWINDOW | WS_EX_CONTROLPARENT, cls.lpszClassName, L"Folio 安装程序",
                                WS_POPUP | WS_CLIPCHILDREN | WS_MINIMIZEBOX,
                                work.left + (work.right - work.left - px(Width)) / 2,
                                work.top + (work.bottom - work.top - px(Height)) / 2, px(Width), px(Height), nullptr,
                                nullptr, instance, nullptr);
    if (!hwnd)
        return static_cast<int>(GetLastError());
    statusText = L"正在安装 Folio…";
    errorText = L"安装失败。请检查目录权限和磁盘空间后重试。";
    setPage(preview ? previewPage : Page::Welcome);
    if (preview && !capture.empty())
    {
        exitCode = capturePreview(hwnd, capture) ? 0 : ERROR_WRITE_FAULT;
        DestroyWindow(hwnd);
    }
    else
    {
        ShowWindow(hwnd, show);
        UpdateWindow(hwnd);
    }
    MSG message {};
    while (GetMessageW(&message, nullptr, 0, 0) > 0)
    {
        if (message.message == WM_KEYDOWN && message.wParam == VK_ESCAPE)
        {
            SendMessageW(hwnd, WM_CLOSE, 0, 0);
            continue;
        }
        if (message.message == WM_KEYDOWN && message.wParam == VK_RETURN)
        {
            auto focus = GetFocus();
            if (focus == pathEdit || focus == primaryButton)
                SendMessageW(hwnd, WM_COMMAND, Primary, 0);
            else if (focus)
                SendMessageW(focus, BM_CLICK, 0, 0);
            continue;
        }
        if (!IsDialogMessageW(hwnd, &message))
        {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    if (worker.joinable())
        worker.join();
    DeleteObject(font);
    DeleteObject(editBrush);
    DestroyIcon(appIcon);
    GdiplusShutdown(token);
    CloseHandle(mutex);
    CoUninitialize();
    return static_cast<int>(exitCode);
}

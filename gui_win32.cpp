#include "gui.h"

#include <cassert>
#include <stdexcept>
#include <system_error>
#include <thread>
#include <string>
#include <atomic>
#include <map>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <Windowsx.h>
#include <commdlg.h>

namespace {

std::atomic<HWND> hMainWindow;

[[noreturn]] void throw_system_error(const std::string& what, const unsigned error_code = GetLastError())
{
    assert(error_code != ERROR_SUCCESS);
    throw std::system_error(error_code, std::system_category(), what);
}

template <typename Derived>
class window_base {
public:
    HWND hwnd() const
    {
        return hwnd_;
    }

    static Derived* from_hwnd(HWND hwnd)
    {
        assert(GetClassWord(hwnd, GCW_ATOM) == atom_);
        return reinterpret_cast<Derived*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    }

    HWND create(const wchar_t* title, DWORD dwStyle, int x, int y, int cx, int cy, HWND hParent = nullptr, HMENU hMenu = nullptr)
    {
        assert(!hwnd_);
        register_class();

        hwnd_ = CreateWindow(Derived::ClassName, title, dwStyle, x, y, cx, cy, hParent, hMenu, GetModuleHandle(nullptr), this);
        if (!hwnd_) {
            delete static_cast<Derived*>(this);
            throw_system_error("Error creating window");
        }

        return hwnd_;
    }

private:
    static inline ATOM atom_;
    HWND hwnd_ = nullptr;

    static void register_class()
    {
        if (atom_)
            return;

        WNDCLASS wc;
        ZeroMemory(&wc, sizeof(wc));
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = s_wnd_proc;
        wc.hInstance = GetModuleHandle(nullptr);
        wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
        wc.lpszClassName = Derived::ClassName;

        atom_ = RegisterClass(&wc);
        if (!atom_)
            throw_system_error("Error registering class");
    }

    static LRESULT CALLBACK s_wnd_proc(HWND hwnd, UINT umsg, WPARAM wparam, LPARAM lparam)
    {
        Derived* self;
        if (umsg == WM_NCCREATE) {
            LPCREATESTRUCT lpcs = reinterpret_cast<LPCREATESTRUCT>(lparam);
            self = reinterpret_cast<Derived*>(lpcs->lpCreateParams);
            self->hwnd_ = hwnd;
            SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LPARAM>(self));
        } else {
            self = from_hwnd(hwnd);
        }
        LRESULT res;
        if (self) {
            res = self->wndproc(hwnd, umsg, wparam, lparam);
        } else {
            res = DefWindowProc(hwnd, umsg, wparam, lparam);
        }
        if (umsg == WM_NCDESTROY) {
            SetWindowLongPtr(hwnd, GWLP_USERDATA, 0);
            delete self;
        }
        return res;
    }
};

class win32_event {
public:
    enum type {
        manual_reset = 1 << 0,
        initially_signalled = 1 << 1,
    };

    explicit win32_event(type t = type(0))
    {
        handle_ = CreateEvent(nullptr, !!(t & manual_reset), !!(t & initially_signalled), nullptr);
        if (!handle_)
            throw_system_error("CreateEvent");
    }

    ~win32_event()
    {
        CloseHandle(handle_);
    }

    void set()
    {
        SetEvent(handle_);
    }

    void wait()
    {
        DWORD dwRes = WaitForSingleObject(handle_, INFINITE);
        if (dwRes != WAIT_OBJECT_0)
            throw std::runtime_error { "WaitForSingleObject failed: " + std::to_string(dwRes) };
    }

private:
    HANDLE handle_;
};

class bitmap_window : public window_base<bitmap_window> {
public:
    explicit bitmap_window(int w, int h)
        : w_ { w }
        , h_ { h }
        , curW_ { 1 }
        , curH_ { 1 }
    {
    }

    void update(const uint32_t* data, int width, int height)
    {
        assert(width > 0 && width <= w_);
        assert(height > 0 && height <= h_);
        assert(hwnd());
        // Update bitmap
        BITMAPINFO bmi;
        ZeroMemory(&bmi, sizeof(bmi));
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = width;
        bmi.bmiHeader.biHeight = -height;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biCompression = BI_RGB;
        bmi.bmiHeader.biBitCount = 32;

        SetDIBits(hdc_, hbm_, 0, height, data, &bmi, DIB_RGB_COLORS);
        curW_ = width;
        curH_ = height;
        // Repaint window
        //RedrawWindow(hwnd(), nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
        InvalidateRect(hwnd(), nullptr, FALSE);
    }

private:
    const int w_;
    const int h_;
    int curW_, curH_;
    HDC hdc_ {};
    HBITMAP hbm_ {};

    friend class window_base;
    static inline const wchar_t* const ClassName = L"BitmapWindow";

    LRESULT wndproc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
    {
        switch (uMsg) {
        case WM_CREATE: {
            HDC hdc = GetWindowDC(hwnd);
            hdc_ = CreateCompatibleDC(hdc);
            hbm_ = CreateCompatibleBitmap(hdc, w_, h_);
            SelectObject(hdc_, hbm_);
            ReleaseDC(hwnd, hdc);
            break;
        }
        case WM_DESTROY:
            DeleteObject(hbm_);
            DeleteDC(hdc_);
            break;
        case WM_ERASEBKGND:
            return TRUE;
        case WM_MOUSEMOVE:
        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP:
        case WM_RBUTTONDOWN:
        case WM_RBUTTONUP:
        case WM_MBUTTONDOWN:
        case WM_MBUTTONUP:
            SendMessage(GetParent(hwnd), uMsg, wParam, lParam);
            return 0;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            if (BeginPaint(hwnd, &ps) && !IsRectEmpty(&ps.rcPaint)) {
                RECT rcClient;
                GetClientRect(hwnd, &rcClient);
                #if 1
#pragma comment(lib, "Msimg32.lib")

                BLENDFUNCTION bf;
                bf.BlendOp = AC_SRC_OVER;
                bf.BlendFlags = 0;
                bf.SourceConstantAlpha = 255; // Fully opaque
                bf.AlphaFormat = 0; // No per-pixel alpha
                AlphaBlend(ps.hdc, 0, 0, rcClient.right - rcClient.left, rcClient.bottom - rcClient.top, hdc_, 0, 0, curW_, curH_, bf);
                #else
                StretchBlt(ps.hdc, 0, 0, rcClient.right - rcClient.left, rcClient.bottom - rcClient.top, hdc_, 0, 0, curW_, curH_, SRCCOPY);
                #endif
                EndPaint(hwnd, &ps);
            }
            return 0;
        }
        }
        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }
};

class main_window : public window_base<main_window> {
public:
    enum : DWORD {
        wm_update = WM_USER + 2,
        wm_get_events,
    };

    explicit main_window(int w, int h, int guiScale)
        : w_ { w }
        , h_ { h }
        , guiScale_ { guiScale }
    {
        // assert(!hDisplay);
        DWORD dwStyle = WS_OVERLAPPEDWINDOW;
        RECT rect;
        rect.left = 0;
        rect.top = 0;
        rect.right = w_;
        rect.bottom = h_;
        AdjustWindowRect(&rect, dwStyle, FALSE);

        const int window_x = 0;
        const int window_y = 0;
        const int window_w = rect.right - rect.left;
        const int window_h = rect.bottom - rect.top;

        HWND hwnd = create(L"", dwStyle, window_x, window_y, window_w, window_h);

        screen_wnd_ = new bitmap_window(w, h);
        screen_wnd_->create(L"", WS_CHILD | WS_VISIBLE, 0, 0, w_, h_, hwnd);
        setTitle();

        ShowWindow(hwnd, SW_NORMAL);
        UpdateWindow(hwnd);
        hMainWindow = hwnd;

        SetWindowPos(GetConsoleWindow(), nullptr, window_x + window_w + 32, window_y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    }

private:
    const int w_;
    const int h_;
    int guiScale_;
    int curW_ = 0;
    int curH_ = 0;
    std::vector<GUI::Event> events_;
    bitmap_window* screen_wnd_;
    static constexpr int maxDisks = 3;
    static constexpr const wchar_t* const diskDescriptors[maxDisks] = {
        TEXT("Drive &A:"),
        TEXT("Drive &B:"),
        TEXT("&HD")
    };
    static constexpr uint8_t driveId[maxDisks] = { 0x00, 0x01, 0x80 };
    std::string diskFileNames_[maxDisks];
    std::string pasteBuf_;
    bool pasteState_ = false;
    bool mouseCaptured_ = false;
    POINT lastMouse_;

    friend class window_base;
    static inline const wchar_t* const ClassName = L"MainWindow";

    enum {
        MENU_ID_INSERT_DISK = 1,
        MENU_ID_EJECT_DISK = MENU_ID_INSERT_DISK + maxDisks,
        MENU_ID_GUI_SCALE_1 = MENU_ID_EJECT_DISK + maxDisks,
        MENU_ID_GUI_SCALE_2,
        MENU_ID_GUI_SCALE_4,
        MENU_ID_PASTE,
    };

    void onCreate()
    {
        HMENU sysMenu = GetSystemMenu(hwnd(), FALSE);
        AppendMenu(sysMenu, MF_MENUBREAK, 0, nullptr);
        for (int i = 0; i < maxDisks; ++i) {
            HMENU subMenu = CreateMenu();
            AppendMenu(sysMenu, MF_POPUP, (UINT_PTR)subMenu, diskDescriptors[i]);
            AppendMenu(subMenu, MF_STRING, MENU_ID_INSERT_DISK + i, L"&Insert...");
            AppendMenu(subMenu, MF_STRING, MENU_ID_EJECT_DISK + i, L"&Eject");
        }
        AppendMenu(sysMenu, MF_MENUBREAK, 0, nullptr);
        AppendMenu(sysMenu, MF_STRING, MENU_ID_GUI_SCALE_1, L"GUI scale &1x1");
        AppendMenu(sysMenu, MF_STRING, MENU_ID_GUI_SCALE_2, L"GUI scale &2x2");
        AppendMenu(sysMenu, MF_STRING, MENU_ID_GUI_SCALE_4, L"GUI scale &4x4");
        AppendMenu(sysMenu, MF_MENUBREAK, 0, nullptr);
        AppendMenu(sysMenu, MF_STRING, MENU_ID_PASTE, L"&Paste");
    }

    void keyboard_event(bool down, [[maybe_unused]] int vk, uint32_t info)
    {
        // Windows uses PS/2 Scan code set 1 internally
        // See https://download.microsoft.com/download/1/6/1/161ba512-40e2-4cc9-843a-923143f3456c/translate.pdf
        const auto scanCode = static_cast<uint8_t>(info >> 16);

        GUI::Event evt {};
        evt.type = GUI::EventType::keyboard;
        evt.key.down = down;
        evt.key.extendedKey = (info & (1 << 24)) != 0;
        evt.key.scanCode = scanCode;
        events_.push_back(evt);
    }

    bool browseForFile(std::string& filename)
    {
        assert(filename.length() < MAX_PATH);
        OPENFILENAMEA ofn;

        char path[MAX_PATH];
        ZeroMemory(&ofn, sizeof(ofn));
        strcpy(path, filename.c_str());
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = hwnd();
        // ofn.lpstrFilter = zz_filter;
        // ofn.lpstrDefExt = defext;
        ofn.lpstrFile = path;
        ofn.nMaxFile = sizeof(path);
        ofn.Flags = OFN_PATHMUSTEXIST;
        if (!GetOpenFileNameA(&ofn))
            return false;
        filename = path;
        return true;
    }

    void scaleWindow()
    {
        int width = curW_;
        int height = curH_;

        if (width <= height)
            width *= 2;
        else if (width / height >= 2)
            height *= 2;

        if (width <= 400) {
            width *= 2;
            height *= 2;
        }

        width *= guiScale_;
        height *= guiScale_;
        RECT rc = { 0, 0, width, height };
        // printf("**** Mode: %dx%d -> %dx%d\n", curW_, curH_, rc.right, rc.bottom);

        AdjustWindowRect(&rc, GetWindowLong(hwnd(), GWL_STYLE), FALSE);

        SetWindowPos(hwnd(), NULL, 0, 0,
            rc.right - rc.left,
            rc.bottom - rc.top,
            SWP_NOMOVE | SWP_NOZORDER);
    }

    void onSysCommand(int command)
    {
        if (command >= MENU_ID_INSERT_DISK && command < MENU_ID_INSERT_DISK + maxDisks) {
            const auto driveIndex = command - MENU_ID_INSERT_DISK;
            auto& filename = diskFileNames_[driveIndex];
            if (!browseForFile(filename))
                return;
            GUI::Event evt {};
            evt.type = GUI::EventType::diskInsert;
            evt.diskInsert.drive = driveId[driveIndex];
            strcpy(evt.diskInsert.filename, filename.c_str()); // FIXME
            events_.push_back(evt);
        } else if (command >= MENU_ID_EJECT_DISK && command < MENU_ID_EJECT_DISK + maxDisks) {
            const auto driveIndex = command - MENU_ID_EJECT_DISK;
            GUI::Event evt {};
            evt.type = GUI::EventType::diskEject;
            evt.diskEject.drive = driveId[driveIndex];
            events_.push_back(evt);
        } else if (command >= MENU_ID_GUI_SCALE_1 && command <= MENU_ID_GUI_SCALE_4) {
            guiScale_ = 1 << (command - MENU_ID_GUI_SCALE_1);
            scaleWindow();
        } else if (command == MENU_ID_PASTE) {
            if (!OpenClipboard(nullptr))
                return;

            HANDLE hData = GetClipboardData(CF_TEXT);
            if (!hData) {
                CloseClipboard();
                return;
            }

            // 3. Lock the handle to get a pointer to the actual text
            auto pszText = static_cast<const char*>(GlobalLock(hData));
            if (pszText == nullptr) {
                CloseClipboard();
                return;
            }

            pasteBuf_ += std::string(pszText);

            GlobalUnlock(hData);
            CloseClipboard();
        }
    }

    void setTitle()
    {
        std::wstring title = L"Emulator window " + std::to_wstring(curW_) + L"x" + std::to_wstring(curH_);
        if (mouseCaptured_)
            title += L" - mouse captured (use middle button to release)";
        SetWindowTextW(hwnd(), title.c_str());
    }

    void onUpdate(const uint32_t* pixels, int width, int height)
    {
        screen_wnd_->update(pixels, width, height);
        if (width != curW_ || height != curH_) {
            curW_ = width;
            curH_ = height;
            scaleWindow();
            if (mouseCaptured_) {
                releaseCapture();
                captureMouse();
            }
            setTitle();
        }
    }

    void onMouseMove(int x, int y)
    {
        if (!mouseCaptured_)
            return;
        RECT rcClient;
        GetClientRect(hwnd(), &rcClient);
        if (rcClient.left == rcClient.right || rcClient.top == rcClient.bottom)
            return;
        GUI::Event evt {};

        POINT curPos = { x, y };
        ClientToScreen(hwnd(), &curPos);

        evt.type = GUI::EventType::mouseMove;
        evt.mouseMove.dx = (curPos.x - lastMouse_.x) * curW_ / (rcClient.right - rcClient.left);
        evt.mouseMove.dy = (curPos.y - lastMouse_.y) * curH_ / (rcClient.bottom - rcClient.top);
        if (evt.mouseMove.dx || evt.mouseMove.dy)
            events_.push_back(evt);
        SetCursorPos(lastMouse_.x, lastMouse_.y);
    }

    void onMouseButton(int index, bool down)
    {
        if (!mouseCaptured_ && index != 2) {
            captureMouse();
            return;
        }
        if (index == 2) {
            releaseCapture();
            return;
        }

        GUI::Event evt {};
        evt.type = GUI::EventType::mouseButton;
        evt.mouseButton.index = index;
        evt.mouseButton.down = down;
        events_.push_back(evt);
    }

    void captureMouse()
    {
        assert(!mouseCaptured_);
        SetCapture(hwnd());
        RECT rcClient;
        GetClientRect(hwnd(), &rcClient);
        POINT ptUL, ptLR;
        ptUL.x = rcClient.left;
        ptUL.y = rcClient.top;
        ptLR.x = rcClient.right + 1;
        ptLR.y = rcClient.bottom + 1;
        ClientToScreen(hwnd(), &ptUL);
        ClientToScreen(hwnd(), &ptLR);
        SetRect(&rcClient, ptUL.x, ptUL.y, ptLR.x, ptLR.y);
        ClipCursor(&rcClient);
        ShowCursor(FALSE);
        mouseCaptured_ = true;
        // Center mouse in window
        lastMouse_ = { ptUL.x + (ptLR.x - ptUL.x) / 2, ptUL.y + (ptLR.y - ptUL.y) / 2 };
        SetCursorPos(lastMouse_.x, lastMouse_.y);
        setTitle();
    }
    
    void releaseCapture()
    {
        if (!mouseCaptured_)
            return;
        ShowCursor(TRUE);
        ClipCursor(NULL);
        ReleaseCapture();
        mouseCaptured_ = false;
        setTitle();
    }

    LRESULT wndproc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
    {
        switch (uMsg) {
        case WM_CREATE:
            onCreate();
            break;
        case WM_SYSCOMMAND:
            onSysCommand(LOWORD(wParam));
            break;
        case WM_SYSKEYDOWN:
        case WM_KEYDOWN:
            keyboard_event(true, static_cast<int>(wParam), static_cast<uint32_t>(lParam));
            return 0;
        case WM_SYSKEYUP:
        case WM_KEYUP:
            keyboard_event(false, static_cast<int>(wParam), static_cast<uint32_t>(lParam));
            return 0;
        case WM_CLOSE:
            PostQuitMessage(0);
            break;
        case WM_DESTROY:
            hMainWindow = nullptr;
            break;
        case WM_ERASEBKGND:
            return TRUE;
        case WM_MOUSEMOVE:
            onMouseMove(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            break;
        case WM_LBUTTONDOWN:
            onMouseButton(0, true);
            break;
        case WM_LBUTTONUP:
            onMouseButton(0, false);
            break;
        case WM_RBUTTONDOWN:
            onMouseButton(1, true);
            break;
        case WM_RBUTTONUP:
            onMouseButton(1, false);
            break;
        case WM_MBUTTONDOWN:
            onMouseButton(2, true);
            break;
        case WM_MBUTTONUP:
            onMouseButton(2, false);
            break;
        case WM_SIZE:
            SetWindowPos(screen_wnd_->hwnd(), nullptr, 0, 0, LOWORD(lParam), HIWORD(lParam), SWP_NOZORDER);
            break;
        case WM_ACTIVATEAPP:
            if (!wParam)
                releaseCapture();
            break;
        case wm_update:
            onUpdate(reinterpret_cast<const uint32_t*>(lParam), LOWORD(wParam), HIWORD(wParam));
            break;
        case wm_get_events:
            if (!pasteBuf_.empty()) {
                const auto ch = pasteBuf_[0];
                GUI::Event evt {};
                evt.type = GUI::EventType::keyboard;
                evt.key.down = !pasteState_;
                evt.key.extendedKey = false;
                evt.key.scanCode = (uint8_t)MapVirtualKeyA(LOBYTE(VkKeyScanA(ch)), MAPVK_VK_TO_VSC);
                if (pasteState_)
                    pasteBuf_.erase(pasteBuf_.begin());
                pasteState_ = !pasteState_;
                events_.push_back(evt);
            }

            *reinterpret_cast<std::vector<GUI::Event>*>(lParam) = std::move(events_);
            events_.clear();
            break;
        }
        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }
};

} // unnamed namespace

class GUI::impl {
public:
    explicit impl(int w, int h, int guiScale)
    {
        SetProcessDPIAware(); // Avoid GUI scaling (must be called before any windows are created)
        win32_event create_event {};
        thread_ = std::thread { [=, &create_event]() { thread_func(&create_event, w, h, guiScale); } };
        create_event.wait();
    }

    ~impl()
    {
        PostThreadMessage(GetThreadId(thread_.native_handle()), WM_QUIT, 0, 0); // N.B. fails if no message queue created (yet)
        thread_.join();
    }

private:
    std::thread thread_;

    void thread_func(win32_event* create_event, int w, int h, int guiScale)
    {
        // TODO: error handling..
        [[maybe_unused]] auto mw = new main_window { w, h, guiScale };
        create_event->set();
        create_event = nullptr;
        for (MSG msg; GetMessage(&msg, nullptr, 0, 0);) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }
};

GUI::GUI(int w, int h, int guiScale)
    : impl_ { std::make_unique<impl>(w, h, guiScale) }
{
}

GUI::~GUI() = default;

std::vector<GUI::Event> GUI::update()
{
    if (!hMainWindow) {
        Event evt {};
        evt.type = EventType::quit;
        return { evt };
    }
    std::vector<Event> events;
    SendMessage(hMainWindow, main_window::wm_get_events, 0, reinterpret_cast<LPARAM>(&events));
    return events;
}

void SetGuiActive(bool active)
{
    if (active && hMainWindow) {
        // Wait for return to be released
        while (GetAsyncKeyState(VK_RETURN) < 0)
            Sleep(10);
        SetForegroundWindow(hMainWindow);
    } else {
        SetForegroundWindow(GetConsoleWindow());
    }
}

void DrawScreen(const uint32_t* pixels, int w, int h)
{
    if (hMainWindow)
        SendMessage(hMainWindow, main_window::wm_update, MAKEWPARAM(w, h), reinterpret_cast<LPARAM>(pixels));
}
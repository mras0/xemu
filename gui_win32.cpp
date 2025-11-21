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

namespace {

std::atomic<HWND> hMainWindow;

[[noreturn]] void throw_system_error(const std::string& what, const unsigned error_code = GetLastError())
{
    assert(error_code != ERROR_SUCCESS);
    throw std::system_error(error_code, std::system_category(), what);
}

std::map<int, uint32_t> vkMap;

uint32_t VirtualKeyToScan(int vk)
{
    if (auto it = vkMap.find(vk); it != vkMap.end())
        return it->second;
    std::printf("TODO: Translate VK 0x%X\n", vk);
    return 0;
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
    {
    }

    void update(const uint32_t* data)
    {
        assert(hwnd());
        // Update bitmap
        BITMAPINFO bmi;
        ZeroMemory(&bmi, sizeof(bmi));
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = w_;
        bmi.bmiHeader.biHeight = -h_;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biCompression = BI_RGB;
        bmi.bmiHeader.biBitCount = 32;

        SetDIBits(hdc_, hbm_, 0, h_, data, &bmi, DIB_RGB_COLORS);
        // Repaint window
        RedrawWindow(hwnd(), nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
    }

private:
    int w_;
    int h_;
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
        case WM_PAINT: {
            PAINTSTRUCT ps;
            if (BeginPaint(hwnd, &ps) && !IsRectEmpty(&ps.rcPaint)) {
                RECT rcClient;
                GetClientRect(hwnd, &rcClient);
                StretchBlt(ps.hdc, 0, 0, rcClient.right - rcClient.left, rcClient.bottom - rcClient.top, hdc_, 0, 0, w_, h_, SRCCOPY);
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

    explicit main_window(int w, int h, int xscale, int yscale)
        : w_ { w }
        , h_ { h }
    {
        //assert(!hDisplay);
        DWORD dwStyle = WS_OVERLAPPEDWINDOW;
        RECT rect;
        rect.left = 0;
        rect.top = 0;
        rect.right = w_ * xscale;
        rect.bottom = h_ * yscale;
        AdjustWindowRect(&rect, dwStyle, FALSE);

        const int window_x = 0;
        const int window_y = 0;
        const int window_w = rect.right - rect.left;
        const int window_h = rect.bottom - rect.top;

        HWND hwnd = create(L"Display", dwStyle, window_x, window_y, window_w, window_h);

        screen_wnd_ = new bitmap_window(w, h);
        screen_wnd_->create(L"", WS_CHILD | WS_VISIBLE, 0, 0, w_ * xscale, h_ * yscale, hwnd);

        ShowWindow(hwnd, SW_NORMAL);
        UpdateWindow(hwnd);
        hMainWindow = hwnd;

        SetWindowPos(GetConsoleWindow(), nullptr, window_x + window_w + 32, window_y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    }

private:
    int w_;
    int h_;
    std::vector<GUI::Event> events_;
    bitmap_window* screen_wnd_;

    friend class window_base;
    static inline const wchar_t* const ClassName = L"MainWindow";
    
    void keyboard_event(bool down, int vk)
    {
        GUI::Event evt {};
        evt.type = GUI::EventType::keyboard;
        evt.key.down = down;
        evt.key.scanCode = VirtualKeyToScan(vk);
        events_.push_back(evt);
    }

    LRESULT wndproc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
    {
        switch (uMsg) {
        case WM_KEYDOWN:
            keyboard_event(true, static_cast<int>(wParam));
            break;
        case WM_KEYUP:
            keyboard_event(false, static_cast<int>(wParam));
            break;
        case WM_CLOSE:
            PostQuitMessage(0);
            break;
        case WM_DESTROY:
            hMainWindow = nullptr;
            break;
        case WM_ERASEBKGND:
            return TRUE;
        case wm_update:
            screen_wnd_->update(reinterpret_cast<const uint32_t*>(lParam));
            break;
        case wm_get_events:
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
    explicit impl(int w, int h, int xscale, int yscale) {
        win32_event create_event {};
        thread_ = std::thread { [=, &create_event]() { thread_func(&create_event, w, h, xscale, yscale); } };
        create_event.wait();
    }

    ~impl()
    {
        PostThreadMessage(GetThreadId(thread_.native_handle()), WM_QUIT, 0, 0); // N.B. fails if no message queue created (yet)
        thread_.join();
    }

private:
    std::thread thread_;

    void thread_func(win32_event* create_event, int w, int h, int xscale, int yscale)
    {
        // TODO: error handling..
        [[maybe_unused]] auto mw = new main_window { w, h, xscale, yscale };
        create_event->set();
        create_event = nullptr;
        for (MSG msg; GetMessage(&msg, nullptr, 0, 0);) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }
};

GUI::GUI(int w, int h, int xscale, int yscale)
    : impl_ { std::make_unique<impl>(w, h, xscale, yscale) }
{
    auto addRange = [](int start, const char* chs) {
        while (*chs)
            vkMap[*chs++] = start++;
    };

    addRange(0x02, "1234567890");
    addRange(0x10, "QWERTYUIOP");
    addRange(0x1E, "ASDFGHJKL");
    addRange(0x2C, "ZXCVBNM");
    vkMap[VK_ESCAPE] = 0x1;
    vkMap[VK_TAB] = 0x0F;
    vkMap[VK_BACK] = 0x0E;
    vkMap[VK_SHIFT] = 0x2A;
    vkMap[VK_RETURN] = 0x1C;
    vkMap[VK_SPACE] = 0x39;
    vkMap[VK_CONTROL] = 0x1D;
    vkMap[VK_MENU] = 0x38;
    

    vkMap[VK_OEM_1] = 0x27; // ';:' for US
    vkMap[VK_OEM_PLUS] = 0x0D; // '+' any country
    vkMap[VK_OEM_COMMA] = 0x33; // ',' any country
    vkMap[VK_OEM_MINUS] = 0x0C; // '-' any country
    vkMap[VK_OEM_PERIOD] = 0x34; // '.' any country
    vkMap[VK_OEM_2] = 0x35; // '/?' for US
    //vkMap[VK_OEM_3] = 0x01; // '`~' for US
    vkMap[VK_OEM_4] = 0x1A; //  '[{' for US
    vkMap[VK_OEM_5] = 0x2B; //  '\|' for US
    vkMap[VK_OEM_6] = 0x1B; //  ']}' for US
    vkMap[VK_OEM_7] = 0x28; //  ''"' for US

    
    for (int i = VK_F1; i <= VK_F10; ++i)
        vkMap[i] = 0x3B + (i - VK_F1);

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
    if (active && hMainWindow)
        SetForegroundWindow(hMainWindow);
    else
        SetForegroundWindow(GetConsoleWindow());
}

void DrawScreen(const uint32_t* pixels)
{
    if (hMainWindow)
        SendMessage(hMainWindow, main_window::wm_update, 0, reinterpret_cast<LPARAM>(pixels));
}

#include <X11/Xlib.h>

#pragma once

#include <memory>
#include <unordered_map>
#include <vector>

static const unsigned int BORDER_WIDTH_INACTIVE = 1;
static const unsigned int BORDER_WIDTH_ACTIVE = 3;
static const unsigned long BORDER_COLOR_INACTIVE = 0x2d2b40;
static const unsigned long BORDER_COLOR_ACTIVE = 0x6c5ce7;
static const unsigned long BG_COLOR = 0xdfe6e9;

class WindowManager {
   public:
    static std::unique_ptr<WindowManager> Create();
    ~WindowManager();
    void Run();

   private:
    WindowManager(Display* display);

    void Frame(Window w);
    void Unframe(Window w);
    void Reframe(Window w, bool active);

    static int OnXError(Display* display, XErrorEvent* e);
    static int OnWMDetected(Display* display, XErrorEvent* e);

    void OnCreateNotify(const XCreateWindowEvent& e);
    void OnDestroyNotify(const XDestroyWindowEvent& e);
    void OnReparentNotify(const XReparentEvent& e);
    void OnConfigureRequest(const XConfigureRequestEvent& e);
    void OnConfigureNotify(const XConfigureEvent& e);
    void OnMapRequest(const XMapRequestEvent& e);
    void OnMapNotify(const XMapEvent& e);
    void OnUnmapNotify(const XUnmapEvent& e);
    void OnButtonPress(const XButtonEvent& e);
    void OnButtonRelease(const XButtonEvent& e);
    void OnMotionNotify(const XMotionEvent& e);

   private:
    Display* m_display;
    const Window m_root;
    static bool m_wm_detected;

    std::unordered_map<Window, Window> m_clients;  // Maps top-level windows to their frame windows
    std::vector<Window> m_inactive_windows;        // List of inactive windows

    Window m_active_window;

    std::pair<int, int> m_drag_start_pos;
    std::pair<int, int> m_drag_start_frame_pos;
    std::pair<int, int> m_drag_start_frame_size;

    const Atom WM_PROTOCOLS;
    const Atom WM_DELETE_WINDOW;
};
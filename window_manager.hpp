
#include <X11/Xlib.h>

#pragma once

#include <memory>
#include <vector>

class WindowManager {
   public:
    static std::unique_ptr<WindowManager> Create();
    ~WindowManager();
    void Run();

   private:
    WindowManager(Display* display);

    void SetWindowBorder(const Window& w, unsigned int width, const char* color_str);
    void FocusWindow(const Window& w);

    static int OnWMDetected(Display* display, XErrorEvent* e);
    static int OnXError(Display* display, XErrorEvent* e);

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
    void OnKeyPress(const XKeyEvent& e);
    void OnKeyRelease(const XKeyEvent& e);

   private:
    Display* m_display;
    const Window m_root;
    static bool m_wm_detected;
    std::vector<Window> m_windows;

    std::pair<int, int> m_drag_start_pos;
    std::pair<int, int> m_drag_start_frame_pos;
    std::pair<int, int> m_drag_start_frame_size;

    const Atom WM_PROTOCOLS;
    const Atom WM_DELETE_WINDOW;
};

#include <X11/Xlib.h>

#pragma once

#include <memory>
#include <unordered_map>
#include <vector>

class WindowManager {
   public:
    static std::unique_ptr<WindowManager> Create();
    ~WindowManager();
    void Run();

   private:
    WindowManager(Display* display);

    void Frame(Window w);
    void Unframe(Window w);
    void SetWindowBorder(const Window& w, unsigned int width, const char* color_str);

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
    void OnKeyPress(const XKeyEvent& e);
    void OnKeyRelease(const XKeyEvent& e);

   private:
    Display* m_display;
    const Window m_root;
    static bool m_wm_detected;

    std::unordered_map<Window, Window> m_clients;  // Maps top-level windows to their frame windows

    std::pair<int, int> m_drag_start_pos;
    std::pair<int, int> m_drag_start_frame_pos;
    std::pair<int, int> m_drag_start_frame_size;

    const Atom WM_PROTOCOLS;
    const Atom WM_DELETE_WINDOW;
};

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

    void SetWindowBorder(const Window& w, unsigned int width, const char* color_str);
    void FocusWindow(const Window& w);
    void WriteToStatusBar(const std::string message);
    void SwitchWorkspace(const int workspace);
    void CreateWorkspace();

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
    Display* display_;
    const Window root_;
    static bool wm_detected_;

    int active_workspace_ = 0;
    int num_workspaces_ = 1;
    Window active_window_ = 0;
    std::vector<std::vector<Window>> workspaces_;  // Vector of windows in every workspace
    Window status_bar_window_;

    std::pair<int, int> drag_start_pos_;
    std::pair<int, int> drag_start_frame_pos_;
    std::pair<int, int> drag_start_frame_size_;

    const Atom WM_PROTOCOLS;
    const Atom WM_DELETE_WINDOW;
};
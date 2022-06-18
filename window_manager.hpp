
#include <X11/Xlib.h>

#include <memory>
#include <unordered_map>

class WindowManager {
   public:
    static std::unique_ptr<WindowManager> Create();
    ~WindowManager();
    void Run();

   private:
    WindowManager(Display* display);

    void Frame(Window w);
    void Unframe(Window w);

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
    std::unordered_map<Window, Window> m_clients;  // Map top-level windows to their frame windows
};
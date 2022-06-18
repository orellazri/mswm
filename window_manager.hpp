
extern "C" {
#include <X11/Xlib.h>
}

#include <memory>

class WindowManager {
   public:
    static std::unique_ptr<WindowManager> Create();
    ~WindowManager();
    void Run();

   private:
    WindowManager(Display* display);

    static int OnXError(Display* display, XErrorEvent* e);
    static int OnWMDetected(Display* display, XErrorEvent* e);

   private:
    Display* m_display;
    const Window m_root;

    static bool m_wm_detected;
};
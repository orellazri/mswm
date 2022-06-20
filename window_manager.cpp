#include "window_manager.hpp"

#include <X11/Xutil.h>
#include <X11/cursorfont.h>
#include <glog/logging.h>

#include <algorithm>

#include "config.hpp"
#include "utils.hpp"

using std::find;
using std::max;
using std::pair;
using std::unique_ptr;

bool WindowManager::m_wm_detected;

unique_ptr<WindowManager> WindowManager::Create() {
    Display* display = XOpenDisplay(nullptr);
    if (display == nullptr) {
        LOG(ERROR) << "Failed to open X display " << XDisplayName(nullptr);
        return nullptr;
    }
    return unique_ptr<WindowManager>(new WindowManager(display));
}

WindowManager::WindowManager(Display* display) : m_display(CHECK_NOTNULL(display)),
                                                 m_root(DefaultRootWindow(m_display)),
                                                 WM_PROTOCOLS(XInternAtom(m_display, "WM_PROTOCOLS", false)),
                                                 WM_DELETE_WINDOW(XInternAtom(m_display, "WM_DELETE_WINDOW", false)) {
}

WindowManager::~WindowManager() {
    XCloseDisplay(m_display);
}

void WindowManager::Run() {
    // Initialization
    m_wm_detected = false;
    XSetErrorHandler(&WindowManager::OnWMDetected);
    XSelectInput(m_display, m_root, SubstructureRedirectMask | SubstructureNotifyMask | ButtonPressMask);
    XGrabButton(m_display,
                AnyButton,
                AnyModifier,
                m_root,
                true,
                ButtonPressMask,
                GrabModeAsync,
                GrabModeAsync,
                None,
                None);
    XSync(m_display, false);
    if (m_wm_detected) {
        LOG(ERROR) << "Detected another window manager on display " << XDisplayString(m_display);
        return;
    }
    XSetErrorHandler(&WindowManager::OnXError);

    // Show mouse cursor
    XDefineCursor(m_display, m_root, XCreateFontCursor(m_display, XC_top_left_arrow));

    // Main event loop
    while (true) {
        XEvent e;
        XNextEvent(m_display, &e);
        LOG(INFO) << "Received event: " << XEventCodeToString(e.type);

        switch (e.type) {
            case CreateNotify:
                OnCreateNotify(e.xcreatewindow);
                break;
            case DestroyNotify:
                OnDestroyNotify(e.xdestroywindow);
                break;
            case ReparentNotify:
                OnReparentNotify(e.xreparent);
                break;
            case ConfigureRequest:
                OnConfigureRequest(e.xconfigurerequest);
                break;
            case ConfigureNotify:
                OnConfigureNotify(e.xconfigure);
                break;
            case MapRequest:
                OnMapRequest(e.xmaprequest);
                break;
            case MapNotify:
                OnMapNotify(e.xmap);
                break;
            case UnmapNotify:
                OnUnmapNotify(e.xunmap);
                break;
            case ButtonPress:
                OnButtonPress(e.xbutton);
                break;
            case ButtonRelease:
                OnButtonRelease(e.xbutton);
                break;
            case MotionNotify:
                // Skip any pending motion events
                while (XCheckTypedWindowEvent(m_display, e.xmotion.window, MotionNotify, &e)) {
                }
                OnMotionNotify(e.xmotion);
                break;
            case KeyPress:
                OnKeyPress(e.xkey);
                break;
            case KeyRelease:
                OnKeyRelease(e.xkey);
                break;
            default:
                LOG(WARNING) << "Ignored event: " << e.type;
        }
    }
}

int WindowManager::OnWMDetected(Display* display, XErrorEvent* e) {
    CHECK_EQ(static_cast<int>(e->error_code), BadAccess);
    m_wm_detected = true;
    return 0;
}

int WindowManager::OnXError(Display* display, XErrorEvent* e) {
    char error_text[1024] = {0};
    XGetErrorText(display, e->error_code, error_text, sizeof(error_text));
    LOG(ERROR) << "Received X Error:"
               << "\n    " << error_text << " (" << int(e->error_code) << ")"
               << "\n    Request: " << XRequestCodeToString(e->request_code) << " (" << int(e->request_code) << ")"
               << "\n    Resource ID: " << e->resourceid;
    return 0;
}

void WindowManager::OnCreateNotify(const XCreateWindowEvent& e) {}

void WindowManager::OnDestroyNotify(const XDestroyWindowEvent& e) {}

void WindowManager::OnReparentNotify(const XReparentEvent& e) {}

void WindowManager::OnConfigureRequest(const XConfigureRequestEvent& e) {
    XWindowChanges wc;
    wc.x = e.x;
    wc.y = e.y;
    wc.width = e.width;
    wc.height = e.height;
    wc.border_width = e.border_width;
    wc.sibling = e.above;
    wc.stack_mode = e.detail;
    XConfigureWindow(m_display, e.window, e.value_mask, &wc);
}

void WindowManager::OnConfigureNotify(const XConfigureEvent& e) {}

void WindowManager::OnMapRequest(const XMapRequestEvent& e) {
    XMapWindow(m_display, e.window);
    XRaiseWindow(m_display, e.window);
    SetWindowBorder(e.window, BORDER_WIDTH_ACTIVE, BORDER_COLOR_ACTIVE);

    m_windows.push_back(e.window);

    LOG(INFO) << "Mapped window " << e.window;
}

void WindowManager::OnMapNotify(const XMapEvent& e) {}

void WindowManager::OnUnmapNotify(const XUnmapEvent& e) {}

void WindowManager::OnButtonPress(const XButtonEvent& e) {
    if (e.subwindow == 0)
        return;

    // Save initial cursor position
    m_drag_start_pos = {e.x_root, e.y_root};

    // Save initial window info
    Window returned_root;
    int x, y;
    unsigned width, height, border_width, depth;
    CHECK(XGetGeometry(
        m_display,
        e.subwindow,
        &returned_root,
        &x,
        &y,
        &width,
        &height,
        &border_width,
        &depth));

    m_drag_start_frame_pos = {x, y};
    m_drag_start_frame_size = {width, height};

    // Raise window and change border to active
    XRaiseWindow(m_display, e.subwindow);
    SetWindowBorder(e.subwindow, BORDER_WIDTH_ACTIVE, BORDER_COLOR_ACTIVE);

    // Change border of all other windows to inactive
    for (auto& window : m_windows) {
        if (window == e.subwindow)
            continue;
        SetWindowBorder(window, BORDER_WIDTH_INACTIVE, BORDER_COLOR_INACTIVE);
    }

    // Alt
    if (e.state & Mod1Mask) {
        // Middle button to close
        if (e.button == Button2) {
            // Try to gracefully kill client if the client supports the WM_DELETE_WINDOW behavior.
            // Otherwise, kill it.
            Atom* supported_protocols;
            int num_supported_protocols;
            if (XGetWMProtocols(m_display, e.subwindow, &supported_protocols, &num_supported_protocols) &&
                find(supported_protocols, supported_protocols + num_supported_protocols, WM_DELETE_WINDOW) != supported_protocols + num_supported_protocols) {
                LOG(INFO) << "Gracefully deleting window " << e.subwindow;
                XEvent msg = {0};
                msg.xclient.type = ClientMessage;
                msg.xclient.message_type = WM_PROTOCOLS;
                msg.xclient.window = e.subwindow;
                msg.xclient.format = 32;
                msg.xclient.data.l[0] = WM_DELETE_WINDOW;
                CHECK(XSendEvent(m_display, e.subwindow, false, 0, &msg));
            } else {
                LOG(INFO) << "Killing window " << e.subwindow;
                XKillClient(m_display, e.subwindow);
            }
        }
    }
}

void WindowManager::OnButtonRelease(const XButtonEvent& e) {}

void WindowManager::OnMotionNotify(const XMotionEvent& e) {}

void WindowManager::OnKeyPress(const XKeyEvent& e) {}

void WindowManager::OnKeyRelease(const XKeyEvent& e) {}

void WindowManager::SetWindowBorder(const Window& w, unsigned int width, const char* color_str) {
    XSetWindowBorderWidth(m_display, w, width);

    Colormap colormap = DefaultColormap(m_display, DefaultScreen(m_display));
    XColor color;
    CHECK(XAllocNamedColor(m_display, colormap, color_str, &color, &color));
    XSetWindowBorder(m_display, w, color.pixel);
}
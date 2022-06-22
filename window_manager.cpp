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

    // Alt + mouse handler
    XGrabButton(m_display,
                AnyButton,
                Mod1Mask,
                m_root,
                True,
                ButtonPressMask | ButtonReleaseMask | PointerMotionMask | OwnerGrabButtonMask,
                GrabModeAsync,
                GrabModeAsync,
                None,
                None);

    // Alt + Tab
    XGrabKey(m_display,
             XKeysymToKeycode(m_display, XK_Tab),
             Mod1Mask,
             m_root,
             False,
             GrabModeAsync,
             GrabModeAsync);

    XSelectInput(m_display, m_root, SubstructureNotifyMask | SubstructureRedirectMask);

    XSync(m_display, false);
    if (m_wm_detected) {
        LOG(ERROR) << "Detected another window manager on display " << XDisplayString(m_display);
        return;
    }

    XSetErrorHandler(&WindowManager::OnXError);

    // Show mouse cursor
    XDefineCursor(m_display, m_root, XCreateFontCursor(m_display, XC_top_left_arrow));

    // Main event loop
    XEvent e;
    while (true) {
        XNextEvent(m_display, &e);
        // LOG(INFO) << "Received event: " << XEventCodeToString(e.type);

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
                while (XCheckTypedWindowEvent(m_display, e.xmotion.subwindow, MotionNotify, &e)) {
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
                LOG(WARNING) << "Ignored event: " << XEventCodeToString(e.type);
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

void WindowManager::SetWindowBorder(const Window& w, unsigned int width, const char* color_str) {
    XSetWindowBorderWidth(m_display, w, width);

    Colormap colormap = DefaultColormap(m_display, DefaultScreen(m_display));
    XColor color;
    CHECK(XAllocNamedColor(m_display, colormap, color_str, &color, &color));
    XSetWindowBorder(m_display, w, color.pixel);
}

void WindowManager::FocusWindow(const Window& w) {
    // Raise and change border on current window
    SetWindowBorder(w, BORDER_WIDTH_ACTIVE, BORDER_COLOR_ACTIVE);
    XRaiseWindow(m_display, w);

    m_active_window = w;

    // Change border of all other windows to inactive
    for (auto& window : m_windows) {
        if (window == w)
            continue;
        SetWindowBorder(window, BORDER_WIDTH_INACTIVE, BORDER_COLOR_INACTIVE);
    }
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
    m_windows.push_back(e.window);

    XMapWindow(m_display, e.window);
    XReparentWindow(m_display, e.window, m_root, 0, 0);
    FocusWindow(e.window);

    LOG(INFO) << "Mapped window " << e.window;
}

void WindowManager::OnMapNotify(const XMapEvent& e) {}

void WindowManager::OnUnmapNotify(const XUnmapEvent& e) {
    // auto it = find(m_windows.begin(), m_windows.end(), e.window);
    // if (it != m_windows.end())
    //     m_windows.erase(it);
}

void WindowManager::OnButtonPress(const XButtonEvent& e) {
    if (e.subwindow == None)
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
    FocusWindow(e.subwindow);

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

void WindowManager::OnMotionNotify(const XMotionEvent& e) {
    if (e.subwindow == None)
        return;

    const pair<int, int> drag_pos = {e.x_root, e.y_root};
    const pair<int, int> delta = {drag_pos.first - m_drag_start_pos.first,
                                  drag_pos.second - m_drag_start_pos.second};

    // Alt
    if (e.state & Mod1Mask) {
        // Left button to move
        if (e.state & Button1Mask) {
            const pair<int, int> dest_frame_pos = {m_drag_start_frame_pos.first + delta.first,
                                                   m_drag_start_frame_pos.second + delta.second};
            XMoveWindow(m_display, e.subwindow, dest_frame_pos.first, dest_frame_pos.second);
        }

        // Right button to reisze
        if (e.state & Button3Mask) {
            const pair<int, int> size_delta = {max(delta.first, -m_drag_start_frame_size.first),
                                               max(delta.second, -m_drag_start_frame_size.second)};
            pair<int, int> dest_frame_size = {m_drag_start_frame_size.first + size_delta.first,
                                              m_drag_start_frame_size.second + size_delta.second};

            // Restrict minimum window size
            dest_frame_size.first = max(dest_frame_size.first, MIN_WINDOW_WIDTH);
            dest_frame_size.second = max(dest_frame_size.second, MIN_WINDOW_HEIGHT);

            // Resize window
            XResizeWindow(m_display, e.subwindow, dest_frame_size.first, dest_frame_size.second);
        }
    }
}

void WindowManager::OnKeyPress(const XKeyEvent& e) {
    // Alt
    if (e.state & Mod1Mask) {
        // Tab to switch active window to next one
        if (e.keycode == XKeysymToKeycode(m_display, XK_Tab)) {
            auto it = find(m_windows.begin(), m_windows.end(), m_active_window);
            CHECK(it != m_windows.end());
            it++;
            if (it == m_windows.end())
                it = m_windows.begin();
            FocusWindow(*it);
        }
    }
}

void WindowManager::OnKeyRelease(const XKeyEvent& e) {}
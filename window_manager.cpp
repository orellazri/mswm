#include "window_manager.hpp"

#include <X11/cursorfont.h>
#include <glog/logging.h>

#include <algorithm>

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
    XSelectInput(m_display, m_root, SubstructureRedirectMask | SubstructureNotifyMask);
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
        // LOG(INFO) << "Received event: " << e.type;

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
    XWindowChanges changes;
    changes.x = e.x;
    changes.y = e.y;
    changes.width = e.width;
    changes.height = e.height;
    changes.border_width = e.border_width;
    changes.sibling = e.above;
    changes.stack_mode = e.detail;

    // Configure frame if window exists
    if (m_clients.count(e.window)) {
        const Window frame = m_clients[e.window];
        XConfigureWindow(m_display, frame, e.value_mask, &changes);
        LOG(INFO) << "Resize [" << frame << "] to " << e.width << "x" << e.height;
    }

    XConfigureWindow(m_display, e.window, e.value_mask, &changes);
    LOG(INFO) << "Resize " << e.window << " to " << e.width << "x" << e.height;
}

void WindowManager::OnConfigureNotify(const XConfigureEvent& e) {}

void WindowManager::OnMapRequest(const XMapRequestEvent& e) {
    Frame(e.window);
    XMapWindow(m_display, e.window);
}

void WindowManager::OnMapNotify(const XMapEvent& e) {}

void WindowManager::OnUnmapNotify(const XUnmapEvent& e) {
    if (!m_clients.count(e.window)) {
        LOG(INFO) << "Ignore UnmapNotify for non-client window " << e.window;
        return;
    }

    Unframe(e.window);
}

void WindowManager::OnButtonPress(const XButtonEvent& e) {
    CHECK(m_clients.count(e.window));
    const Window frame = m_clients[e.window];

    // Save initial cursor position
    m_drag_start_pos = {e.x_root, e.y_root};

    // Save initial window info
    Window returned_root;
    int x, y;
    unsigned width, height, border_width, depth;
    CHECK(XGetGeometry(
        m_display,
        frame,
        &returned_root,
        &x,
        &y,
        &width,
        &height,
        &border_width,
        &depth));

    m_drag_start_frame_pos = {x, y};
    m_drag_start_frame_size = {width, height};

    XRaiseWindow(m_display, e.window);

    // Alt
    if (e.state & Mod1Mask) {
        // Middle button to close
        if (e.button == Button2) {
            // Try to gracefully kill client if the client supports the WM_DELETE_WINDOW behavior.
            // Otherwise, kill it.
            Atom* supported_protocols;
            int num_supported_protocols;
            if (XGetWMProtocols(m_display, e.window, &supported_protocols, &num_supported_protocols) &&
                find(supported_protocols, supported_protocols + num_supported_protocols, WM_DELETE_WINDOW) != supported_protocols + num_supported_protocols) {
                LOG(INFO) << "Gracefully deleting window " << e.window;
                XEvent msg = {0};
                msg.xclient.type = ClientMessage;
                msg.xclient.message_type = WM_PROTOCOLS;
                msg.xclient.window = e.window;
                msg.xclient.format = 32;
                msg.xclient.data.l[0] = WM_DELETE_WINDOW;
                CHECK(XSendEvent(m_display, e.window, false, 0, &msg));
            } else {
                LOG(INFO) << "Killing window " << e.window;
                XKillClient(m_display, e.window);
            }
        }
    }
}

void WindowManager::OnButtonRelease(const XButtonEvent& e) {}

void WindowManager::OnMotionNotify(const XMotionEvent& e) {
    CHECK(m_clients.count(e.window));
    const Window frame = m_clients[e.window];
    const pair<int, int> drag_pos = {e.x_root, e.y_root};
    const pair<int, int> delta = {drag_pos.first - m_drag_start_pos.first,
                                  drag_pos.second - m_drag_start_pos.second};

    // Alt
    if (e.state & Mod1Mask) {
        // Left button to move
        if (e.state & Button1Mask) {
            const pair<int, int> dest_frame_pos = {m_drag_start_frame_pos.first + delta.first,
                                                   m_drag_start_frame_pos.second + delta.second};
            XMoveWindow(m_display, frame, dest_frame_pos.first, dest_frame_pos.second);
        }

        // Right button to reisze
        if (e.state & Button3Mask) {
            const pair<int, int> size_delta = {max(delta.first, -m_drag_start_frame_size.first),
                                               max(delta.second, -m_drag_start_frame_size.second)};
            const pair<int, int> dest_frame_size = {m_drag_start_frame_size.first + size_delta.first,
                                                    m_drag_start_frame_size.second + size_delta.second};

            // Resize window and frame
            XResizeWindow(m_display, e.window, dest_frame_size.first, dest_frame_size.second);
            XResizeWindow(m_display, frame, dest_frame_size.first, dest_frame_size.second);
        }
    }
}

void WindowManager::Frame(Window w) {
    // Don't frame windows we've already framed
    CHECK(!m_clients.count(w));

    // Retrieve window attributes
    XWindowAttributes x_window_attrs;
    CHECK(XGetWindowAttributes(m_display, w, &x_window_attrs));

    // Create frame
    const Window frame = XCreateSimpleWindow(
        m_display,
        m_root,
        x_window_attrs.x, x_window_attrs.y,
        x_window_attrs.width, x_window_attrs.height,
        BORDER_WIDTH,
        BORDER_COLOR,
        BG_COLOR);

    XSelectInput(m_display, frame, SubstructureRedirectMask | SubstructureNotifyMask);
    XAddToSaveSet(m_display, w);
    XReparentWindow(m_display, w, frame, 0, 0);
    XMapWindow(m_display, frame);
    m_clients[w] = frame;

    // Capture mouse buttons
    XGrabButton(
        m_display,
        AnyButton,
        AnyModifier,
        w,
        false,
        ButtonPressMask | ButtonReleaseMask | ButtonMotionMask,
        GrabModeAsync,
        GrabModeAsync,
        None,
        None);

    LOG(INFO) << "Framed window " << w << " [" << frame << "]";
}

void WindowManager::Unframe(Window w) {
    CHECK(m_clients.count(w));
    const Window frame = m_clients[w];
    XUnmapWindow(m_display, frame);
    XReparentWindow(m_display, w, m_root, 0, 0);
    XRemoveFromSaveSet(m_display, w);
    XDestroyWindow(m_display, frame);
    m_clients.erase(w);

    LOG(INFO) << "Unframed window " << w << " [" << frame << "]";
}
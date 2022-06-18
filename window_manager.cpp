#include "window_manager.hpp"

#include <X11/cursorfont.h>
#include <glog/logging.h>

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

WindowManager::WindowManager(Display* display) : m_display(CHECK_NOTNULL(display)), m_root(DefaultRootWindow(m_display)) {
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
        LOG(INFO) << "Received event: " << e.type;

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
                LOG(WARNING) << "Ignored event";
        }
    }
}

int WindowManager::OnWMDetected(Display* display, XErrorEvent* e) {
    CHECK_EQ(static_cast<int>(e->error_code), BadAccess);
    m_wm_detected = true;
    return 0;
}

int WindowManager::OnXError(Display* display, XErrorEvent* e) {
    LOG(ERROR) << "Received X Error: " << int(e->error_code) << " from request: " << int(e->request_code);
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
    // TODO: Try using nullptr for unused values
    int x, y;
    unsigned width, height, border_width, depth;
    CHECK(XGetGeometry(
        m_display,
        frame,
        &returned_root,
        &x, &y,
        &width, &height,
        &border_width,
        &depth));

    m_drag_start_frame_pos = {x, y};
    m_drag_start_frame_size = {width, height};

    XRaiseWindow(m_display, frame);
}

void WindowManager::OnButtonRelease(const XButtonEvent& e) {}

void WindowManager::OnMotionNotify(const XMotionEvent& e) {
    CHECK(m_clients.count(e.window));
    const Window frame = m_clients[e.window];
    const pair<int, int> drag_pos = {e.x_root, e.y_root};
    const pair<int, int> delta = {drag_pos.first - m_drag_start_pos.first,
                                  drag_pos.second - m_drag_start_pos.second};
    const pair<int, int> dest_frame_pos = {m_drag_start_frame_pos.first + delta.first,
                                           m_drag_start_frame_pos.second + delta.second};

    XMoveWindow(m_display, frame, dest_frame_pos.first, dest_frame_pos.second);
}

void WindowManager::Frame(Window w) {
    const unsigned int BORDER_WIDTH = 3;
    const unsigned long BORDER_COLOR = 0x6c5ce7;
    const unsigned long BG_COLOR = 0xffffff;

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

    // Move windows with alt + left mouse button
    XGrabButton(
        m_display,
        Button1,
        AnyModifier,
        w,
        false,
        ButtonPressMask | ButtonReleaseMask | ButtonMotionMask,
        GrabModeAsync,
        GrabModeAsync,
        None,
        None);

    LOG(INFO)
        << "Framed window " << w << " [" << frame << "]";
}

void WindowManager::Unframe(Window w) {
    const Window frame = m_clients[w];
    XUnmapWindow(m_display, frame);
    XReparentWindow(m_display, w, m_root, 0, 0);
    XRemoveFromSaveSet(m_display, w);
    XDestroyWindow(m_display, frame);
    m_clients.erase(w);

    LOG(INFO) << "Unframed window " << w << " [" << frame << "]";
}
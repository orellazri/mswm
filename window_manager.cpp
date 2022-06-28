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
using std::string;
using std::unique_ptr;

bool WindowManager::wm_detected_;

unique_ptr<WindowManager> WindowManager::Create() {
    Display* display = XOpenDisplay(nullptr);
    if (display == nullptr) {
        LOG(ERROR) << "Failed to open X display " << XDisplayName(nullptr);
        return nullptr;
    }
    return unique_ptr<WindowManager>(new WindowManager(display));
}

WindowManager::WindowManager(Display* display) : display_(CHECK_NOTNULL(display)),
                                                 root_(DefaultRootWindow(display_)),
                                                 WM_PROTOCOLS(XInternAtom(display_, "WM_PROTOCOLS", false)),
                                                 WM_DELETE_WINDOW(XInternAtom(display_, "WM_DELETE_WINDOW", false)) {
}

WindowManager::~WindowManager() {
    XCloseDisplay(display_);
}

void WindowManager::Run() {
    // Initialization
    wm_detected_ = false;
    XSetErrorHandler(&WindowManager::OnWMDetected);

    // Alt + Mouse buttons
    XGrabButton(display_,
                AnyButton,
                Mod1Mask,
                root_,
                True,
                ButtonPressMask | ButtonReleaseMask | PointerMotionMask | OwnerGrabButtonMask,
                GrabModeAsync,
                GrabModeAsync,
                None,
                None);

    // Alt + Tab to switch active window
    XGrabKey(display_,
             XKeysymToKeycode(display_, XK_Tab),
             Mod1Mask,
             root_,
             False,
             GrabModeAsync,
             GrabModeAsync);

    // Alt + (Shift) + Enter for Terminal
    XGrabKey(display_,
             XKeysymToKeycode(display_, XK_Return),
             Mod1Mask | ShiftMask,
             root_,
             False,
             GrabModeAsync,
             GrabModeAsync);

    // Alt + Ctrl + Right for next workspace
    XGrabKey(display_,
             XKeysymToKeycode(display_, XK_Right),
             Mod1Mask | ControlMask,
             root_,
             False,
             GrabModeAsync,
             GrabModeAsync);

    // Alt + Ctrl + Left for previous workspace
    XGrabKey(display_,
             XKeysymToKeycode(display_, XK_Left),
             Mod1Mask | ControlMask,
             root_,
             False,
             GrabModeAsync,
             GrabModeAsync);

    // Alt + Shift + Ctrl + Right to move active window to next workpace
    XGrabKey(display_,
             XKeysymToKeycode(display_, XK_Right),
             Mod1Mask | ControlMask | ShiftMask,
             root_,
             False,
             GrabModeAsync,
             GrabModeAsync);

    // Alt + Shift + Ctrl + Left to move active window to previous workpace
    XGrabKey(display_,
             XKeysymToKeycode(display_, XK_Left),
             Mod1Mask | ControlMask | ShiftMask,
             root_,
             False,
             GrabModeAsync,
             GrabModeAsync);

    XSelectInput(display_, root_, SubstructureNotifyMask | SubstructureRedirectMask);

    XSync(display_, false);
    if (wm_detected_) {
        LOG(ERROR) << "Detected another window manager on display " << XDisplayString(display_);
        return;
    }

    XSetErrorHandler(&WindowManager::OnXError);

    // Show mouse cursor
    XDefineCursor(display_, root_, XCreateFontCursor(display_, XC_top_left_arrow));

    // Create status bar window
    status_bar_window_ = XCreateSimpleWindow(
        display_,
        root_,
        0, 0,
        DisplayWidth(display_, DefaultScreen(display_)), STATUS_BAR_HEIGHT,
        STATUS_BAR_BORDER_WIDTH,
        STATUS_BAR_BORDER_COLOR,
        STATUS_BAR_BG_COLOR);
    XMapWindow(display_, status_bar_window_);

    // Main event loop
    XEvent e;
    while (true) {
        XNextEvent(display_, &e);
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
                while (XCheckTypedWindowEvent(display_, e.xmotion.subwindow, MotionNotify, &e)) {
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
    wm_detected_ = true;
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
    XSetWindowBorderWidth(display_, w, width);

    Colormap colormap = DefaultColormap(display_, DefaultScreen(display_));
    XColor color;
    CHECK(XAllocNamedColor(display_, colormap, color_str, &color, &color));
    XSetWindowBorder(display_, w, color.pixel);
}

void WindowManager::FocusWindow(const Window& w) {
    // Raise and change border on current window
    SetWindowBorder(w, BORDER_WIDTH_ACTIVE, BORDER_COLOR_ACTIVE);
    XRaiseWindow(display_, w);

    active_window_ = w;

    // Change border of all other windows to inactive
    for (auto& window : windows_) {
        if (window.first == w) {
            continue;
        }
        SetWindowBorder(window.first, BORDER_WIDTH_INACTIVE, BORDER_COLOR_INACTIVE);
    }

    // Write window title to status bar
    XTextProperty xtext;
    XGetWMName(display_, w, &xtext);
    WriteToStatusBar(string(reinterpret_cast<char*>(xtext.value)));
}

void WindowManager::WriteToStatusBar(const string message) {
    // Write active workspace number and message
    std::stringstream fmt;
    fmt << "[" << active_workspace_ << "] " << message;

    XClearWindow(display_, status_bar_window_);
    XDrawString(display_,
                status_bar_window_,
                DefaultGC(display_, DefaultScreen(display_)),
                16, 16,
                fmt.str().c_str(),
                fmt.str().length());
}

void WindowManager::SwitchWorkspace(const int workspace) {
    CHECK(workspace >= 0);
    CHECK(workspace < num_workspaces_);

    // Hide windows of current workspace
    for (auto& window : windows_) {
        if (window.second != active_workspace_)
            continue;
        XWithdrawWindow(display_, window.first, DefaultScreen(display_));
    }

    // Show windows of new workspace
    for (auto& window : windows_) {
        if (window.second != workspace)
            continue;
        XMapWindow(display_, window.first);
    }

    active_workspace_ = workspace;
    WriteToStatusBar("");
}

void WindowManager::CreateWorkspace() {
    num_workspaces_++;
    SwitchWorkspace(num_workspaces_ - 1);
}

void WindowManager::OnCreateNotify(const XCreateWindowEvent& e) {}

void WindowManager::OnDestroyNotify(const XDestroyWindowEvent& e) {
    // Remove window from windows vector and switch active window
    auto it = windows_.find(e.window);
    if (it != windows_.end()) {
        windows_.erase(it);
    }
}

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
    XConfigureWindow(display_, e.window, e.value_mask, &wc);
}

void WindowManager::OnConfigureNotify(const XConfigureEvent& e) {}

void WindowManager::OnMapRequest(const XMapRequestEvent& e) {
    windows_[e.window] = active_workspace_;

    XMapWindow(display_, e.window);
    XReparentWindow(display_, e.window, root_, 0, 0);
    XMoveWindow(display_, e.window, 0, STATUS_BAR_HEIGHT);
    FocusWindow(e.window);

    LOG(INFO) << "Mapped window " << e.window;
}

void WindowManager::OnMapNotify(const XMapEvent& e) {}

void WindowManager::OnUnmapNotify(const XUnmapEvent& e) {}

void WindowManager::OnButtonPress(const XButtonEvent& e) {
    if (e.subwindow == None)
        return;

    // Save initial cursor position
    drag_start_pos_ = {e.x_root, e.y_root};

    // Save initial window info
    Window returned_root;
    int x, y;
    unsigned width, height, border_width, depth;
    CHECK(XGetGeometry(
        display_,
        e.subwindow,
        &returned_root,
        &x,
        &y,
        &width,
        &height,
        &border_width,
        &depth));

    drag_start_frame_pos_ = {x, y};
    drag_start_frame_size_ = {width, height};

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
            if (XGetWMProtocols(display_, e.subwindow, &supported_protocols, &num_supported_protocols) &&
                find(supported_protocols, supported_protocols + num_supported_protocols, WM_DELETE_WINDOW) != supported_protocols + num_supported_protocols) {
                LOG(INFO) << "Gracefully deleting window " << e.subwindow;
                XEvent msg = {0};
                msg.xclient.type = ClientMessage;
                msg.xclient.message_type = WM_PROTOCOLS;
                msg.xclient.window = e.subwindow;
                msg.xclient.format = 32;
                msg.xclient.data.l[0] = WM_DELETE_WINDOW;
                CHECK(XSendEvent(display_, e.subwindow, false, 0, &msg));
            } else {
                LOG(INFO) << "Killing window " << e.subwindow;
                XKillClient(display_, e.subwindow);
            }
        }
    }
}

void WindowManager::OnButtonRelease(const XButtonEvent& e) {}

void WindowManager::OnMotionNotify(const XMotionEvent& e) {
    if (e.subwindow == None)
        return;

    const pair<int, int> drag_pos = {e.x_root, e.y_root};
    const pair<int, int> delta = {drag_pos.first - drag_start_pos_.first,
                                  drag_pos.second - drag_start_pos_.second};

    // Alt
    if (e.state & Mod1Mask) {
        // Left button to move
        if (e.state & Button1Mask) {
            const pair<int, int> dest_frame_pos = {drag_start_frame_pos_.first + delta.first,
                                                   drag_start_frame_pos_.second + delta.second};

            // Don't move window above status bar
            if (dest_frame_pos.second < STATUS_BAR_HEIGHT)
                return;

            XMoveWindow(display_, e.subwindow, dest_frame_pos.first, dest_frame_pos.second);
        }

        // Right button to reisze
        if (e.state & Button3Mask) {
            const pair<int, int> size_delta = {max(delta.first, -drag_start_frame_size_.first),
                                               max(delta.second, -drag_start_frame_size_.second)};
            pair<int, int> dest_frame_size = {drag_start_frame_size_.first + size_delta.first,
                                              drag_start_frame_size_.second + size_delta.second};

            // Restrict minimum window size
            dest_frame_size.first = max(dest_frame_size.first, MIN_WINDOW_WIDTH);
            dest_frame_size.second = max(dest_frame_size.second, MIN_WINDOW_HEIGHT);

            // Resize window
            XResizeWindow(display_, e.subwindow, dest_frame_size.first, dest_frame_size.second);
        }
    }
}

void WindowManager::OnKeyPress(const XKeyEvent& e) {
    // Alt
    if (e.state & Mod1Mask) {
        // Alt + Tab to switch active window to next one
        if (e.keycode == XKeysymToKeycode(display_, XK_Tab)) {
            // Don't switch if there are no windows
            if (windows_.size() == 0)
                return;

            auto it = windows_.find(e.window);
            if (it == windows_.end()) {
                it = windows_.begin();
            } else {
                it++;
                if (it == windows_.end())
                    it = windows_.begin();
            }

            FocusWindow(it->first);
            return;
        }

        // Alt + Shift
        if (e.state & ShiftMask) {
            // Alt + Shift + Enter to open terminal
            if (e.keycode == XKeysymToKeycode(display_, XK_Return)) {
                if (fork() == 0) {
                    char* argument_list[] = {"xterm", NULL};
                    execvp("xterm", argument_list);
                    exit(EXIT_SUCCESS);
                }
                return;
            }

            // Alt + Shift + Ctrl
            if (e.state & ControlMask) {
                // Alt + Shift + Ctrl + Right
                if (e.keycode == XKeysymToKeycode(display_, XK_Right)) {
                    if (active_window_ == 0)
                        return;

                    // Check if there is a next workspace
                    if (active_workspace_ == num_workspaces_ - 1)
                        return;

                    // Hide window
                    XWithdrawWindow(display_, active_window_, DefaultScreen(display_));

                    // Move window to next workspace
                    windows_[active_window_] = active_workspace_ + 1;
                    active_window_ = 0;

                    WriteToStatusBar("");
                    return;
                }

                // Alt + Shift + Ctrl + Left
                if (e.keycode == XKeysymToKeycode(display_, XK_Left)) {
                    if (active_window_ == 0)
                        return;

                    // Check if there is a previous workspace
                    if (active_workspace_ == 0)
                        return;

                    // Hide window
                    XWithdrawWindow(display_, active_window_, DefaultScreen(display_));

                    // Move window to next workspace
                    windows_[active_window_] = active_workspace_ - 1;
                    active_window_ = 0;

                    WriteToStatusBar("");
                    return;
                }
            }
        }

        // Alt + Ctrl
        if (e.state & ControlMask) {
            // Alt + Ctrl + Right
            if (e.keycode == XKeysymToKeycode(display_, XK_Right)) {
                // Switch to next workspace
                if (active_workspace_ < num_workspaces_ - 1) {
                    // TODO
                    SwitchWorkspace(active_workspace_ + 1);
                } else {
                    // Create new workspace if on the last one
                    CreateWorkspace();
                }
                return;
            }

            // Alt + Ctrl + Left
            if (e.keycode == XKeysymToKeycode(display_, XK_Left)) {
                // Switch to previous workspace if we are not on the first one
                if (active_workspace_ == 0)
                    return;

                SwitchWorkspace(active_workspace_ - 1);
                return;
            }
        }
    }
}

void WindowManager::OnKeyRelease(const XKeyEvent& e) {}
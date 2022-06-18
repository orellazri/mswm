#include "window_manager.hpp"

#include <glog/logging.h>

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
    m_wm_detected = false;
    XSetErrorHandler(&WindowManager::OnWMDetected);
    XSelectInput(m_display, m_root, SubstructureRedirectMask | SubstructureNotifyMask);
    XSync(m_display, false);
    if (m_wm_detected) {
        LOG(ERROR) << "Detected another window manager on display " << XDisplayString(m_display);
        return;
    }
    XSetErrorHandler(&WindowManager::OnXError);
}

int WindowManager::OnWMDetected(Display* display, XErrorEvent* e) {
    CHECK_EQ(static_cast<int>(e->error_code), BadAccess);
    m_wm_detected = true;
    return 0;
}

int WindowManager::OnXError(Display* display, XErrorEvent* e) {
    LOG(ERROR) << "Received X Error: " << int(e->error_code);
    return 0;
}
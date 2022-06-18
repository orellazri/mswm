#include <glog/logging.h>

#include <cstdlib>

#include "window_manager.hpp"

int main(int argc, char** argv) {
    google::InitGoogleLogging(argv[0]);

    std::unique_ptr<WindowManager> window_manager(WindowManager::Create());
    if (!window_manager) {
        LOG(ERROR) << "Failed to initialize window manager";
        return EXIT_FAILURE;
    }

    window_manager->Run();

    return EXIT_SUCCESS;
}
#define NOMINMAX  // Prevent Windows.h min/max macro conflicts
#include "../Rift Recon - Core/ApplicationController.h"
#include "../Rift Recon - Core/Common.h"
#include <iostream>
#include <stdexcept>

int main() {
    try {
        // Create and initialize the application controller
        LeagueRecorder::ApplicationController app;

        if (!app.initialize()) {
            std::cerr << "Failed to initialize application. Exiting." << std::endl;
            return 1;
        }

        // Run the application
        app.run();

        return 0;
    }
    catch (const std::exception& e) {
        std::cerr << "Unhandled exception: " << e.what() << std::endl;
        return 1;
    }
    catch (...) {
        std::cerr << "Unknown exception occurred" << std::endl;
        return 1;
    }
}
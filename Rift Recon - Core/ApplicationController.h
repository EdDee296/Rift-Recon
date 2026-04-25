#pragma once

#include "GameStateMonitor.h"
#include "ChampionDetector.h"
#include "ScreenCapture.h"
#include "GameCaptureEngine.h"
#include <atomic>
#include <thread>

namespace LeagueRecorder {

    class ApplicationController {
    public:
        ApplicationController();
        ~ApplicationController();

        // Initialize and start the application
        bool initialize();

        // Run the main application loop
        void run();

        // Stop the application
        void shutdown();

    private:
        // Components
        GameStateMonitor m_gameMonitor;
        ChampionDetector m_championDetector;
        ScreenCapture m_screenCapture;
        GameCaptureEngine m_gameCaptureEngine;

        // State flags
        std::atomic<bool> m_shouldExit;

        // Input handling thread
        std::thread m_inputThread;

        // Event handlers
        void onGameStarted(const std::vector<std::string>& enemyChampions, const std::string& playerTeam);
        void onGameEnded();

        // Input handling
        void handleUserInput();

        // Status updates
        void onStatusUpdate(const std::string& status);
    };

} // namespace LeagueRecorder
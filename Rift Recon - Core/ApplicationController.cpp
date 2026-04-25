#include "ApplicationController.h"
#include "Common.h"
#include <windows.h> // For GetAsyncKeyState

namespace LeagueRecorder {

    ApplicationController::ApplicationController()
        : m_gameCaptureEngine(m_screenCapture, m_championDetector)
        , m_shouldExit(false)
    {
    }

    ApplicationController::~ApplicationController() {
        shutdown();
    }

    bool ApplicationController::initialize() {
        // Register callbacks
        m_gameMonitor.setGameStartedCallback([this](auto& champions, auto& team) {
            this->onGameStarted(champions, team);
            });

        m_gameMonitor.setGameEndedCallback([this]() {
            this->onGameEnded();
            });

        m_gameCaptureEngine.setStatusCallback([this](auto& status) {
            this->onStatusUpdate(status);
            });

        // Initialize the screen capture
        if (!m_screenCapture.initialize()) {
            LOG("[ApplicationController] Failed to initialize screen capture");
            return false;
        }

        // Connect ChampionDetector to the WebSocket server
        m_championDetector.setWebSocketServer(m_gameMonitor.getWebSocketServer());

        LOG("[ApplicationController] Application initialized successfully");
        return true;
    }

    void ApplicationController::run() {
        // Start the game state monitor
        m_gameMonitor.start();
        LOG("[ApplicationController] Starting application. Waiting for game...");
        LOG("[ApplicationController] Controls: LEFT ARROW = Stop recording, DOWN ARROW = Select capture region");

        // Start input handling thread
        m_shouldExit = false;
        m_inputThread = std::thread(&ApplicationController::handleUserInput, this);

        // Main application loop
        while (!m_shouldExit) {
            // Most of the work happens in the event callbacks and threads
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        // Clean up when done
        shutdown();
    }

    void ApplicationController::shutdown() {
        // Signal threads to stop
        m_shouldExit = true;

        // Stop game capture processing first
        m_gameCaptureEngine.stopRecording("application shutdown");

        // Stop game monitoring
        m_gameMonitor.stop();

        // Wait for input thread to finish
        if (m_inputThread.joinable()) {
            m_inputThread.join();
        }

        // Clean up screen capture
        m_screenCapture.cleanup();

        LOG("[ApplicationController] Application shutdown complete");
    }

    void ApplicationController::onGameStarted(const std::vector<std::string>& enemyChampions, const std::string& playerTeam) {
        LOG("[ApplicationController] Game detected - loading champion templates...");

        // Initialize the champion detector with enemy champions
        m_championDetector.initialize(enemyChampions);
        /*m_championDetector.debugShowResizedTemplates();*/

        // Start recording
        if (m_gameCaptureEngine.startRecording()) {
            LOG("[ApplicationController] Recording started");
        }
        else {
            LOG("[ApplicationController] Failed to start recording");
        }
    }

    void ApplicationController::onGameEnded() {
        LOG("[ApplicationController] Game ended");

        // Stop recording
        m_gameCaptureEngine.stopRecording("game ended");

        // Clear champion templates to save memory
        m_championDetector.clearTemplates();
    }

    void ApplicationController::handleUserInput() {
        while (!m_shouldExit) {
            // Check for left arrow key to stop recording
            //if (GetAsyncKeyState(VK_LEFT) & 0x8000) {
            //    LOG("[ApplicationController] LEFT ARROW pressed - stopping current recording");
            //    m_gameCaptureEngine.stopRecording("manual stop");

            //    // Wait until key is released to avoid multiple stops
            //    while (GetAsyncKeyState(VK_LEFT) & 0x8000) {
            //        Sleep(10);
            //    }
            //}

            // Check for down arrow key to select region
            if (GetAsyncKeyState(VK_DOWN) & 0x8000) {
                LOG("[ApplicationController] DOWN ARROW pressed - selecting capture region");
                m_gameCaptureEngine.selectCaptureRegion();

                // Wait until key is released to avoid multiple selections
                while (GetAsyncKeyState(VK_DOWN) & 0x8000) {
                    Sleep(10);
                }
            }

            // Check for escape key to exit application
            //if (GetAsyncKeyState(VK_ESCAPE) & 0x8000) {
            //    LOG("[ApplicationController] ESC pressed - exiting application");
            //    m_shouldExit = true;

            //    // Wait until key is released
            //    while (GetAsyncKeyState(VK_ESCAPE) & 0x8000) {
            //        Sleep(10);
            //    }
            //}

            // Sleep to reduce CPU usage
            Sleep(100);
        }
    }

    void ApplicationController::onStatusUpdate(const std::string& status) {
        // Nothing extra to do here - already logged by GameCaptureEngine
    }

} // namespace LeagueRecorder
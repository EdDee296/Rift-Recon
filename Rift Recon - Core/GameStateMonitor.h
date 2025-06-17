#pragma once

#define CPPHTTPLIB_OPENSSL_SUPPORT
#include <httplib.h>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <functional>
#include "Common.h"
#include "wsk.h"

using json = nlohmann::json;

namespace LeagueRecorder {

    class GameStateMonitor {
    public:
        using GameStartedCallback = std::function<void(const std::vector<std::string>&, const std::string&)>;
        using GameEndedCallback = std::function<void()>;

        GameStateMonitor();
        ~GameStateMonitor();

        void start();
        void stop();

        bool isGameActive() const;
        std::vector<std::string> getEnemyChampions();

        void setGameStartedCallback(GameStartedCallback callback);
        void setGameEndedCallback(GameEndedCallback callback);

        // WebSocket access methods
        WebSocketServer* getWebSocketServer() { return m_webSocketServer.get(); }
        bool broadcastMessage(const std::string& message);

    private:
        void pollLoop();

        std::atomic<bool> m_shouldExit;
        std::atomic<bool> m_gameActive;
        std::thread m_pollThread;

        GameStartedCallback m_gameStartedCallback;
        GameEndedCallback m_gameEndedCallback;

        mutable std::mutex m_dataMutex;
        std::vector<std::string> m_enemyNames;
        std::string m_playerTeam;

        // WebSocket server to provide real-time updates
        std::unique_ptr<WebSocketServer> m_webSocketServer;
        bool m_webSocketServerStarted;
        static constexpr int WS_PORT = 8080; // WebSocket server port
    };

} // namespace LeagueRecorder
#include "GameStateMonitor.h"

namespace LeagueRecorder {

    GameStateMonitor::GameStateMonitor()
        : m_shouldExit(false), m_gameActive(false), m_webSocketServerStarted(false) {
        // Create WebSocket server but don't start it yet
        m_webSocketServer = std::make_unique<WebSocketServer>(WS_PORT);

        // Set up WebSocket client connection callback
        m_webSocketServer->setClientConnectCallback([this](size_t clientCount) {
            LOG("[GameStateMonitor] WebSocket client connected. Total clients: " + std::to_string(clientCount));
            });
    }

    GameStateMonitor::~GameStateMonitor() {
        stop();
    }

    void GameStateMonitor::start() {
        // Don't start if already running
        if (m_pollThread.joinable()) {
            return;
        }

        m_shouldExit = false;
        m_pollThread = std::thread(&GameStateMonitor::pollLoop, this);
        LOG("[GameStateMonitor] Started monitoring for active games");
    }

    void GameStateMonitor::stop() {
        m_shouldExit = true;
        if (m_pollThread.joinable()) {
            m_pollThread.join();
        }

        // Stop WebSocket server if running
        if (m_webSocketServer && m_webSocketServerStarted) {
            m_webSocketServer->stop();
            m_webSocketServerStarted = false;
        }

        LOG("[GameStateMonitor] Stopped monitoring");
    }

    bool GameStateMonitor::isGameActive() const {
        return m_gameActive.load();
    }

    void GameStateMonitor::setGameStartedCallback(GameStartedCallback callback) {
        m_gameStartedCallback = callback;
    }

    void GameStateMonitor::setGameEndedCallback(GameEndedCallback callback) {
        m_gameEndedCallback = callback;
    }

    std::vector<std::string> GameStateMonitor::getEnemyChampions() {
        std::lock_guard<std::mutex> lock(m_dataMutex);
        return m_enemyNames;
    }

    bool GameStateMonitor::broadcastMessage(const std::string& message) {
        if (m_webSocketServer && m_webSocketServerStarted) {
            return m_webSocketServer->broadcastMessage(message);
        }
        return false;
    }

    void GameStateMonitor::pollLoop() {
        // Connect to localhost:2999 over HTTPS
        httplib::SSLClient cli("127.0.0.1", 2999);
        // disable certificate verification (LCU uses a self‐signed cert)
        cli.enable_server_certificate_verification(false);

        while (!m_shouldExit) {
            bool previousGameState = m_gameActive.load();

            auto res = cli.Get("/liveclientdata/playerlist");
            if (res && res->status == 200) {
                try {
                    // Start WebSocket server if not already started
                    if (!m_webSocketServerStarted && m_webSocketServer) {
                        if (m_webSocketServer->start()) {
                            m_webSocketServerStarted = true;
                            LOG("[GameStateMonitor] WebSocket server started on port " + std::to_string(WS_PORT));
                        }
                    }

                    // Parse JSON response
                    json players = json::parse(res->body);

                    // Determine player's team and enemy champions
                    std::string localPlayerName;

                    // First, get the local player information
                    auto activePlayerRes = cli.Get("/liveclientdata/activeplayername");
                    if (activePlayerRes && activePlayerRes->status == 200) {
                        // Remove quotes if present in the response
                        localPlayerName = activePlayerRes->body;
                        if (localPlayerName.front() == '"' && localPlayerName.back() == '"') {
                            localPlayerName = localPlayerName.substr(1, localPlayerName.size() - 2);
                        }
                    }

                    // Clear previous enemy names and determine teams
                    std::lock_guard<std::mutex> lock(m_dataMutex);
                    m_enemyNames.clear();
                    m_playerTeam.clear();

                    // Find player's team
                    for (const auto& player : players) {
                        std::string summonerName = player["summonerName"];
                        if (summonerName == localPlayerName) {
                            m_playerTeam = player["team"];
                            break;
                        }
                    }

                    // If we found player's team, collect enemy champions
                    if (!m_playerTeam.empty()) {
                        for (const auto& player : players) {
                            std::string team = player["team"];
                            if (team != m_playerTeam) {
                                std::string championName = player["championName"];
                                m_enemyNames.push_back(championName);
                            }
                        }
                    }

                    // Game is active since we got valid response
                    bool hasChampions = !m_enemyNames.empty();
                    bool wasActive = m_gameActive.load();

                    // If game just started, trigger callback
                    if (!wasActive && hasChampions) {
                        m_gameActive = true;
                        if (m_gameStartedCallback) {
                            m_gameStartedCallback(m_enemyNames, m_playerTeam);
                        }
                        LOG("[GameStateMonitor] Game has started");
                        std::string champions;
                        for (const auto& name : m_enemyNames) {
                            if (!champions.empty()) champions += ", ";
                            champions += name;
                        }
                        LOGF("[GameStateMonitor] Enemy champions: %s", champions.c_str());

                        // Send game started notification to WebSocket clients
                        if (m_webSocketServerStarted) {
                            json gameStartedMsg = {
                                {"event", "game_started"},
                                {"player_team", m_playerTeam},
                                {"enemy_champions", m_enemyNames}
                            };
                            m_webSocketServer->broadcastMessage(gameStartedMsg.dump());
                        }
                    }
                }
                catch (const std::exception& e) {
                    LOG("[GameStateMonitor] Error parsing JSON: " + std::string(e.what()));
                }
            }
            else {
                bool wasActive = m_gameActive.exchange(false);

                // If game just ended, trigger callback
                if (wasActive && m_gameEndedCallback) {
                    m_gameEndedCallback();
                    LOG("[GameStateMonitor] Game has ended");

                    // Send game ended notification to WebSocket clients
                    if (m_webSocketServerStarted) {
                        json gameEndedMsg = {
                            {"event", "game_ended"}
                        };
                        m_webSocketServer->broadcastMessage(gameEndedMsg.dump());
                    }
                }
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
    }

} // namespace LeagueRecorder

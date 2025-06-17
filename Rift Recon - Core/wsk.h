#pragma once
#define WIN32_LEAN_AND_MEAN  // Prevents Windows.h from including Winsock.h
#define _WINSOCKAPI_ 
#include <winsock2.h>
#include <ws2tcpip.h> 
#include <windows.h>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/strand.hpp>
#include <iostream>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <functional>
#include <memory>
#include <boost/asio.hpp>
#include <deque>

namespace LeagueRecorder {

    class WebSocketSession;

    class WebSocketServer {
    public:
        // Callback type for client connection events (receives the client count)
        using ClientCallback = std::function<void(size_t)>;

        // Constructor takes a port number
        explicit WebSocketServer(int port);
        ~WebSocketServer();

        // Start/stop the server
        bool start();
        void stop();

        // Send a message to all connected clients
        bool broadcastMessage(const std::string& message);

        // Server status
        bool isRunning() const;
        int getConnectedClientCount() const;

        // Set callback for client connection events
        void setClientConnectCallback(ClientCallback cb);

    private:
        // Accept new connections
        void doAccept();

        // Server state
        int m_port;
        boost::asio::io_context m_ioc;
        boost::asio::ip::tcp::acceptor m_acceptor;
        std::thread m_thread;
        std::atomic<bool> m_isRunning;

        // Clients management
        mutable std::mutex m_clientMutex;
        std::vector<std::shared_ptr<WebSocketSession>> m_clients;
        ClientCallback m_clientCallback;
    };

} // namespace LeagueRecorder

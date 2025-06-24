#include "pch.h"
#include "../Rift Recon - Core/wsk.h"
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/connect.hpp>
#include <chrono>
#include <thread>
#include <future>
#include <atomic>

using namespace LeagueRecorder;
namespace beast = boost::beast;
namespace websocket = beast::websocket;
namespace net = boost::asio;
using tcp = net::ip::tcp;

// Beast WebSocket client for testing
class WebSocketTestClient {
public:
    WebSocketTestClient() : resolver_(ioc_), ws_(ioc_) {}

    ~WebSocketTestClient() {
        disconnect();
    }

    bool connect(const std::string& host, const std::string& port) {
        try {
            // Resolve the host
            auto const results = resolver_.resolve(host, port);

            // Connect to the server
            auto ep = net::connect(beast::get_lowest_layer(ws_), results);

            // Set a decorator to change the User-Agent of the handshake
            ws_.set_option(websocket::stream_base::decorator(
                [](websocket::request_type& req) {
                    req.set(beast::http::field::user_agent, "Boost.Beast WebSocket Test Client");
                }));

            // Perform the websocket handshake
            ws_.handshake(host + ':' + port, "/");

            connected_ = true;
            return true;
        }
        catch (const std::exception& e) {
            std::cerr << "Client connection failed: " << e.what() << std::endl;
            return false;
        }
    }

    void disconnect() {
        if (!connected_) return;

        try {
            ws_.close(websocket::close_code::normal);
            connected_ = false;
        }
        catch (const std::exception& e) {
            std::cerr << "Client disconnect error: " << e.what() << std::endl;
        }
    }

    bool sendMessage(const std::string& message) {
        if (!connected_) return false;

        try {
            ws_.write(net::buffer(message));
            return true;
        }
        catch (const std::exception& e) {
            std::cerr << "Client send error: " << e.what() << std::endl;
            return false;
        }
    }

    std::string readMessage() {
        if (!connected_) return "";

        try {
            beast::flat_buffer buffer;
            ws_.read(buffer);
            return beast::buffers_to_string(buffer.data());
        }
        catch (const std::exception& e) {
            std::cerr << "Client read error: " << e.what() << std::endl;
            return "";
        }
    }

    // Async read with timeout
    std::future<std::string> readMessageAsync() {
        return std::async(std::launch::async, [this]() {
            return readMessage();
            });
    }

    bool isConnected() const { return connected_; }

    void runInBackground() {
        if (backgroundThread_.joinable()) return;

        backgroundThread_ = std::thread([this]() {
            try {
                ioc_.run();
            }
            catch (const std::exception& e) {
                std::cerr << "Client background thread error: " << e.what() << std::endl;
            }
            });
    }

    void stopBackground() {
        ioc_.stop();
        if (backgroundThread_.joinable()) {
            backgroundThread_.join();
        }
        ioc_.restart();
    }

private:
    net::io_context ioc_;
    tcp::resolver resolver_;
    websocket::stream<tcp::socket> ws_;
    std::atomic<bool> connected_{ false };
    std::thread backgroundThread_;
};

class WebSocketServerTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Use a different port for each test to avoid conflicts
        static int portCounter = 12345;
        port = portCounter++;
        server = std::make_unique<WebSocketServer>(port);
    }

    void TearDown() override {
        if (server && server->isRunning()) {
            server->stop();
        }
        server.reset();
    }

    // Helper function to wait for a condition with timeout
    template<typename Predicate>
    bool waitForCondition(Predicate pred, std::chrono::milliseconds timeout = std::chrono::milliseconds(5000)) {
        auto start = std::chrono::steady_clock::now();
        while (!pred() && (std::chrono::steady_clock::now() - start) < timeout) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return pred();
    }

    std::unique_ptr<WebSocketServer> server;
    int port;
};

// Test basic server lifecycle
TEST_F(WebSocketServerTest, ServerStartStop) {
    // Test initial state
    EXPECT_FALSE(server->isRunning());
    EXPECT_EQ(server->getConnectedClientCount(), 0);

    // Test start
    EXPECT_TRUE(server->start());
    EXPECT_TRUE(server->isRunning());

    // Give the server a moment to fully initialize
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Test stop
    server->stop();
    EXPECT_FALSE(server->isRunning());
}

// Test starting server twice
TEST_F(WebSocketServerTest, DoubleStart) {
    EXPECT_TRUE(server->start());
    EXPECT_TRUE(server->isRunning());

    // Starting again should return true but not cause issues
    EXPECT_TRUE(server->start());
    EXPECT_TRUE(server->isRunning());
}

// Test stopping server when not running
TEST_F(WebSocketServerTest, StopWhenNotRunning) {
    EXPECT_FALSE(server->isRunning());

    // Should not cause any issues
    server->stop();
    EXPECT_FALSE(server->isRunning());
}

// Test broadcasting messages
TEST_F(WebSocketServerTest, BroadcastMessage) {
    EXPECT_TRUE(server->start());
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Test broadcasting when no clients are connected
    EXPECT_TRUE(server->broadcastMessage("test message"));

    // Create a client to receive messages
    WebSocketTestClient client;
    EXPECT_TRUE(client.connect("localhost", std::to_string(port)));

    // Wait for connection to be registered
    EXPECT_TRUE(waitForCondition([&]() {
        return server->getConnectedClientCount() > 0;
        }));

    // Send a broadcast message
    const std::string testMessage = "Hello WebSocket!";
    EXPECT_TRUE(server->broadcastMessage(testMessage));

    // Read the message with timeout
    auto messageFuture = client.readMessageAsync();
    auto status = messageFuture.wait_for(std::chrono::seconds(5));

    ASSERT_EQ(status, std::future_status::ready) << "Message receive timeout";

    std::string receivedMessage = messageFuture.get();
    EXPECT_EQ(receivedMessage, testMessage);

    client.disconnect();
}

// Test broadcasting when server is not running
TEST_F(WebSocketServerTest, BroadcastWhenNotRunning) {
    EXPECT_FALSE(server->isRunning());
    EXPECT_FALSE(server->broadcastMessage("test message"));
}

// Test client callback functionality
TEST_F(WebSocketServerTest, ClientCallback) {
    std::atomic<int> callbackInvocations{ 0 };
    std::vector<size_t> clientCounts;
    std::mutex callbackMutex;

    server->setClientConnectCallback([&](size_t count) {
        std::lock_guard<std::mutex> lock(callbackMutex);
        callbackInvocations++;
        clientCounts.push_back(count);
        });

    EXPECT_TRUE(server->start());
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Create a client
    WebSocketTestClient client;
    EXPECT_TRUE(client.connect("localhost", std::to_string(port)));

    // Wait for callback
    EXPECT_TRUE(waitForCondition([&]() { return callbackInvocations.load() > 0; }));

    {
        std::lock_guard<std::mutex> lock(callbackMutex);
        EXPECT_GE(callbackInvocations.load(), 1);
        EXPECT_FALSE(clientCounts.empty());
        EXPECT_EQ(clientCounts.back(), 1);
    }

    client.disconnect();
}


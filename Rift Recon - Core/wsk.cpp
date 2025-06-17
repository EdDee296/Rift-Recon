#include "wsk.h"

namespace beast = boost::beast;
namespace websocket = beast::websocket;
namespace net = boost::asio;
using tcp = net::ip::tcp;

namespace LeagueRecorder {

    class WebSocketSession : public std::enable_shared_from_this<WebSocketSession> {
    public:
        std::deque<std::shared_ptr<std::string>> m_writeQueue;
        bool m_writing = false;

        explicit WebSocketSession(tcp::socket socket)
            : m_ws(std::move(socket)) {
        }

        void start() {
            m_ws.set_option(websocket::stream_base::timeout::suggested(
                beast::role_type::server));
            m_ws.set_option(websocket::stream_base::decorator(
                [](websocket::response_type& res) {
                    res.set(beast::http::field::server,
                        "LeagueRecorder WebSocket Server");
                }));

            // Accept the websocket handshake
            m_ws.async_accept(
                beast::bind_front_handler(
                    &WebSocketSession::onAccept,
                    shared_from_this()));
        }

        void send(std::shared_ptr<std::string> msg) {
            net::post(
                m_ws.get_executor(),
                [self = shared_from_this(), msg]() {
                    bool writingInProgress = !self->m_writeQueue.empty();
                    self->m_writeQueue.push_back(msg);
                    if (!writingInProgress) {
                        self->doWrite();
                    }
                });
        }


        void close() {
            net::post(
                m_ws.get_executor(),
                beast::bind_front_handler(
                    &WebSocketSession::doClose,
                    shared_from_this()));
        }

    private:
        void onAccept(beast::error_code ec) {
            if (ec) {
                std::cerr << "WebSocket accept error: " << ec.message() << std::endl;
                return;
            }

            // Start reading messages (just to keep connection alive and detect disconnects)
            doRead();
        }

        void doRead() {
            m_ws.async_read(
                m_buffer,
                beast::bind_front_handler(
                    &WebSocketSession::onRead,
                    shared_from_this()));
        }

        void onRead(beast::error_code ec, std::size_t bytes_transferred) {
            if (ec == websocket::error::closed) {
                return;
            }

            if (ec) {
                std::cerr << "WebSocket read error: " << ec.message() << std::endl;
                return;
            }

            // Clear the buffer for the next read
            m_buffer.consume(m_buffer.size());

            // Continue reading
            doRead();
        }

        void doWrite() {
            auto msg = m_writeQueue.front();
            m_ws.async_write(
                net::buffer(*msg),
                [self = shared_from_this(), msg](beast::error_code ec, std::size_t) {
                    if (ec) {
                        std::cerr << "WebSocket write error: " << ec.message() << "\n";
                        return;
                    }

                    self->m_writeQueue.pop_front();
                    if (!self->m_writeQueue.empty()) {
                        self->doWrite();
                    }
                });
        }



        void onWrite(beast::error_code ec, std::size_t bytes_transferred) {
            if (ec) {
                std::cerr << "WebSocket write error: " << ec.message() << std::endl;
                return;
            }
        }

        void doClose() {
            beast::error_code ec;
            m_ws.close(websocket::close_code::normal, ec);
            if (ec) {
                std::cerr << "WebSocket close error: " << ec.message() << std::endl;
            }
        }

        websocket::stream<beast::tcp_stream> m_ws;
        beast::flat_buffer m_buffer;
    };

    WebSocketServer::WebSocketServer(int port)
        : m_port(port),
        m_ioc(),
        m_acceptor(m_ioc, tcp::endpoint(tcp::v4(), port)),
        m_isRunning(false),
        m_clientCallback(nullptr)
    {
    }

    WebSocketServer::~WebSocketServer() {
        stop();
    }

    bool WebSocketServer::start() {
        if (m_isRunning)
            return true;

        try {
            m_isRunning = true;
            doAccept();

            // Run the io_context on its own thread
            m_thread = std::thread([this]() {
                try {
                    m_ioc.run();
                }
                catch (const std::exception& e) {
                    std::cerr << "WebSocketServer thread exception: " << e.what() << std::endl;
                }
                m_isRunning = false;
                });

            return true;
        }
        catch (const std::exception& e) {
            std::cerr << "WebSocketServer failed to start: " << e.what() << std::endl;
            m_isRunning = false;
            return false;
        }
    }

    void WebSocketServer::stop() {
        if (!m_isRunning)
            return;

        {
            std::lock_guard<std::mutex> lock(m_clientMutex);
            for (auto& client : m_clients) {
                client->close();
            }
            m_clients.clear();
        }

        m_ioc.stop();

        if (m_thread.joinable()) {
            m_thread.join();
        }

        m_isRunning = false;
    }

    bool WebSocketServer::broadcastMessage(const std::string& message) {
        if (!m_isRunning)
            return false;

        auto sharedMsg = std::make_shared<std::string>(message);

        std::lock_guard<std::mutex> lock(m_clientMutex);
        for (auto& client : m_clients) {
            client->send(sharedMsg);
        }

        return true;
    }

    bool WebSocketServer::isRunning() const {
        return m_isRunning;
    }

    void WebSocketServer::setClientConnectCallback(ClientCallback cb) {
        m_clientCallback = cb;
    }

    int WebSocketServer::getConnectedClientCount() const {
        std::lock_guard<std::mutex> lock(m_clientMutex);
        return static_cast<int>(m_clients.size());
    }

    void WebSocketServer::doAccept() {
        // Asynchronously accept a new connection
        m_acceptor.async_accept(
            net::make_strand(m_ioc),
            [this](beast::error_code ec, tcp::socket socket) {
                if (!ec) {
                    // Create and start the session
                    auto session = std::make_shared<WebSocketSession>(std::move(socket));

                    {
                        std::lock_guard<std::mutex> lock(m_clientMutex);
                        m_clients.push_back(session);

                        // Call the callback with the new client count
                        if (m_clientCallback) {
                            m_clientCallback(m_clients.size());
                        }
                    }

                    session->start();
                }
                else {
                    std::cerr << "Accept error: " << ec.message() << std::endl;
                }

                // Accept another connection if still running
                if (m_isRunning) {
                    doAccept();
                }
            });
    }

} // namespace LeagueRecorder

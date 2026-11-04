
#define BOOST_ERROR_CODE_HEADER_ONLY

#include <iostream>
#include <string>
#include <memory>
#include <set>
#include <deque>
#include <utility>

#include <boost/asio.hpp>

#ifdef _WIN32
#pragma comment(lib, "Ws2_32.lib")
#pragma comment(lib, "Mswsock.lib")
#endif

using boost::asio::ip::tcp;

// 전방 선언
class ChatServer;

// =================
// 1. 클라이언트 세션
// =================

class Session : public std::enable_shared_from_this<Session>
{
private:
    tcp::socket socket_;
    ChatServer& server_;

    boost::asio::streambuf read_buffer_;
    std::deque<std::string> write_queue_;

    bool closed_ = false;

public:
    Session(tcp::socket socket, ChatServer& server)
        : socket_(std::move(socket)),
        server_(server)
    {
    }

    void start();

    void deliver(const std::string& message);

    void close();

private:
    void do_read();

    void do_write();
};


// ===================
// 2. 채팅 서버
// ===================

class ChatServer
{
private:
    boost::asio::io_context& io_context_;
    tcp::acceptor acceptor_;

    std::set<std::shared_ptr<Session>> sessions_;

public:
    ChatServer(
        boost::asio::io_context& io_context,
        unsigned short port
    )
        : io_context_(io_context),
        acceptor_(
            io_context,
            tcp::endpoint(tcp::v4(), port)
        )
    {
    }

    void start()
    {
        std::cout << "Server started on port "
            << acceptor_.local_endpoint().port()
            << "\n";

        do_accept();
    }

    void broadcast(const std::string& message)
    {
        for (auto& session : sessions_)
        {
            session->deliver(message);
        }
    }

    void remove_session(const std::shared_ptr<Session>& session)
    {
        sessions_.erase(session);
    }

private:
    void do_accept()
    {
        acceptor_.async_accept(
            [this](boost::system::error_code ec, tcp::socket socket)
            {
                if (!ec)
                {
                    auto session = std::make_shared<Session>(
                        std::move(socket),
                        *this
                    );

                    sessions_.insert(session);

                    session->start();

                    std::cout << "Client connected. Current clients: "
                        << sessions_.size() << "\n";

                    broadcast("[Server] A client joined.\n");
                }
                else
                {
                    std::cerr << "Accept error: "
                        << ec.message() << "\n";
                }

                // 다음 클라이언트 접속 대기
                do_accept();
            }
        );
    }
};


// ===================
// 3. Session 구현
// ===================

void Session::start()
{
    do_read();
}


void Session::do_read()
{
    auto self = shared_from_this();

    boost::asio::async_read_until(
        socket_,
        read_buffer_,
        "\n",
        [this, self](boost::system::error_code ec, std::size_t length)
        {
            if (ec)
            {
                close();
                return;
            }

            std::istream stream(&read_buffer_);

            std::string message;
            std::getline(stream, message);

            // Windows의 CRLF 처리
            if (!message.empty() && message.back() == '\r')
            {
                message.pop_back();
            }

            if (!message.empty())
            {
                std::cout << "[Received] " << message << "\n";

                message += "\n";

                // 모든 클라이언트에게 메시지 전달
                server_.broadcast(message);
            }

            // 다음 메시지 수신 대기
            do_read();
        }
    );
}


void Session::deliver(const std::string& message)
{
    if (closed_)
        return;

    bool writing = !write_queue_.empty();

    write_queue_.push_back(message);

    // 이미 전송 중이면 큐에만 저장
    if (!writing)
    {
        do_write();
    }
}


void Session::do_write()
{
    auto self = shared_from_this();

    boost::asio::async_write(
        socket_,
        boost::asio::buffer(write_queue_.front()),

        [this, self](boost::system::error_code ec, std::size_t length)
        {
            if (ec)
            {
                close();
                return;
            }

            write_queue_.pop_front();

            if (!write_queue_.empty())
            {
                do_write();
            }
        }
    );
}


void Session::close()
{
    if (closed_)
        return;

    closed_ = true;

    boost::system::error_code ec;

    socket_.shutdown(tcp::socket::shutdown_both, ec);
    socket_.close(ec);

    server_.remove_session(shared_from_this());

    std::cout << "Client disconnected. Current clients: "
        << "session removed\n";

    server_.broadcast("[Server] A client left.\n");
}


// ===================
// 4. 프로그램 시작점
// ===================

int main()
{
    try
    {
        boost::asio::io_context io_context;

        ChatServer server(io_context, 9000);

        server.start();

        io_context.run();
    }
    catch (const std::exception& e)
    {
        std::cerr << "Exception: " << e.what() << "\n";
    }

    return 0;
}


#define BOOST_ERROR_CODE_HEADER_ONLY

#include <iostream>
#include <string>
#include <thread>
#include <deque>
#include <mutex>

#include <boost/asio.hpp>

#ifdef _WIN32
#pragma comment(lib, "Ws2_32.lib")
#pragma comment(lib, "Mswsock.lib")
#endif

using boost::asio::ip::tcp;

// ===================
// 채팅 클라이언트
// ===================

class ChatClient
{
private:
    boost::asio::io_context& io_context_;
    tcp::socket socket_;

    boost::asio::streambuf read_buffer_;

    std::deque<std::string> write_queue_;
    std::mutex write_mutex_;

    bool closed_ = false;

public:
    ChatClient(
        boost::asio::io_context& io_context,
        const tcp::resolver::results_type& endpoints
    )
        : io_context_(io_context),
        socket_(io_context)
    {
        do_connect(endpoints);
    }

    // 사용자 입력 스레드에서 호출됨
    void send_message(const std::string& text)
    {
        // io_context 스레드에서 안전하게 큐를 다루도록 post
        boost::asio::post(
            io_context_,
            [this, text]()
            {
                bool writing = !write_queue_.empty();

                write_queue_.push_back(text);

                if (!writing)
                {
                    do_write();
                }
            }
        );
    }

    void close()
    {
        boost::asio::post(
            io_context_,
            [this]()
            {
                if (closed_)
                    return;

                closed_ = true;

                boost::system::error_code ec;
                socket_.shutdown(tcp::socket::shutdown_both, ec);
                socket_.close(ec);
            }
        );
    }

private:
    void do_connect(const tcp::resolver::results_type& endpoints)
    {
        boost::asio::async_connect(
            socket_,
            endpoints,
            [this](boost::system::error_code ec, const tcp::endpoint& endpoint)
            {
                if (!ec)
                {
                    std::cout << "Connected to "
                        << endpoint << "\n";
                    std::cout << "Type a message and press Enter. "
                        << "Type /quit to exit.\n";

                    do_read();
                }
                else
                {
                    std::cerr << "Connect error: "
                        << ec.message() << "\n";
                }
            }
        );
    }

    void do_read()
    {
        boost::asio::async_read_until(
            socket_,
            read_buffer_,
            "\n",
            [this](boost::system::error_code ec, std::size_t length)
            {
                if (ec)
                {
                    if (!closed_)
                    {
                        std::cout << "\nDisconnected from server: "
                            << ec.message() << "\n";
                    }
                    close();
                    return;
                }

                std::istream stream(&read_buffer_);

                std::string line;
                std::getline(stream, line);

                // Windows의 CRLF 처리
                if (!line.empty() && line.back() == '\r')
                {
                    line.pop_back();
                }

                std::cout << line << "\n";

                // 다음 메시지 수신 대기
                do_read();
            }
        );
    }

    void do_write()
    {
        boost::asio::async_write(
            socket_,
            boost::asio::buffer(write_queue_.front()),
            [this](boost::system::error_code ec, std::size_t length)
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
};


// ===================
// 프로그램 시작점
// ===================

int main(int argc, char* argv[])
{
    try
    {
        // 기본값: 로컬 서버, 포트 9000
        std::string host = "127.0.0.1";
        std::string port = "9000";

        if (argc >= 2)
            host = argv[1];
        if (argc >= 3)
            port = argv[2];

        boost::asio::io_context io_context;

        tcp::resolver resolver(io_context);
        auto endpoints = resolver.resolve(host, port);

        ChatClient client(io_context, endpoints);

        // io_context를 별도 스레드에서 실행 (네트워크 송수신 처리)
        std::thread io_thread(
            [&io_context]()
            {
                io_context.run();
            }
        );

        // 메인 스레드에서 사용자 입력 처리
        std::string line;
        while (std::getline(std::cin, line))
        {
            if (line == "/quit")
            {
                break;
            }

            // 서버 프로토콜에 맞춰 개행 문자 추가
            client.send_message(line + "\n");
        }

        client.close();
        io_thread.join();
    }
    catch (const std::exception& e)
    {
        std::cerr << "Exception: " << e.what() << "\n";
    }

    return 0;
}

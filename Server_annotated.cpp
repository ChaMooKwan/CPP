// ============================================================================
//  Server_annotated.cpp
//  원본 Server.cpp 에 "객체지향(OOP) 관점" 설명 주석을 붙인 학습용 버전.
//  로직은 원본과 100% 동일하며, 주석만 추가했습니다.
//
//  [알고리즘 문제 풀던 사람을 위한 큰 그림]
//  알고리즘 문제는 보통 main() 하나에 함수 몇 개를 두고 "데이터 -> 처리 -> 출력"
//  순서로 흘러갑니다. 반면 이 코드는 "역할을 가진 객체들이 서로 협력"하는 구조입니다.
//    - ChatServer : 접속을 받아들이고, 전체 세션을 관리하고, 메시지를 전체에 뿌림
//    - Session    : "서버에 붙은 클라이언트 1명"을 나타내는 객체. 읽기/쓰기를 담당
//  객체가 서로를 참조(ChatServer <-> Session)하면서 협력하는 게 핵심입니다.
// ============================================================================

#define BOOST_ERROR_CODE_HEADER_ONLY

#include <iostream>
#include <string>
#include <memory>     // std::shared_ptr, std::make_shared, enable_shared_from_this
#include <set>
#include <deque>
#include <utility>    // std::move

#include <boost/asio.hpp>

#ifdef _WIN32
#pragma comment(lib, "Ws2_32.lib")
#pragma comment(lib, "Mswsock.lib")
#endif

using boost::asio::ip::tcp;

// 전방 선언(forward declaration):
// Session 클래스가 ChatServer 를 "참조"로 알고 있어야 하고(아래에서 ChatServer&),
// ChatServer 는 Session 을 담고 있어야 합니다. 둘이 서로를 참조하므로,
// 먼저 "ChatServer 라는 클래스가 있다"고 이름만 알려주는 것입니다.
// (알고리즘에서 함수 쓰기 전에 프로토타입 선언하는 것과 같은 개념)
class ChatServer;


// ============================================================================
// 1. 클라이언트 세션 (Session)
//
//  [OOP 개념: 클래스 = 데이터 + 그 데이터를 다루는 함수(메서드)를 하나로 묶은 것]
//  여기서 Session 은 "접속한 클라이언트 1명"을 표현하는 객체입니다.
//  - 가지고 있는 데이터: 소켓, 읽기 버퍼, 보낼 메시지 큐, 종료 여부
//  - 할 수 있는 행동(메서드): 시작(start), 메시지 전달(deliver), 종료(close) ...
//
//  [OOP 개념: 상속(inheritance)]
//  "class Session : public std::enable_shared_from_this<Session>"
//  Session 은 enable_shared_from_this<Session> 을 "상속"받습니다.
//  상속이란 "부모가 가진 기능을 물려받는 것"입니다.
//  여기서는 다형성을 위한 상속이 아니라, shared_from_this() 라는 기능을
//  얻기 위한 상속입니다(자세한 설명은 아래 do_read 주석 참고).
// ============================================================================

class Session : public std::enable_shared_from_this<Session>
{
private:
    // [OOP 개념: 캡슐화(encapsulation)]
    // private 아래의 멤버들은 "클래스 바깥에서 직접 건드릴 수 없는" 내부 상태입니다.
    // 외부는 아래 public 메서드(start/deliver/close)를 통해서만 이 객체를 다룹니다.
    // = 내부 구현을 숨기고(hiding), 정해진 통로로만 쓰게 강제하는 것.

    tcp::socket socket_;          // 이 클라이언트와 연결된 TCP 소켓
    ChatServer& server_;          // 나를 관리하는 서버 (참조. 복사본이 아니라 '그 서버 자체')

    boost::asio::streambuf read_buffer_;     // 수신 데이터를 담아두는 버퍼
    std::deque<std::string> write_queue_;    // 아직 못 보낸 메시지들을 줄 세워두는 큐

    bool closed_ = false;         // 이미 종료됐는지 표시 (중복 종료 방지용)

public:
    // [OOP 개념: 생성자(constructor)]
    // 객체가 "태어날 때" 자동으로 호출되어 멤버를 초기화하는 특수 함수.
    // 클래스 이름과 똑같은 이름을 가지고, 반환 타입이 없습니다.
    //
    // ": socket_(std::move(socket)), server_(server)" 부분은
    // '멤버 초기화 리스트'로, 멤버 변수들을 초기화합니다.
    //  - std::move(socket): 소켓은 복사가 안 되는 자원이라 '소유권을 넘겨' 받습니다.
    //  - server_(server)  : 참조 멤버라서 반드시 여기(초기화 리스트)에서 묶어줘야 함.
    // 뒤의 { } 는 '생성자 본문'입니다. 추가로 할 일이 없어서 비어 있습니다.
    Session(tcp::socket socket, ChatServer& server)
        : socket_(std::move(socket)),
        server_(server)
    {
    }

    // 아래 3개는 '선언(declaration)'만 되어 있습니다. 실제 본문(구현)은
    // 파일 아래쪽 "3. Session 구현" 부분에 Session::start() 처럼 따로 있습니다.
    // (클래스 선언을 깔끔하게 보여주고, 구현은 분리하는 흔한 스타일)

    void start();                               // 이 세션 동작 시작 (읽기 루프 시작)

    void deliver(const std::string& message);   // 이 클라이언트에게 메시지 1건을 보냄

    void close();                                // 이 세션 종료 및 정리

private:
    void do_read();     // 비동기로 한 줄 읽기
    void do_write();    // 큐에 쌓인 메시지를 비동기로 전송
};


// ============================================================================
// 2. 채팅 서버 (ChatServer)
//
//  [OOP 개념: 또 하나의 클래스 = 또 하나의 역할]
//  ChatServer 는 "전체 채팅방"을 관리하는 객체입니다.
//  - 새 접속을 받아들이고(do_accept)
//  - 모든 Session 을 모아두고(sessions_)
//  - 누군가의 메시지를 전원에게 뿌립니다(broadcast)
//
//  [Session 과의 관계: 연관(association) / 양방향 참조]
//  ChatServer 는 Session 들을 담고(set), 각 Session 은 server_ 로 ChatServer 를
//  되돌아봅니다. 객체들이 서로를 알고 협력하는 전형적인 OOP 구조입니다.
// ============================================================================

class ChatServer
{
private:
    boost::asio::io_context& io_context_;    // Asio의 이벤트 처리 엔진(참조)
    tcp::acceptor acceptor_;                 // 들어오는 접속을 받아주는 객체

    // 접속 중인 모든 세션의 집합.
    // shared_ptr 로 담는 이유: 세션의 수명을 '공유 소유'로 관리하기 위함.
    // (누구든 이 세션을 참조하는 동안엔 메모리가 살아있음)
    std::set<std::shared_ptr<Session>> sessions_;

public:
    // 생성자: 서버가 생성될 때 acceptor_ 를 '이 포트에서 들을 준비' 상태로 초기화.
    // acceptor_(io_context, tcp::endpoint(tcp::v4(), port)) 가 그 초기화입니다.
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

    // [OOP 개념: public 메서드 = 객체에게 '시켜도 되는 일']
    // 외부(main)는 server.start() 처럼 이 메서드만 호출합니다. 내부 do_accept 등은 모름.
    void start()
    {
        std::cout << "Server started on port "
            << acceptor_.local_endpoint().port()
            << "\n";

        do_accept();
    }

    // 모든 세션에게 같은 메시지를 전달.
    // sessions_(여러 Session 객체)를 돌며 각 객체의 deliver()를 호출합니다.
    // = "객체들에게 메시지를 보내 행동을 시키는" 객체지향적 협력.
    void broadcast(const std::string& message)
    {
        for (auto& session : sessions_)
        {
            session->deliver(message);
        }
    }

    // 끊긴 세션을 집합에서 제거. (Session::close() 안에서 서버에게 요청함)
    void remove_session(const std::shared_ptr<Session>& session)
    {
        sessions_.erase(session);
    }

private:
    // 새 클라이언트 접속을 '비동기로' 기다리는 함수.
    void do_accept()
    {
        acceptor_.async_accept(
            // [OOP/C++ 개념: 람다(lambda) = 이름 없는 함수 객체]
            // async_accept 는 "접속이 들어오면 이 함수를 실행해줘" 라며 콜백을 받습니다.
            // [this] 는 '이 ChatServer 객체'를 람다 안에서 쓰겠다는 캡처입니다.
            [this](boost::system::error_code ec, tcp::socket socket)
            {
                if (!ec)
                {
                    // [OOP 개념: 객체 생성]
                    // make_shared<Session>(...) 로 새 Session 객체를 '동적 생성'.
                    // 접속한 소켓과, 자기 자신(*this = 이 서버)을 넘겨줍니다.
                    // => 새로 태어난 Session 은 server_ 로 이 서버를 알게 됩니다.
                    auto session = std::make_shared<Session>(
                        std::move(socket),
                        *this
                    );

                    sessions_.insert(session);   // 관리 집합에 등록

                    session->start();            // 세션에게 "동작 시작해" 라고 지시

                    std::cout << "Client connected. Current clients: "
                        << sessions_.size() << "\n";

                    broadcast("[Server] A client joined.\n");
                }
                else
                {
                    std::cerr << "Accept error: "
                        << ec.message() << "\n";
                }

                // 다음 클라이언트 접속을 또 기다림(재귀적으로 수락 루프 유지)
                do_accept();
            }
        );
    }
};


// ============================================================================
// 3. Session 구현
//
//  위 클래스 선언에서 '선언만' 했던 메서드들의 실제 본문입니다.
//  "Session::start" 에서 Session:: 은 "이 함수는 Session 클래스 소속"이라는 표시입니다.
//  (클래스 바깥에 멤버 함수 구현을 적을 때 쓰는 문법 = 범위 지정 연산자 '::')
// ============================================================================

void Session::start()
{
    do_read();
}


void Session::do_read()
{
    // [아주 중요한 OOP/수명 관리 개념: shared_from_this()]
    // 이 함수는 enable_shared_from_this 를 상속했기 때문에 쓸 수 있습니다.
    // self 는 "나 자신을 가리키는 shared_ptr" 입니다.
    // 왜 필요?: 아래 async_read_until 은 '나중에' 콜백을 실행합니다.
    // 그 사이에 이 Session 객체가 사라지면 콜백에서 죽은 객체를 건드려 크래시가 납니다.
    // self 를 람다에 캡처해두면, 콜백이 끝날 때까지 이 객체가 '살아있도록' 보장됩니다.
    auto self = shared_from_this();

    boost::asio::async_read_until(
        socket_,
        read_buffer_,
        "\n",     // 개행 문자가 나올 때까지 한 줄을 읽음 (= 메시지 구분자)
        // [this, self] : 이 객체의 멤버를 쓰려고 this, 수명 보장을 위해 self 를 캡처
        [this, self](boost::system::error_code ec, std::size_t length)
        {
            if (ec)
            {
                close();     // 읽기 실패(보통 접속 끊김) => 세션 종료
                return;
            }

            std::istream stream(&read_buffer_);

            std::string message;
            std::getline(stream, message);

            // Windows의 CRLF("\r\n") 처리: 끝의 '\r' 제거
            if (!message.empty() && message.back() == '\r')
            {
                message.pop_back();
            }

            if (!message.empty())
            {
                std::cout << "[Received] " << message << "\n";

                message += "\n";

                // 받은 메시지를 '서버'에게 넘겨 전체에 뿌리게 함.
                // Session 이 직접 다른 세션들을 모르고, 서버를 통해 협력하는 설계.
                server_.broadcast(message);
            }

            // 다음 메시지를 또 읽음 (읽기 루프 유지)
            do_read();
        }
    );
}


// deliver: "이 클라이언트에게 메시지 1건 보내줘" 라는 요청 처리.
void Session::deliver(const std::string& message)
{
    if (closed_)
        return;

    // write_queue_ 가 비어있지 않으면 = 지금 전송 중이라는 뜻.
    bool writing = !write_queue_.empty();

    write_queue_.push_back(message);   // 보낼 메시지를 큐 뒤에 추가

    // 전송 중이 아니었다면 지금 전송 시작. (전송 중이면 큐에만 넣고 끝)
    // => 비동기 전송이 꼬이지 않게 "한 번에 하나씩" 보내는 패턴.
    if (!writing)
    {
        do_write();
    }
}


void Session::do_write()
{
    auto self = shared_from_this();   // do_read 와 같은 이유로 수명 보장

    boost::asio::async_write(
        socket_,
        boost::asio::buffer(write_queue_.front()),   // 큐의 맨 앞 메시지를 전송

        [this, self](boost::system::error_code ec, std::size_t length)
        {
            if (ec)
            {
                close();     // 전송 실패 => 종료
                return;
            }

            write_queue_.pop_front();     // 방금 보낸 메시지를 큐에서 제거

            // 큐에 아직 남은 게 있으면 이어서 다음 것 전송
            if (!write_queue_.empty())
            {
                do_write();
            }
        }
    );
}


void Session::close()
{
    // closed_ 플래그로 '한 번만' 정리되도록 보장 (중복 호출 방어 = 방어적 프로그래밍)
    if (closed_)
        return;

    closed_ = true;

    boost::system::error_code ec;

    socket_.shutdown(tcp::socket::shutdown_both, ec);   // 송수신 중단
    socket_.close(ec);                                  // 소켓 닫기

    // 서버에게 "나를 목록에서 빼줘" 라고 요청.
    // shared_from_this() 로 '나 자신'을 가리키는 포인터를 넘깁니다.
    server_.remove_session(shared_from_this());

    std::cout << "Client disconnected. Current clients: "
        << "session removed\n";

    server_.broadcast("[Server] A client left.\n");
}


// ============================================================================
// 4. 프로그램 시작점
//
//  알고리즘 문제의 main 과 비슷해 보이지만, 하는 일은
//  "객체들을 만들고(server), 동작을 시작시키고(start), 이벤트 루프를 돌리는 것(run)" 입니다.
// ============================================================================

int main()
{
    try
    {
        boost::asio::io_context io_context;    // 모든 비동기 작업을 굴리는 엔진

        ChatServer server(io_context, 9000);   // 서버 '객체' 생성 (포트 9000)

        server.start();                        // 접속 받기 시작

        io_context.run();   // 이벤트 루프: 비동기 작업/콜백들이 여기서 실제로 돌아감.
                            // (접속이 들어오고 메시지가 오갈 때마다 등록된 콜백이 실행됨)
    }
    catch (const std::exception& e)
    {
        std::cerr << "Exception: " << e.what() << "\n";
    }

    return 0;
}


// ============================================================================
//  [부록] 오버로딩 / 오버라이딩 / 다형성 — 이 코드에서는 어디에 있나?
//
//  질문해 주신 세 가지를 '정직하게' 짚습니다. 억지로 끼워 맞추지 않았습니다.
//
//  (1) 오버로딩(Overloading) = "같은 이름, 다른 매개변수"의 함수를 여러 개 두는 것
//      예) void f(int);  void f(double);  void f(int, int);  <- 이름 같고 인자 다름
//      -> 이 코드에는 '우리가 직접 만든' 오버로딩은 없습니다.
//         (단, 생성자 ChatServer(...) 와 Session(...) 처럼 생성자를 정의하는 것,
//          그리고 << 연산자로 여러 타입을 출력하는 std::cout << ... 는
//          라이브러리가 '연산자 오버로딩'으로 구현해 둔 것을 쓰는 사례입니다.)
//
//  (2) 오버라이딩(Overriding) = 부모의 'virtual(가상) 함수'를 자식이 '재정의'하는 것
//      -> 이 코드에는 오버라이딩이 없습니다.
//         Session 이 enable_shared_from_this 를 상속하긴 하지만,
//         부모의 가상 함수를 재정의(override)하지는 않습니다.
//         이 상속은 'is-a(~의 일종)' 관계나 다형성 목적이 아니라,
//         shared_from_this() 기능을 얻기 위한 '구현 상속'입니다.
//
//  (3) 다형성(Polymorphism) = "같은 호출인데 실제 타입에 따라 다르게 동작"
//      대표적으로 '부모 포인터로 자식 객체를 가리키고 virtual 함수를 호출' 하는 것.
//      -> 이 코드에는 런타임 다형성(virtual 기반)이 없습니다.
//         (참고: 템플릿/람다 같은 '컴파일타임 다형성' 요소는 간접적으로 쓰이지만,
//          질문하신 전형적인 상속 기반 다형성은 등장하지 않습니다.)
//
//  요약: 이 코드가 보여주는 핵심 OOP는 '다형성'보다는
//        - 캡슐화(private 멤버 + public 메서드)
//        - 책임 분리(ChatServer 는 관리, Session 은 개별 통신)
//        - 객체 간 협력(서로 참조하며 메서드 호출)
//        - 객체 수명 관리(shared_ptr + shared_from_this)
//        입니다.
//
//  >> 만약 '오버라이딩/다형성'을 꼭 연습해보고 싶다면,
//     예를 들어 "메시지를 가공하는 방식"을 추상 클래스로 만들고
//     (예: class MessageFormatter { virtual std::string format(...)=0; }),
//     PlainFormatter / TimestampFormatter 같은 자식들이 그걸 각각 override 하게
//     바꾸면, 같은 format() 호출이 타입에 따라 다르게 동작하는 '진짜 다형성' 예제가 됩니다.
//     원하시면 그렇게 리팩터링한 버전을 만들어 드릴게요.
// ============================================================================

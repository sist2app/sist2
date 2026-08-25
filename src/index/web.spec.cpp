#include <gtest/gtest.h>

#include <chrono>
#include <string>
#include <thread>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

extern "C" {
#include "src/index/web.h"
}

/*
 * An Elasticsearch that accepts the connection and then stops answering used to hang the index
 * step forever: not one of the requests carried a timeout of any kind, so the last thing in the
 * log was "Indexed 1000 documents" and the task never ended.
 */

#ifdef _WIN32
#define close_socket closesocket
#define SHUT_RDWR SD_BOTH
using socket_t = SOCKET;
static constexpr socket_t invalid_socket = INVALID_SOCKET;
#else
#define close_socket close
using socket_t = int;
static constexpr socket_t invalid_socket = -1;
#endif

/** Accepts one connection and holds it open without ever writing a response */
class SilentServer {
public:
    SilentServer() {
#ifdef _WIN32
        // libcurl has already started Winsock by the time a test runs, but the fixture must not
        // depend on that ordering
        WSADATA wsa_data;
        WSAStartup(MAKEWORD(2, 2), &wsa_data);
#endif
        listen_fd = socket(AF_INET, SOCK_STREAM, 0);
        EXPECT_NE(listen_fd, invalid_socket);

        int reuse = 1;
        setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, (const char *) &reuse, sizeof(reuse));

        sockaddr_in addr = {};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = 0;

        EXPECT_EQ(bind(listen_fd, (sockaddr *) &addr, sizeof(addr)), 0);
        EXPECT_EQ(listen(listen_fd, 1), 0);

        socklen_t len = sizeof(addr);
        EXPECT_EQ(getsockname(listen_fd, (sockaddr *) &addr, &len), 0);
        port = ntohs(addr.sin_port);

        accept_thread = std::thread([this]() {
            accepted_fd = accept(listen_fd, nullptr, nullptr);
        });
    }

    ~SilentServer() {
        shutdown(listen_fd, SHUT_RDWR);
#ifdef _WIN32
        // Windows leaves a blocking accept() parked on a shut-down listener, so the socket is
        // closed to release it. Elsewhere the descriptor is closed after the join: closing one a
        // thread is blocked on is undefined, and the number can be handed to another socket.
        close_socket(listen_fd);
        accept_thread.join();
#else
        accept_thread.join();
        close_socket(listen_fd);
#endif
        if (accepted_fd != invalid_socket) {
            close_socket(accepted_fd);
        }
    }

    std::string url() const {
        return "http://127.0.0.1:" + std::to_string(port) + "/_bulk";
    }

private:
    socket_t listen_fd = invalid_socket;
    socket_t accepted_fd = invalid_socket;
    int port = 0;
    std::thread accept_thread;
};

class WebTimeoutTest : public ::testing::Test {
protected:
    void SetUp() override {
        web_set_timeouts(1, 1);
    }

    void TearDown() override {
        web_set_timeouts(WEB_CONNECT_TIMEOUT, WEB_STALL_TIMEOUT);
        web_thread_cleanup();
    }
};

TEST_F(WebTimeoutTest, PostToAServerThatNeverAnswersGivesUp) {
    SilentServer server;

    auto start = std::chrono::steady_clock::now();
    response_t *resp = web_post(server.url().c_str(), "{}\n", FALSE);
    auto elapsed = std::chrono::steady_clock::now() - start;

    EXPECT_EQ(resp->status_code, 0);
    EXPECT_LT(std::chrono::duration_cast<std::chrono::seconds>(elapsed).count(), 10);

    free_response(resp);
}

TEST_F(WebTimeoutTest, PutToAServerThatNeverAnswersGivesUp) {
    SilentServer server;

    auto start = std::chrono::steady_clock::now();
    response_t *resp = web_put(server.url().c_str(), "{}", FALSE);
    auto elapsed = std::chrono::steady_clock::now() - start;

    EXPECT_EQ(resp->status_code, 0);
    EXPECT_LT(std::chrono::duration_cast<std::chrono::seconds>(elapsed).count(), 10);

    free_response(resp);
}

/** A port with nothing behind it must not hold the connect open either */
TEST_F(WebTimeoutTest, PostToAClosedPortGivesUp) {
    auto start = std::chrono::steady_clock::now();
    // 198.51.100.0/24 is reserved for documentation: nothing routes there
    response_t *resp = web_post("http://198.51.100.1:9200/_bulk", "{}\n", FALSE);
    auto elapsed = std::chrono::steady_clock::now() - start;

    EXPECT_EQ(resp->status_code, 0);
    EXPECT_LT(std::chrono::duration_cast<std::chrono::seconds>(elapsed).count(), 10);

    free_response(resp);
}

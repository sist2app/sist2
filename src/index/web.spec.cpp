#include <gtest/gtest.h>

#include <chrono>
#include <string>
#include <thread>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>

extern "C" {
#include "src/index/web.h"
}

/*
 * An Elasticsearch that accepts the connection and then stops answering used to hang the index
 * step forever: not one of the requests carried a timeout of any kind, so the last thing in the
 * log was "Indexed 1000 documents" and the task never ended.
 */

/** Accepts one connection and holds it open without ever writing a response */
class SilentServer {
public:
    SilentServer() {
        listen_fd = socket(AF_INET, SOCK_STREAM, 0);
        EXPECT_NE(listen_fd, -1);

        int reuse = 1;
        setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

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
        accept_thread.join();
        if (accepted_fd != -1) {
            close(accepted_fd);
        }
        close(listen_fd);
    }

    std::string url() const {
        return "http://127.0.0.1:" + std::to_string(port) + "/_bulk";
    }

private:
    int listen_fd = -1;
    int accepted_fd = -1;
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

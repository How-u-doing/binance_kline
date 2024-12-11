#include "shm_bbuffer_spmc.h"
#include "kline_common.h"
#include "websocketpp/config/asio_client.hpp"
#include "websocketpp/client.hpp"

#include <iostream>
#include <algorithm>
#include <csignal>

enum { MAX_KLINE_MSG_SIZE = 400 };

struct KlineData {
    char msg[MAX_KLINE_MSG_SIZE];
};

typedef websocketpp::client<websocketpp::config::asio_tls_client> WebSocketClient;

class BinanceKlineClient {
public:
    BinanceKlineClient(ShmCircularBuffer<KlineData, /* IsProducer: */ true> &shm_bbuffer)
        : shm_bbuffer_(shm_bbuffer) {
        wsclient_.init_asio();
        wsclient_.set_tls_init_handler([](websocketpp::connection_hdl hdl) {
            (void)hdl;
            return std::make_shared<websocketpp::lib::asio::ssl::context>(
                websocketpp::lib::asio::ssl::context::tlsv12_client);
        });
        wsclient_.set_message_handler(
            [this](websocketpp::connection_hdl hdl, WebSocketClient::message_ptr msg) {
                on_message(hdl, msg);
            });
        wsclient_.set_open_handler([](websocketpp::connection_hdl hdl) {
            (void)hdl;
            std::cout << "Connection established.\n";
        });
        wsclient_.set_fail_handler([](websocketpp::connection_hdl hdl) {
            (void)hdl;
            std::cerr << "Connection failed.\n";
        });
        wsclient_.set_close_handler([](websocketpp::connection_hdl hdl) {
            (void)hdl;
            std::cout << "Connection closed.\n";
        });
    }

    int connect(const std::string &uri) {
        websocketpp::lib::error_code ec;
        auto con = wsclient_.get_connection(uri, ec);
        if (ec) {
            std::cerr << "Error creating connection: " << ec.message() << "\n";
            return -1;
        }
        wsclient_.connect(con);
        return 0;
    }

    void run() { wsclient_.run(); }

private:
    void on_message(websocketpp::connection_hdl hdl, WebSocketClient::message_ptr msg) {
        (void)hdl;
        std::string_view message = msg->get_payload();
        KlineData kline_data;
        strncpy(kline_data.msg, message.data(), MAX_KLINE_MSG_SIZE - 1);
        kline_data.msg[MAX_KLINE_MSG_SIZE - 1] = '\0';
        shm_bbuffer_.produce(kline_data);
        std::cout << "Message received: " << message << "\n";
        print_kline_data(message);
    }

    ShmCircularBuffer<KlineData, /* IsProducer: */ true> &shm_bbuffer_;
    WebSocketClient wsclient_;
};

void run_producer(const char *shm_name, idx_t capacity) {
    ShmCircularBuffer<KlineData, /* IsProducer: */ true> shm_bbuffer(shm_name, capacity);
    BinanceKlineClient client(shm_bbuffer);

    const std::string uri = "wss://stream.binance.com:9443/ws/btcusdt@kline_1m";
    // or retry n times
    if (client.connect(uri) == -1)
        exit(EXIT_FAILURE);
    client.run();
}

void run_consumer(const char *shm_name) {
    ShmCircularBuffer<KlineData, /* IsProducer: */ false> shm_bbuffer(shm_name);

    KlineData item;
    while (true) {
        shm_bbuffer.consume(&item);
        std::cout << "Message consumed: " << item.msg << "\n";
        print_kline_data(item.msg);
    }
}

void print_usage_and_exit(const char *app) {
    std::cerr << "Usage: \n"
              << app << " producer /shm_name capacity\n"
              << app << " consumer /shm_name\n";
    exit(EXIT_FAILURE);
}

int main(int argc, const char *argv[]) {
    const char *app = argv[0];
    if (argc == 1)
        print_usage_and_exit(app);
    const std::string app_kind = argv[1];
    if (app_kind == "producer") {
        if (argc != 4)
            print_usage_and_exit(app);
        idx_t capacity = std::stod(argv[3]);
        if (capacity <= 0) {
            std::cerr << "invalid capacity\n";
            exit(EXIT_FAILURE);
        }
        const char *shm_name = argv[2];
        run_producer(shm_name, capacity);
    } else if (app_kind == "consumer") {
        if (argc != 3)
            print_usage_and_exit(app);
        const char *shm_name = argv[2];
        run_consumer(shm_name);
    } else {
        print_usage_and_exit(app);
    }

    return 0;
}

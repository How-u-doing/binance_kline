#include "kline_common.h"
#include "websocketpp/config/asio_client.hpp"
#include "websocketpp/client.hpp"

#include <iostream>
#include <string>
#include <string_view>

typedef websocketpp::client<websocketpp::config::asio_tls_client> WebSocketClient;

class BinanceKlineClient {
public:
    BinanceKlineClient() {
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
        std::cout << "Message received: " << message << "\n";
        print_kline_data(message);
    }

    WebSocketClient wsclient_;
};

int main() {
    const std::string uri = "wss://stream.binance.com:9443/ws/btcusdt@kline_1m";

    BinanceKlineClient client;
    // or retry n times
    if (client.connect(uri) == -1)
        return -1;
    client.run();

    return 0;
}

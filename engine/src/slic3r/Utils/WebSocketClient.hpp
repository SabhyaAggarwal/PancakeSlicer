#ifndef _WEB_SOCKET_CLIENT_HPP_
#define _WEB_SOCKET_CLIENT_HPP_
#include <boost/asio/buffer.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http/field.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/core/buffers_to_string.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/websocket/rfc6455.hpp>
#include <exception>
#include <boost/beast/websocket/stream_base.hpp>
#include <boost/beast/websocket/stream.hpp>
#include <iostream>
#include <string>
#include <chrono>

class WebSocketClient {
public:
    WebSocketClient():
    resolver_(ioc_), ws_(ioc_),is_connect(false) {

    }

    ~WebSocketClient() {
        if(!is_connect){
            return;
        }
        try {
            ws_.close(boost::beast::websocket::close_code::normal);
        } catch (const std::exception& e) {
            std::cerr << "Error: " << e.what() << std::endl;
        }
    }
    void connect(const std::string& host, const std::string& port, const std::string& path="/"){
        if(is_connect){
           return;
        }
        auto const results = resolver_.resolve(host, port);

        auto ep = boost::asio::connect(ws_.next_layer(), results);
        std::string _host = host;
        if(_host.size()>0&&_host[host.size()-1] == '/'){
            _host[host.size()-1] = '\0';
        }

        ws_.set_option(boost::beast::websocket::stream_base::decorator(
            [](boost::beast::websocket::request_type& req)
            {
                req.set(boost::beast::http::field::user_agent,"ElegooSlicer");
            }));
        ws_.handshake(_host, path);
        is_connect = true;
    }

    void send(const std::string& message){
        ws_.write(boost::asio::buffer(message));
    }

    std::string receive(int timeout = 0){
        boost::beast::flat_buffer buffer;

        ws_.read(buffer);

        return boost::beast::buffers_to_string(buffer.data());
    }


private:
    boost::asio::io_context ioc_;
    boost::asio::ip::tcp::resolver resolver_;
    boost::beast::websocket::stream<boost::asio::ip::tcp::socket> ws_;
    bool is_connect;
};

#endif
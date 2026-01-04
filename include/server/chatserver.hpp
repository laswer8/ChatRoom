#pragma once

#include "HeadFile.h"
#include "chatservice.hpp"
#include "threadpool.hpp"
#include "sslservice.hpp"
#include "kafkahandler.hpp"


#define CERTIFICATE_CHAIN_PATH "./key/server_ca.pem"
// =====================  WebSocket++ Server  =====================

class WebSocketChatRoom {
public:
    // TLS enabled server configuration
    using server         = websocketpp::server<websocketpp::config::asio_tls>;
    using connection_hdl = websocketpp::connection_hdl;
    using message_ptr    = server::message_ptr;

    WebSocketChatRoom() : m_worker_pool(ThreadPoolBuilder::getInstance()->build()) {
        /* 1. 基础 Server 配置 */
        m_server.init_asio();
        m_server.set_reuse_addr(true);
        m_server.clear_access_channels(websocketpp::log::alevel::all);
        m_server.set_error_channels(websocketpp::log::elevel::all);
        m_server.set_access_channels(websocketpp::log::alevel::all ^ websocketpp::log::alevel::frame_payload);

        m_server.set_max_message_size(65536);//设置消息大小限制

        /* 2. TLS / 验证回调 */
        m_server.set_tls_init_handler(websocketpp::lib::bind(&WebSocketChatRoom::on_tls_init, this, _1));
        m_server.set_validate_handler(websocketpp::lib::bind(&WebSocketChatRoom::on_validate, this, _1));

        /* 3. HTTP 回调*/
        m_server.set_http_handler(websocketpp::lib::bind(&WebSocketChatRoom::on_http, this, _1));

        /* 4. 事件回调 */
        m_server.set_open_handler(websocketpp::lib::bind(&WebSocketChatRoom::on_open, this, _1));
        m_server.set_close_handler(websocketpp::lib::bind(&WebSocketChatRoom::on_close, this, _1));
        m_server.set_message_handler(websocketpp::lib::bind(&WebSocketChatRoom::on_message, this, _1, _2));

        this->kafka_consumer = make_shared<KafkaMessageConsumer>();
        if (this->kafka_consumer and this->kafka_consumer->Init()) {
            this->kafka_consumer->setMsgCallBack(bind(&WebSocketChatRoom::on_consumer,this,_1));
            this->kafka_consumer->start_recv({KAFKA_LOCAL_SERVER_ID});
        }
    }

    ~WebSocketChatRoom() { stop(); }

    static shared_ptr<WebSocketChatRoom> GetInstance() {
        static shared_ptr<WebSocketChatRoom> instance = shared_ptr<WebSocketChatRoom>(new WebSocketChatRoom());
        return instance;
    }

    // 启动服务器
    void start(const string& ip,const uint64_t& port) {
        m_server.set_listen_backlog(4096);
        //监听指定地址
        boost::asio::ip::tcp::endpoint ep(boost::asio::ip::address::from_string(ip), port);
        m_server.listen(ep);
        m_server.start_accept();

        // 多线程跑 io_context
        for (size_t i = 0; i < 3; ++i) {
            m_io_threads.emplace_back([this]() {
                try {
                    m_server.run();
                } catch (...) {
                    LOG_ERROR << "ws io thread exit with error";
                }
            });
        }

        // 当前线程也跑一次，阻塞直到 stop()
        m_server.run();
    }
 
    // 停止服务器
    void stop() {
        m_server.stop_listening();
        if (m_worker_pool)
            m_worker_pool->shutdown_strong();
        m_server.stop();
        for (auto &t : m_io_threads) {
            if (t.joinable()) t.join();
        }
    }

private:
    // =====================  WebSocket++ TLS & 验证  =====================
    //TCP 连接升级到 TLS 之前调用
    std::shared_ptr<boost::asio::ssl::context> on_tls_init(connection_hdl hdl) {
        namespace asio = boost::asio;
        //创建 TLS context，指定协议族
        auto ctx = std::make_shared<asio::ssl::context>(asio::ssl::context::tlsv12);
        try {
            ctx->set_options(asio::ssl::context::default_workarounds |  // 兼容老版本 OpenSSL bug
                             asio::ssl::context::no_sslv2 |             // 禁用极不安全的 SSLv2
                             asio::ssl::context::no_sslv3 |
                             asio::ssl::context::no_tlsv1 |
                             asio::ssl::context::no_tlsv1_1 |
                             asio::ssl::context::single_dh_use);        // 每次握手重新生成 DH key，防止重放
            //验证客户端证书
            ctx->set_verify_mode(boost::asio::ssl::verify_peer);
            ctx->load_verify_file(CERTIFICATE_CHAIN_PATH);

            ctx->use_certificate_chain_file(CERTIFICATE_CHAIN_PATH);//CA
            ctx->use_private_key_file(PRIVATE_KEY_PATH, asio::ssl::context::pem);//私钥
        } catch (const std::exception &e) {
            //抛出失败，结束服务器
            LOG_ERROR << "tls init failed: " << e.what();
            //throw std::runtime_error("tls init failed");
        }
        return ctx;
    }

    bool on_validate(connection_hdl hdl) {
        try {
            auto con = m_server.get_con_from_hdl(hdl);
            auto ret = WebSocketService::on_verify(con);
            if (ret == selfdefine::frame::status::token_expire) {
                con->set_status(websocketpp::http::status_code::continue_code);
                con->set_body("expire");
                return false;
            }else if (ret == selfdefine::frame::status::ok) {
                return true;
            }
            return false;
        } catch (const std::exception &e) {
            LOG_ERROR << "validate error: " << e.what();
            return false;
        }
    }

    // =====================  WebSocket++ 回调  =====================
    //建立连接后不能继续使用http响应，而是应该发送ws数据帧
    void on_open(connection_hdl hdl) {
        server::connection_ptr con = m_server.get_con_from_hdl(hdl);
        if (!con || con->get_state() != websocketpp::session::state::open){
            return;
        }
        try{
            LOG_INFO<<"new connection at "<<con->get_remote_endpoint()
                <<" uri is "<<con->get_request().get_uri()
                <<" method: "<<con->get_request().get_method()
                <<" param: "<<con->get_uri()->get_query();
            const string& query = con->get_uri()->get_query();
            std::unordered_map<std::string, std::string> kv;
            std::istringstream ss(query);
            std::string part;
            //参数解析
            while (std::getline(ss, part, '&')) {
                auto pos = part.find('=');
                if (pos != std::string::npos) {
                    kv[part.substr(0, pos)] = part.substr(pos + 1);
                }
            }
            //查找必要字段
            if (!kv.count("uid")) {
                //拒绝握手,设置状态码
                con->close(websocketpp::close::status::extension_required,"miss uid");
                return;
            }
            //保存用户信息
            if (!WebSocketService::GetInstance()->on_open(kv["uid"],hdl)) {
                con->close(websocketpp::close::status::try_again_later,"busy");
            }
        }catch (exception &e) {
            LOG_DEBUG << "on_open failed: " << e.what();
            con->close(websocketpp::close::status::un_valied,e.what());
        }
    }

    void on_close(connection_hdl hdl) {
        server::connection_ptr con = m_server.get_con_from_hdl(hdl);
        if (!con){
            return;
        }
        try{
            LOG_INFO<<"close connection at "<<con->get_remote_endpoint()
                <<" uri is "<<con->get_request().get_uri()
                <<" method: "<<con->get_request().get_method()
                <<" param: "<<con->get_uri()->get_query();
            const string& query = con->get_uri()->get_query();
            std::unordered_map<std::string, std::string> kv;
            std::istringstream ss(query);
            std::string part;
            //参数解析
            while (std::getline(ss, part, '&')) {
                auto pos = part.find('=');
                if (pos != std::string::npos) {
                    kv[part.substr(0, pos)] = part.substr(pos + 1);
                }
            }
            //查找必要字段
            if (!kv.count("uid")) {
                //拒绝握手,设置状态码
                return;
            }
            WebSocketService::GetInstance()->on_remove(kv["uid"]);
        }catch (exception &e) {
            LOG_DEBUG << "on_close failed: " << e.what();
        }
    }

    void on_fail(connection_hdl hdl) {
        server::connection_ptr con = m_server.get_con_from_hdl(hdl);
        if (!con){
            return;
        }
        try{
            LOG_INFO<<"failed connection at "<<con->get_remote_endpoint()
                <<" uri is "<<con->get_request().get_uri()
                <<" method: "<<con->get_request().get_method()
                <<" param: "<<con->get_uri()->get_query();
            const string& query = con->get_uri()->get_query();
            std::unordered_map<std::string, std::string> kv;
            std::istringstream ss(query);
            std::string part;
            //参数解析
            while (std::getline(ss, part, '&')) {
                auto pos = part.find('=');
                if (pos != std::string::npos) {
                    kv[part.substr(0, pos)] = part.substr(pos + 1);
                }
            }
            //查找必要字段
            if (!kv.count("uid")) {
                //拒绝握手,设置状态码
                return;
            }
            WebSocketService::GetInstance()->on_remove(kv["uid"]);
        }catch (exception &e) {
            LOG_DEBUG << "on_close failed: " << e.what();
        }
    }

    // =====================  HTTP 登录  =====================
    void on_http(connection_hdl hdl) {
        try{
            //改为调用业务代码
            server::connection_ptr con = m_server.get_con_from_hdl(hdl);
            if (!con || con->get_state() != websocketpp::session::state::open)return;
            //该请求为wss升级请求，直接返回
            if (con->get_request_header("Upgrade") == "websocket")return;

            std::string method = con->get_request().get_method();
            std::string uri = con->get_request().get_uri();
            if (method != "POST") {
                con->set_status(websocketpp::http::status_code::method_not_allowed);
                con->set_body("Method Not Is POST And API Error");
                con->send_http_response();
                return;
            }
            //延迟处理
            con->defer_http_response();
            std::string body = con->get_request_body();
            auto task = std::make_shared<std::function<void()>>([this, hdl, uri, body]() {
                std::string response_body;
                websocketpp::http::status_code::value status = websocketpp::http::status_code::ok;
                try {
                    json js = json::parse(body);
                    auto handler = WebSocketService::GetInstance()->GetHttpHandler(uri);
                    if (handler) {
                        //执行
                        auto value = handler(m_server,hdl,js);
                        response_body = value.first;
                        status = value.second;
                    }else {
                        status = websocketpp::http::status_code::not_found;
                        response_body = "NOT FOUND";
                    }
                } catch (const std::exception &e) {
                    status = websocketpp::http::status_code::bad_request;
                    response_body = e.what();
                }
                //交由io_context线程调度
                this->m_server.get_io_service().post([this,hdl,status,response_body]() {
                    server::connection_ptr con = this->m_server.get_con_from_hdl(hdl);
                    if (con && con->get_state() == websocketpp::session::state::open) {
                        try {
                            con->set_status(status);
                            con->set_body(response_body);
                            con->send_http_response();
                        }catch (const std::exception &e) {
                            LOG_DEBUG<<"响应错误："<<e.what();
                        }
                    }else {
                        LOG_DEBUG<<"连接已关闭，抛弃连接";
                    }
                });
            });
            m_worker_pool->submit(task);
        }catch (exception& e) {
            LOG_ERROR << "https handler error: " << e.what();
        }
    }

    // =====================  WebSocket 消息  =====================
    void on_message(connection_hdl hdl, message_ptr msg) {
        try{
            // 将业务处理放到线程池
            server::connection_ptr con = m_server.get_con_from_hdl(hdl);
            if (!con || con->get_state() != websocketpp::session::state::open)return;
            if (msg->get_opcode() != websocketpp::frame::opcode::text) {
                con->close(websocketpp::close::status::invalid_payload,"Payload not is txt");
                return;
            }
            json js = json::parse(msg->get_payload());
            //快速验证令牌有效性
            auto ret = WebSocketService::verify_access_token(js);
            if (ret == selfdefine::frame::status::token_expire) {
                json res;
                res["status"] = selfdefine::frame::status::token_expire;
                res["body"] = "expire";
                auto ec = con->send(res.dump());
                if (ec) {
                    LOG_DEBUG<<ec.value()<<": "<<ec.message();
                }
                return;
            }else if (ret != selfdefine::frame::status::ok) {
                json res;
                res["status"] = selfdefine::frame::status::token_expire;
                res["body"] = "expire";
                auto ec = con->send(res.dump());
                if (ec) {
                    LOG_DEBUG<<ec.value()<<": "<<ec.message();
                }
                return;
            }
            con->defer_http_response();
            auto task = std::make_shared<std::function<void()>>([this,hdl,payload = msg->get_payload()]() {
                json res;
                res["status"] = selfdefine::frame::status::not_found;
                res["body"] = "NOT FOUND";
                string response = res.dump();
                json js = json::parse(payload);
                try {
                    auto handler = WebSocketService::GetInstance()->GetWsHandler(js["API"].get<string>());
                    if (handler) {
                        //执行
                        response = handler(m_server,hdl,js);
                    }
                } catch (const std::exception &e) {
                    res["status"] = selfdefine::frame::status::bad_request;
                    res["body"] = e.what();
                    response = res.dump();
                }
                //交由io_context线程调度
                this->m_server.get_io_service().post([this,hdl,response]() {
                    server::connection_ptr con = this->m_server.get_con_from_hdl(hdl);
                    if (con && con->get_state() == websocketpp::session::state::open) {
                        try {
                            auto ec = con->send(response);
                            if (ec) {
                                LOG_DEBUG<<ec.value()<<": "<<ec.message();
                            }
                        }catch (const std::exception &e) {
                            LOG_DEBUG<<"响应错误："<<e.what();
                        }
                    }else {
                        LOG_DEBUG<<"连接已关闭，抛弃连接";
                    }
                });
            });
            m_worker_pool->submit(task);
        }catch (exception& e) {
            LOG_ERROR << "message handler error: " << e.what();
        }
    }
    //消费其他服务器转发的请求
    void on_consumer(string&& data) {
        try {
            //获取消息并解析
            json js = json::parse(data);
            //获取包含的用户ID，查找对应的hdl
            string uid = js["uid"].get<std::string>();
            auto hdl = WebSocketService::GetInstance()->getUserHdl(uid);
            auto handler = WebSocketService::GetInstance()->GetWsHandler(js["API"].get<string>());
            string response;
            if (handler) {
                //执行
                response = handler(m_server,hdl,js);
            }else {
                json res;
                res["status"] = selfdefine::frame::status::not_found;
                res["body"] = "NOT FOUND";
                response = res.dump();
            }
            //交由io_context线程调度
            this->m_server.get_io_service().post([this,hdl,response]() {
                server::connection_ptr con = this->m_server.get_con_from_hdl(hdl);
                if (con && con->get_state() == websocketpp::session::state::open) {
                    try {
                        auto ec = con->send(response);
                        if (ec) {
                            LOG_DEBUG<<ec.value()<<": "<<ec.message();
                        }
                    }catch (const std::exception &e) {
                        LOG_DEBUG<<"响应错误："<<e.what();
                    }
                }else {
                    LOG_DEBUG<<"连接已关闭，抛弃连接";
                }
            });
        }catch (exception &e) {
            //失败对于目标用户是无感
            LOG_DEBUG<<data<<" 执行失败: "<<e.what();
        }
    }
private:
    // core objects
    server                         m_server;
    std::shared_ptr<KafkaMessageConsumer> kafka_consumer;
    std::shared_ptr<ThreadPool>    m_worker_pool;
    std::vector<std::thread>       m_io_threads;
};
#include <websocketpp/client.hpp>
#include <websocketpp/config/asio_client.hpp>
#include <nlohmann/json.hpp>
#include <boost/asio.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/version.hpp>
#include <string>
#include <iostream>
using json = nlohmann::json;
using namespace std;
// WebSocket客户端类型定义
typedef websocketpp::client<websocketpp::config::asio_tls_client> client;
typedef websocketpp::config::asio_tls_client::message_type::ptr message_ptr;
typedef websocketpp::lib::shared_ptr<websocketpp::lib::asio::ssl::context> context_ptr;
typedef client::connection_ptr connection_ptr;

using websocketpp::lib::placeholders::_1;
using websocketpp::lib::placeholders::_2;
using websocketpp::lib::bind;
#define CERTIFICATE_CHAIN_PATH "../key/root_ca.pem"
class conn_metadata {
private:
    uint64_t uid;
    websocketpp::connection_hdl hdl;
    string status;
    string url;
    string server;
    string error_reason;
    vector<string> recodes;
public:
    conn_metadata(const uint64_t& _id,websocketpp::connection_hdl _hdl, const string& _url):
        uid(_id),hdl(_hdl),status("connectiong"),url(_url),server("N/A"){}
    void on_open(client* c,websocketpp::connection_hdl hdl) {
        try{
            this->status = "open";
            connection_ptr con = c->get_con_from_hdl(hdl);
            if (!con || con->get_state() != websocketpp::session::state::open) {
                this->status = "error";
                this->error_reason = "closed";
                return;
            }
            this->server = con->get_response_header("Server");
        }catch (exception& e) {
            this->status = "error";
            this->error_reason = e.what();
        }
    }

    void on_close(client* c,websocketpp::connection_hdl hdl) {
        try{
            this->status = "closed";
            connection_ptr con = c->get_con_from_hdl(hdl);
            if (!con) {
                this->status = "error";
                this->error_reason = "connection is gone";
                return;
            }
            stringstream ss;
            ss<< "close code: " << con->get_remote_close_code() << " ("
              << websocketpp::close::status::get_string(con->get_remote_close_code())
              << "), close reason: " << con->get_remote_close_reason();
            error_reason = ss.str();
        }catch (exception& e) {
            this->status = "error";
            this->error_reason = e.what();
        }
    }

    void on_failed(client * c, websocketpp::connection_hdl hdl) {
        try{
            this->status = "failed";
            connection_ptr con = c->get_con_from_hdl(hdl);
            if (!con) {
                this->status = "error";
                this->error_reason = "connection is gone";
                return;
            }
            this->server = con->get_response_header("Server");
            this->error_reason = con->get_ec().message();
        }catch (exception& e) {
            this->status = "error";
            this->error_reason = e.what();
        }
    }

    void on_message(websocketpp::connection_hdl hdl,client::message_ptr msg) {
        if (msg->get_opcode() == websocketpp::frame::opcode::text) {
            recodes.push_back("  << " + msg->get_payload());
        } else {
            recodes.push_back("  << " + websocketpp::utility::to_hex(msg->get_payload()));
        }
    }


    websocketpp::connection_hdl get_hdl() const {
        return hdl;
    }

    int get_id() const {
        return uid;
    }

    std::string get_status() const {
        return status;
    }

    void record_send_message(const string& msg) {
        recodes.push_back(::move(msg));
    }

    void print() {
        cout << "> URI: " << url << "\n"
        << "> Status: " << status << "\n"
        << "> Remote Server: " << (server.empty() ? "None Specified" : server) << "\n"
        << "> Error/close reason: " << (error_reason.empty() ? "N/A" : error_reason)<<endl;
        cout<<"message processed: "<<recodes.size()<<endl;
        for (auto& s:recodes) {
            cout<<">> "<<s;
        }
    }
};

class TLS_endpoint {
private:
    typedef unordered_map<uint64_t,shared_ptr<conn_metadata>> conn_map;

    client m_endpoint;
    shared_ptr<thread> m_thread;
    conn_map m_conn_map;
    uint64_t next_id;

    // TLS初始化处理
    static std::shared_ptr<boost::asio::ssl::context> on_tls_init(websocketpp::connection_hdl) {
        context_ptr ctx = websocketpp::lib::make_shared<boost::asio::ssl::context>(boost::asio::ssl::context::tlsv12);
        try {
            ctx->set_options(boost::asio::ssl::context::default_workarounds |  // 兼容老版本 OpenSSL bug
                             boost::asio::ssl::context::no_sslv2 |             // 禁用极不安全的 SSLv2
                             boost::asio::ssl::context::no_sslv3 |
                             boost::asio::ssl::context::no_tlsv1 |
                             boost::asio::ssl::context::no_tlsv1_1 |
                             boost::asio::ssl::context::single_dh_use);        // 每次握手重新生成 DH key，防止重放

            // 单向TLS验证，验证服务端CA有效性
            ctx->set_verify_mode(boost::asio::ssl::verify_peer);
            ctx->load_verify_file(CERTIFICATE_CHAIN_PATH);

        } catch (std::exception& e) {
            std::cout << "Error in TLS context initialization: " << e.what() << std::endl;
        }
        return ctx;
    }

public:
    TLS_endpoint():next_id(0) {
        this->m_endpoint.clear_access_channels(websocketpp::log::alevel::all);
        this->m_endpoint.clear_error_channels(websocketpp::log::elevel::all);
        m_endpoint.set_access_channels(websocketpp::log::alevel::all);
        m_endpoint.set_error_channels(websocketpp::log::elevel::all);
        this->m_endpoint.init_asio();
        m_endpoint.start_perpetual();
        m_endpoint.set_tls_init_handler(std::bind(
            &TLS_endpoint::on_tls_init,
            std::placeholders::_1
        ));
        m_thread = make_shared<thread>(&client::run, &m_endpoint);
    }

    ~TLS_endpoint() {
        m_endpoint.stop_perpetual();
        for (auto& item:m_conn_map) {
            if (item.second->get_status() != "open") {
                continue;
            }
            cout<<"close connection "<<item.second->get_id()<<endl;
            error_code ec;
            m_endpoint.close(item.second->get_hdl(),websocketpp::close::status::going_away,"",ec);
            if (ec) {
                cout<<"close connection "<<item.second->get_id()<<" error: "<<ec.message()<<endl;
            }
        }
        m_thread->join();
    }

    int connect(const string& url) {
        try{
            error_code ec;
            connection_ptr con = m_endpoint.get_connection(url,ec);
            if (ec) {
                cout<<"connect initialization error: "<<ec.message()<<endl;
                return -1;
            }
            uint64_t id = next_id++;
            shared_ptr<conn_metadata> metadata_ptr = make_shared<conn_metadata>(id,con->get_handle(),url);
            if (metadata_ptr == nullptr) {
                cout<<"get client metadata error"<<endl;
                return -1;
            }
            m_conn_map[id] = metadata_ptr;
            con->set_open_handler(std::bind(
                &conn_metadata::on_open,
                metadata_ptr,
                &this->m_endpoint,
                _1
            ));
            con->set_fail_handler(std::bind(
                &conn_metadata::on_failed,
                metadata_ptr,
                &this->m_endpoint,
                _1
            ));
            con->set_close_handler(std::bind(
                &conn_metadata::on_close,
                metadata_ptr,
                &this->m_endpoint,
                _1
            ));
            con->set_message_handler(std::bind(
                &conn_metadata::on_message,
                metadata_ptr,
                _1,
                _2
            ));
            m_endpoint.connect(con);
            return id;
        }catch (exception& e) {
            cout<<"TLS_endpoint::connect error: "<<e.what()<<endl;
            return -1;
        }
    }

    shared_ptr<conn_metadata> get_metadata(const uint64_t& id) {
        try{
            conn_map::const_iterator it = this->m_conn_map.find(id);
            if (it == this->m_conn_map.end()) {
                return nullptr;
            }else {
                return it->second;
            }
        }catch (exception& e) {
            cout<<"TLS_endpoint::get_metadata error: "<<e.what()<<endl;
            return nullptr;
        }
    }

    void close(const uint64_t& id,websocketpp::close::status::value code,const string& reason) {
        try {
            error_code ec;
            if (this->m_conn_map.count(id) == 0) {
                cout<<"not found id: "<<id<<endl;
                return;
            }
            shared_ptr<conn_metadata> item = this->m_conn_map[id];
            m_endpoint.close((*item).get_hdl(),code,reason,ec);
            if (ec) {
                cout<<"close error: "<<ec.message()<<endl;
            }
        }catch (exception& e) {
            cout<<"TLS_endpoint::close error: "<<e.what()<<endl;
        }
    }

    void send(const uint64_t& id,const string& message) {
        error_code ec;
        if (this->m_conn_map.count(id) == 0) {
            cout<<"Not Found Connection: "<<id<<endl;
            return;
        }
        auto metadata = this->m_conn_map[id];
        m_endpoint.send(metadata->get_hdl(),message,websocketpp::frame::opcode::text,ec);
        if (ec) {
            cout<<"send error: "<<ec.message()<<endl;
            return;
        }
        metadata->record_send_message(message);
    }
};

//https，websocketpp不支持发起https请求
class req_info {
public:
    std::string host;
    std::string service;
    std::string target;
    boost::beast::http::verb method;
};
uint64_t uid;
string access_token;
string reflush_token;
class request {
private:
    req_info info;
    boost::beast::http::request<boost::beast::http::string_body> req;
    boost::beast::http::response<boost::beast::http::dynamic_body> res;

public:
    request(req_info& _info):info(_info) {

    }

    void get_http_response() {
        http_get();
    }
    void get_https_response() {
        https_get();
    }
    void get_post_response(string&& body) {
        https_post(body);
    }
private:

    void http_get() {
        try {
            req.method(info.method);
            req.target(info.target);
            req.set(boost::beast::http::field::host,info.host);
            req.set(boost::beast::http::field::user_agent,"HttpLoginTest");
            boost::asio::io_context ioc;
            boost::asio::ip::tcp::resolver resolver(ioc);
            auto result = resolver.resolve(info.host,info.service);
            boost::beast::tcp_stream stream(ioc);
            stream.connect(result);

            boost::beast::http::write(stream,req);

            boost::beast::flat_buffer buffer;
            boost::beast::http::read(stream,buffer,res);
            std::cout << "return code: " << res.result_int() << std::endl;
            std::cout << "HTTP/" << res.version() << " " << res.result() << " "
                      << res.reason() << "\n";
            std::cout << "Body: "
                      << boost::beast::buffers_to_string(res.body().data()) << "\n";
            stream.socket().shutdown(boost::asio::ip::tcp::socket::shutdown_both);
        }catch (exception& e) {
            cout<<"HTTP error: "<<e.what()<<endl;
        }
    }

    void https_get() {
        try {
            boost::asio::io_context ioc;
            boost::asio::ssl::context ctx(boost::asio::ssl::context::tlsv12);
            ctx.set_verify_mode(boost::asio::ssl::verify_peer);
            ctx.load_verify_file(CERTIFICATE_CHAIN_PATH);
            boost::asio::ip::tcp::resolver resolver(ioc);
            auto result = resolver.resolve(info.host,info.service);

            boost::beast::ssl_stream<boost::beast::tcp_stream> stream(ioc,ctx);

            SSL_set_tlsext_host_name(stream.native_handle(),info.host.c_str());

            boost::beast::get_lowest_layer(stream).connect(result);

            stream.handshake(boost::asio::ssl::stream_base::client);

            req.method(info.method);
            req.target(info.target);
            req.set(boost::beast::http::field::host,info.host);
            req.set(boost::beast::http::field::user_agent,"HttpsLoginTest");

            boost::beast::http::write(stream,req);

            boost::beast::flat_buffer buffer;
            boost::beast::http::read(stream, buffer, res);

            std::cout << "https response" << std::endl;
            std::cout << "return code: " << res.result_int() << std::endl;
            std::cout << "HTTP/" << res.version() << " " << res.result() << " "
                      << res.reason() << "\n";
            std::cout << "Body: "<<boost::beast::buffers_to_string(res.body().data())<< "\n";
            stream.shutdown();
        }catch(exception& e) {
            cout<<"HTTPS error: "<<e.what()<<endl;
        }
    }

    void https_post(string& body) {
        try {
            boost::asio::io_context ioc;
            boost::asio::ssl::context ctx(boost::asio::ssl::context::tlsv12);
            ctx.set_verify_mode(boost::asio::ssl::verify_peer);
            ctx.load_verify_file(CERTIFICATE_CHAIN_PATH);
            boost::asio::ip::tcp::resolver resolver(ioc);
            auto result = resolver.resolve(info.host,info.service);

            boost::beast::ssl_stream<boost::beast::tcp_stream> stream(ioc,ctx);

            SSL_set_tlsext_host_name(stream.native_handle(),info.host.c_str());

            boost::beast::get_lowest_layer(stream).connect(result);

            stream.handshake(boost::asio::ssl::stream_base::client);

            req.method(info.method);
            req.target(info.target);
            req.set(boost::beast::http::field::host,info.host);
            req.set(boost::beast::http::field::user_agent,"HttpsLoginTest");

            req.set(boost::beast::http::field::content_type,"application/json; charset=utf-8");
            req.body() = body;
            req.prepare_payload();

            boost::beast::http::write(stream,req);

            boost::beast::flat_buffer buffer;
            boost::beast::http::read(stream, buffer, res);

            std::cout << "https response" << std::endl;
            std::cout << "return code: " << res.result_int() << std::endl;
            std::cout << "HTTP/" << res.version() << " " << res.result() << " "
                      << res.reason() << "\n";
            string str = boost::beast::buffers_to_string(res.body().data());
            json js = json::parse(str);
            std::cout << "Body: "
                      << js << "\n";
            boost::beast::error_code ec;
            stream.shutdown(ec);
            if (ec) {
                cout<<"closed: "<<ec.message()<<"\n";
                return;
            }
            uid = stoull(js["uid"].get<string>());
            access_token = js["access_token"].get<string>();
            reflush_token = js["reflush_token"].get<string>();

        }catch(exception& e) {
            cout<<"HTTPS error: "<<e.what()<<endl;
        }
    }

};


void test0_6(){
    bool done = false;
    string input;
    TLS_endpoint endpoint;

    while (!done) {
        try{
            cout<<"Enter CMD : ";
            getline(cin,input);
            if (input == "quit") {
                done = true;
            }else if (input == "help") {
                std::cout
                    << "\nCommand List:\n"
                    << "  connect <ws uri>\n"   //connect https://192.168.250.100:6000/login?data={"account":"testaccount2","password":"hashedpassword2"}
                    << "  register\n"
                    << "  https\n"
                    << "  post\n"
                    << "  send <connection id> <message>\n"
                    << "  close <connection id> [<close code:default=1000>] [<close reason>]\n"
                    << "  show <connection id>\n"
                    << "  help: Display this help text\n"
                    << "  quit: Exit the program\n"
                    << std::endl;
            }else if (input.substr(0,7) == "connect") {
                stringstream ss;
                ss<<R"(wss://192.168.250.100:6333/?uid=)"<<uid<<"&token="<<access_token;
                string url = ss.str();
                int id = endpoint.connect(url);
                if (id != -1) {
                    cout<<"connect success"<<endl;
                }
            }else if (input.substr(0,5) == "https") {
                req_info info;
                info.host = "192.168.250.100";
                info.service = "6333";
                info.target = "/login";
                info.method = boost::beast::http::verb::get;
                request req(info);
                req.get_https_response();
            }else if (input.substr(0,4) == "post") {
                req_info info;
                info.host = "192.168.250.100";
                info.service = "6333";
                info.target = "/login";
                info.method = boost::beast::http::verb::post;
                json js;
                js["account"] = "user1account";
                js["password"] = "user1password";
                request req(info);
                req.get_post_response(js.dump());
            }else if (input.substr(0,8) == "register") {
                req_info info;
                info.host = "192.168.250.100";
                info.service = "6333";
                info.target = "/register";
                info.method = boost::beast::http::verb::post;
                json js;
                js["username"] = "user1";
                js["account"] = "user1account";
                js["password"] = "user1password";
                request req(info);
                req.get_post_response(js.dump());
            }else if (input.substr(0,4) == "send") {
                std::stringstream ss(input);

                std::string cmd;
                int id;
                std::string message;

                ss >> cmd >> id;
                std::getline(ss,message);

                endpoint.send(id, message);
            }else if (input.substr(0,5) == "close") {
                std::stringstream ss(input);

                std::string cmd;
                int id;
                int close_code = websocketpp::close::status::normal;
                std::string reason;

                ss >> cmd >> id >> close_code;
                std::getline(ss,reason);

                endpoint.close(id, close_code, reason);
            }else if (input.substr(0,4) == "show") {
                int id = atoi(input.substr(5).c_str());
                auto metadata = endpoint.get_metadata(id);
                if (metadata != nullptr) {
                    metadata->print();
                }else {
                    cout<<"invalid id: "<<id<<endl;
                }
            }else {
                cout<<"UNKNOW CMD: "<<input<<endl;
            }
        }catch (exception& e) {
            cout<<e.what()<<endl;
        }
    }
}


int main(){
    test0_6();
    return 0;
}
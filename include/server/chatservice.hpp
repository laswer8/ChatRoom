#pragma once

#include "HeadFile.h"
#include "dbhandler/user_handler.hpp"
#include "dbhandler/offlinemsg_handler.hpp"
#include "dbhandler/chatgroup_handler.hpp"
#include "dbhandler/friend_handler.hpp"
#include "dbhandler/request_handler.hpp"
#include "sslservice.hpp"
#include "kafkahandler.hpp"
#include "redishandler.hpp"
#include "dbhandler/history_handler.hpp"

#define SERVER_ID "ChatServer1"
#define ACCESS_SCOPE "access"
#define REFLUSH_SCOPE "reflush"

//websocket数据帧风格，统一返回json序列化字符串，包含自定义状态码与消息体
using WebSocketMsgHandler = function<string (websocketpp::server<websocketpp::config::asio_tls>&,const websocketpp::connection_hdl&,json&)>;
//http响应包风格，返回消息体与状态码
using HttpMsgHandler = function<pair<string,websocketpp::http::status_code::value> (websocketpp::server<websocketpp::config::asio_tls>&,const websocketpp::connection_hdl&,json&)>;

//HTTP风格API
namespace HTTP_API_TYPE {
    const string LOGIN_API = "/login";      //客户端使用HTTPS请求进行登录
    const string REGISTER_API = "/register";    //客户端使用HTTPS进行注册
};

//Websocket风格API
namespace WS_API_TYPE {
    const string INFO_API = "/api/info";
    const string REFRESH_TOKEN_API = "/api/fresh";    //用户令牌过期进行验证
    const string TRANSPROT_API = "/api/transport";  //转发操作消息
    const string OFFLINE_API = "/api/offline";  //获取离线消息
    const string USERINFO_API = "/api/user/info";  //获取用户信息
    const string GROUPINFO_API = "/api/group/info";  //获取群组信息
    const string FRIENDLIST_API = "/api/friend/list";  //获取好友列表
    const string GROUPLIST_API = "/api/group/list";  //获取群组列表
    const string REQUESTLIST_API = "/api/request/list";  //获取请求列表
    const string GROUPMEMBERLIST_API = "/api/group/member/list";  //获取群组成员列表
    const string SEARCH_API = "/api/search";  //条件搜索
    const string SENDFRIENDREQUEST_API = "/api/send/friend/request";  //发送好友请求
    const string SENDGROUPREQUEST_API = "/api/send/group/request";  //获取入群申请
    const string PROCESSFRIENDREQUEST_API = "/api/process/friend/request";  //处理好友请求
    const string PROCESSGROUPREQUEST_API = "/api/process/group/request";  //处理入群申请
    const string DELETEFRIEND_API = "/api/delete/friend";  //删除好友
    const string QUITGROUP_API = "/api/quit/group";  //退出群组
    const string CREATEGROUP_API = "/api/create/group";  //创建群组
    const string KICKOUTMEMBER_API = "/api/kickout/member";  //踢出组员
    const string UPMEMBERROLE_API = "/api/improve/member/role";  //提升组员权限
    const string REVOKEMEMBERROLE_API = "/api/revoke/member/role";  //撤销组员权限
    const string CHAT_API = "/api/chat";  //聊天
    const string GROUPCHAT_API = "/api/group/chat";  //群组聊天
    const string SAVEHISTROY_API = "/api/save/history";  //保存历史记录
    const string HISTROY_API = "/api/history";  //获取历史记录

};

namespace websocketpp::close::status {
    static value const un_valied = 4000;
    static value const token_expire = 4001;
    static value const closed = 4002;
};

namespace selfdefine::frame::status {
    enum value {
        uninitialized = 0,

        continue_code = 100,
        switching_protocols = 101,

        ok = 200,
        created = 201,
        accepted = 202,
        non_authoritative_information = 203,
        no_content = 204,
        reset_content = 205,
        partial_content = 206,

        multiple_choices = 300,
        moved_permanently = 301,
        found = 302,
        see_other = 303,
        not_modified = 304,
        use_proxy = 305,
        temporary_redirect = 307,

        bad_request = 400,
        unauthorized = 401,
        payment_required = 402,
        forbidden = 403,
        not_found = 404,
        method_not_allowed = 405,
        not_acceptable = 406,
        proxy_authentication_required = 407,
        request_timeout = 408,
        conflict = 409,
        gone = 410,
        length_required = 411,
        precondition_failed = 412,
        request_entity_too_large = 413,
        request_uri_too_long = 414,
        unsupported_media_type = 415,
        request_range_not_satisfiable = 416,
        expectation_failed = 417,
        im_a_teapot = 418,
        upgrade_required = 426,
        precondition_required = 428,
        too_many_requests = 429,
        request_header_fields_too_large = 431,
        token_expire = 432,

        internal_server_error = 500,
        not_implemented = 501,
        bad_gateway = 502,
        service_unavailable = 503,
        gateway_timeout = 504,
        http_version_not_supported = 505,
        not_extended = 510,
        network_authentication_required = 511
    };
};

class WebSocketService {
    using server         = websocketpp::server<websocketpp::config::asio_tls>;
    using connection_hdl = websocketpp::connection_hdl;
    using message_ptr    = server::message_ptr;
private:
    HttpMsgHandler generateHttpVoidHandler(const string &msg,const websocketpp::http::status_code::value& status) {
        return [&msg,&status](websocketpp::server<websocketpp::config::asio_tls>&,const websocketpp::connection_hdl &, json &) {
            return pair(msg,status);
        };
    }
    WebSocketMsgHandler generateWSVoidHandler(const string &msg,const selfdefine::frame::status::value& status) {
        return [&msg,&status](websocketpp::server<websocketpp::config::asio_tls>&,const websocketpp::connection_hdl &, json &) {
            json js;
            js["body"] = msg;
            js["status"] = status;
            return js.dump();
        };
    }
public:
    static shared_ptr<WebSocketService> GetInstance() {
        static shared_ptr<WebSocketService> service = shared_ptr<WebSocketService>(new WebSocketService());
        return service;
    }

    WebSocketMsgHandler GetWsHandler(const string& type) {
        try {
            if (WsServiceMap.count(type) == 0) {
                return generateWSVoidHandler("NOT FOUND SERVICE",selfdefine::frame::status::not_found);
            }
            return WsServiceMap[type];
        }catch (exception &e) {
            //失败返回一个空函数
            return generateWSVoidHandler(e.what(),selfdefine::frame::status::bad_request);
        }
    }

    HttpMsgHandler GetHttpHandler(const string& type) {
        try {
            if (HttpServiceMap.count(type) == 0) {
                return generateHttpVoidHandler("NOT FOUND SERVICE",websocketpp::http::status_code::not_found);
            }
            return HttpServiceMap[type];
        }catch (exception &e) {
            //失败返回一个空函数
            return generateHttpVoidHandler(e.what(),websocketpp::http::status_code::bad_request);
        }
    }

    websocketpp::connection_hdl getUserHdl(const string& address) {
        shared_lock<shared_mutex> lock(m);
        if (UserMap.count(address) == 0) {
            throw runtime_error("User Closed");
        }
        return UserMap[address];
    }

    /// http注册流程
    /**
    * @param m_server 服务器句柄
    * @param hdl  连接句柄
    * @param js   参数
    * @return <请求体，状态码>
    *
    *1. 用户登录发送https POST请求，进行注册
    *2. 使用分布式雪花算法生成唯一用户ID，将其与账户与加密密码写入数据库
    *3. 添加布隆过滤器
    *4. 返回状态码，判断状态
    *5. 后续客户端应再次发起登录请求
    */
    pair<string,websocketpp::http::status_code::value> registe(websocketpp::server<websocketpp::config::asio_tls>& m_server,const websocketpp::connection_hdl &hdl, json &js) {
        try {
            server::connection_ptr con = m_server.get_con_from_hdl(hdl);
            if (con) {
                string username = js["username"].get<string>();
                string count = js["account"].get<string>();
                string pwd = js["password"].get<string>();
                if (username.empty() || count.empty() || pwd.empty()) {
                    return make_pair("username not allow is empty or count or password unavailable",websocketpp::http::status_code::unauthorized);
                }
                try {
                    // auto res = DBCache::GetInstance()->CacheFindWithBloom("ChatRoom","user",
                    //     count,ttl,
                    //     ":","select 1 from user where account = ?",count);
                    //布隆过滤器验证，判断用户是否已存在
                    if (redis_handler->BfExistByAC("ChatRoom","user",count))
                        return make_pair("User Already Exists",websocketpp::http::status_code::conflict);
                }catch (runtime_error& re) {
                    LOG_ERROR<<"runtime_error: "<<re.what();
                    return make_pair("DB Service Unavaliable",websocketpp::http::status_code::service_unavailable);
                }
                auto uid = Snowflake::GetInstance()->generateUniqueId();
                string cache_key = "lock:user:registe:"+count;
                string cache_value = SSLService::generatorUUID();
                uint64_t ttl = redis_handler->GenerateTTL(30000,60000);
                distribute_lock lock(cache_key,cache_value,ttl>0?ttl:60000);
                if (!lock.is_success()) {
                    //获取锁失败
                    return make_pair("Repeat Rejester Request",websocketpp::http::status_code::conflict);
                }
                string hash_pwd;
                if (!SSLService::bcryptEncode(pwd,hash_pwd) || hash_pwd.empty()) {
                    return make_pair("Encode Service Unavaliable",websocketpp::http::status_code::service_unavailable);
                }
                if (!user_handler->appendUser(uid,username,count,hash_pwd)) {
                    return make_pair("register failed",websocketpp::http::status_code::unauthorized);
                }
                //添加布隆过滤器
                if (!DBCache::GetInstance()->bm_add(count)
                    || !DBCache::GetInstance()->bm_add(to_string(uid))) {
                    return make_pair("BF Service Unavaliable",websocketpp::http::status_code::service_unavailable);
                }
                return make_pair("success",websocketpp::http::status_code::ok);
            }
            return make_pair("Connection Closed",websocketpp::http::status_code::request_timeout);
        }catch (exception &e) {
            return make_pair(e.what(),websocketpp::http::status_code::bad_request);
        }
    }

    /// http登录流程
    /**
    * @param m_server 服务器句柄
    * @param hdl  连接句柄
    * @param js   参数
    * @return <请求体，状态码>
    *
    *1. 用户登录发送https POST请求，请求登录
    *2. 检验账号密码，失败返回
    *3. 成功生成两个jwt作为token(短期token、刷新token)返回给客户端，服务端存储刷新token token:userid:刷新token 用户ID TTL
    *4. 短期token用于api请求，刷新token用于检测用户连接有效性，便于及时控制连接
    *5. 后续客户端使用短期token发起wss连接，服务端存储对应连接的映射
    */
    pair<string,websocketpp::http::status_code::value> login(websocketpp::server<websocketpp::config::asio_tls>& m_server,const websocketpp::connection_hdl &hdl, json &js) {
        try {
            auto start = std::chrono::high_resolution_clock::now();

            server::connection_ptr con = m_server.get_con_from_hdl(hdl);
            if (con) {
                string count = js["account"].get<string>();
                string pwd = js["password"].get<string>();
                if (count.empty() || pwd.empty()) {
                    return make_pair("count or password unavailable",websocketpp::http::status_code::unauthorized);
                }

                std::cout << "函数执行时间: " <<
                    std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - start).count()
                << " 毫秒" << std::endl;
                start = std::chrono::high_resolution_clock::now();

                //数据库请求
                shared_ptr<User> res = nullptr;
                try {
                    //根据用户名获取账户
                    //布隆过滤器验证
                    if (!redis_handler->BfExistByAC("ChatRoom","user",count))
                        return make_pair("User Not Exists",websocketpp::http::status_code::unauthorized);
                    res = UserHandler::getUserByAccount(count);
                }catch (runtime_error& re) {
                    LOG_ERROR<<"runtime_error: "<<re.what();
                    return make_pair("Service Unavaliable",websocketpp::http::status_code::service_unavailable);
                }

                std::cout << "函数执行时间: " <<
                    std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - start).count()
                << " 毫秒" << std::endl;
                start = std::chrono::high_resolution_clock::now();

                if (res == nullptr) {
                    //用户不存在
                    return make_pair("用户不存在",websocketpp::http::status_code::unauthorized);
                }
                //校验
                if (res->getUsername() != count or !SSLService::bcryptVartify(pwd,res->getPassword())) {
                    //校验失败
                    return make_pair("账号或密码错误",websocketpp::http::status_code::unauthorized);
                }

                std::cout << "函数执行时间: " <<
                    std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - start).count()
                << " 毫秒" << std::endl;
                start = std::chrono::high_resolution_clock::now();

                string uid = to_string(res->getUID());
                //验证通过，生成JWT
                unordered_map<string,jwt::claim> payload;
                //生成短期令牌,有效期15分钟
                payload["scope"] = jwt::claim(string(ACCESS_SCOPE));
                string access_token = SSLService::getJWT(SERVER_ID,"uid:"+uid,payload,
                    chrono::system_clock::now()+chrono::minutes(60),
                    chrono::system_clock::now());
                //生成刷新令牌，有效期7天
                payload["scope"] = jwt::claim(string(REFLUSH_SCOPE));
                //需要生成JTI防止重放攻击,添加SERVER_ID保证分布式环境安全
                string jti = SERVER_ID + SSLService::generatorUUID();
                string reflush_token = SSLService::getJWT(SERVER_ID,"uid:"+uid,payload,
                    chrono::system_clock::now()+chrono::hours(24*7),
                    chrono::system_clock::now(),
                    jti);

                if (access_token.empty() || reflush_token.empty()) {
                    return make_pair("Get Token Error",websocketpp::http::status_code::service_unavailable);
                }

                std::cout << "函数执行时间: " <<
                    std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - start).count()
                << " 毫秒" << std::endl;
                start = std::chrono::high_resolution_clock::now();

                //存储JTI到redis
                stringstream ss;
                ss<<"reflushtoken:"<<jti;
                try{
                    //写入数据库使用分布式锁,超时时间1min
                    string cache_key = "lock:user:login:"+uid;
                    string cache_value = SSLService::generatorUUID();
                    uint64_t ttl = redis_handler->GenerateTTL(30000,60000);
                    distribute_lock lock(cache_key,cache_value,ttl>0?ttl:60000);
                    if (!lock.is_success()) {
                        //获取锁失败
                        return make_pair("Repeat Login Request",websocketpp::http::status_code::conflict);
                    }
                    ttl = redis_handler->GenerateTTL(3600000*24*5,3600000*24*7);
                    if (!redis_handler->CacheKey(ss.str(),"1",ttl>0?ttl:3600000*24*7)) {
                        //缓存失败
                        return make_pair("Cache Error",websocketpp::http::status_code::bad_request);
                    }
                }catch (runtime_error& re) {
                    LOG_ERROR<<"runtime_error: "<<re.what();
                    return make_pair("Service Unavaliable",websocketpp::http::status_code::service_unavailable);
                }

                std::cout << "函数执行时间: " <<
                    std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - start).count()
                << " 毫秒" << std::endl;
                start = std::chrono::high_resolution_clock::now();

                //返回给客户端响应
                json response;
                response["uid"] = uid;
                response["name"] = res->getName();
                response["access_token"] = access_token;
                response["reflush_token"] = reflush_token;
                return make_pair(response.dump(),websocketpp::http::status_code::ok);
            }
            return make_pair("Connection Closed",websocketpp::http::status_code::request_timeout);
        }catch (exception &e) {
            return make_pair(e.what(),websocketpp::http::status_code::bad_request);
        }
    }

    ///验证短期令牌
    /**
    *@param con wss连接共享指针
    *@param leeway_seconds 延迟补偿，弥补运输带来的延迟
    *@return 状态码
    */
    static selfdefine::frame::status::value
    on_verify(const shared_ptr<websocketpp::connection<websocketpp::config::asio_tls>>& con,const uint64_t& leeway_seconds = 60) {
        try {
            if (!con){
                return selfdefine::frame::status::gone;
            }
            auto request = con->get_request();
            auto u_h = request.get_header("Upgrade");
            auto c_h = request.get_header("Connection");
            const string& method = request.get_method();
            //验证WebSocket特定头部,如果不包含升级请求代表为https请求，直接返回ok交给http_handler处理
            if (request.get_header("Upgrade") != "websocket" || request.get_header("Connection").find("Upgrade") == std::string::npos) {
                LOG_DEBUG << "Https Request Pass Validate";
                return selfdefine::frame::status::upgrade_required;
            }
            if (method != "GET") {
                LOG_DEBUG << "Invalid method for WebSocket handshake: " << method;
                return selfdefine::frame::status::method_not_allowed;
            }
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
            if (!kv.count("uid") || !kv.count("token") ) {
                //拒绝握手,设置状态码
                return selfdefine::frame::status::precondition_required;
            }
            unordered_map<string,jwt::claim> except_claim;
            except_claim["scope"] = jwt::claim(string(ACCESS_SCOPE));//确认令牌类型
            //验证token
            auto ret = SSLService::verifyJWT(kv["token"],
                SERVER_ID,
                except_claim,
                "uid:"+kv["uid"],
                "",
                60);
            if (ret == SSLService::verify_status::FAILED) {
                return selfdefine::frame::status::unauthorized;
            }else if (ret == SSLService::verify_status::EXPIRE) {
                return selfdefine::frame::status::token_expire;
            }
            return selfdefine::frame::status::ok;
        } catch (const std::exception &e) {
            LOG_DEBUG<<"Exception: "<<e.what();
            return selfdefine::frame::status::bad_request;
        }
    }

    ///验证短期令牌
    static selfdefine::frame::status::value
    verify_access_token(json &js,const uint64_t& leeway_sec = 60) {
        try {
            string token = js["token"].get<string>();
            string uid = js["uid"].get<string>();
            if (token.empty() || uid.empty()) {
                return selfdefine::frame::status::precondition_required;
            }
            unordered_map<string,jwt::claim> except_claim;
            except_claim["scope"] = jwt::claim(string(ACCESS_SCOPE));//确认令牌类型
            auto ret = SSLService::verifyJWT(token,
                SERVER_ID,
                except_claim,
                "uid:"+uid,
                "",
                leeway_sec);
            if (ret == SSLService::verify_status::FAILED) {
                // con->set_body("unavailable");
                // con->set_status(websocketpp::http::status_code::unauthorized);
                return selfdefine::frame::status::unauthorized;
            }else if (ret == SSLService::verify_status::EXPIRE) {
                // con->set_body("expire");
                // con->set_status(websocketpp::http::status_code::unauthorized);
                return selfdefine::frame::status::token_expire;
            }
            return selfdefine::frame::status::ok;
        }catch (exception &e) {
            LOG_DEBUG<<e.what();
            return selfdefine::frame::status::bad_request;
        }
    }

    /// 获取离线消息
    /**
    * @param m_server 服务器句柄
    * @param hdl  连接句柄
    * @param js   参数
    * @return json序列化结果
    *
    *1. 客户端成功登录建立WSS连接后，进入到首页，此时拥有uid、access_token
    *2. 客户端发送请求，携带：access_token、uid、fid、请求类型（好友会话、群组会话）
    *3. 安全性验证：验证uid、fid真实性
    *4. 返回{状态码，消息列表}
    */
    string offline(websocketpp::server<websocketpp::config::asio_tls>& m_server,const websocketpp::connection_hdl &hdl, json &js) {
        try {
            json response;
            server::connection_ptr con = m_server.get_con_from_hdl(hdl);
            if (!con || con->get_state() != websocketpp::session::state::open) {
                response["status"] = selfdefine::frame::status::gone;
                response["body"] = json(nullptr);
                return response.dump();
            }
            bool is_group = js["is_group"].get<bool>();
            string uid = js["uid"].get<string>();
            string fid = js["fid"].get<string>();
            if (uid.empty() || fid.empty()) {
                response["status"] = selfdefine::frame::status::bad_request;
                response["body"] = "Empty";
                return response.dump();
            }
            //布隆过滤器验证
            if (redis_handler->BfExistByAC("ChatRoom","user",uid)){
                if ((is_group && !redis_handler->BfExistByAC("ChatRoom","group",fid))
                    || (!is_group && !redis_handler->BfExistByAC("ChatRoom","user",fid))) {
                    response["status"] = selfdefine::frame::status::unauthorized;
                    response["body"] = "User Not Exists";
                    return response.dump();
                }
            }else {
                response["status"] = selfdefine::frame::status::unauthorized;
                response["body"] = "User Not Exists";
                return response.dump();
            }
            //获取离线消息
            auto res = this->offline_handler->GetUserOffline(uid,fid);
            response["status"] = selfdefine::frame::status::ok;
            response["body"] = res;
            return response.dump();
        }catch (exception &e) {
            json response;
            response["status"] = selfdefine::frame::status::bad_request;
            response["body"] = e.what();
            return response.dump();
        }
    }

    ///用户信息获取
    /**
     * @param js['uid'] 用户ID
     * @retuen 用户ID、用户名、用户账号、手机号、邮箱
     *
     * 获取的不是自己的信息，而是查看对方用户的详细信息
     */
    string GetUserInfo(websocketpp::server<websocketpp::config::asio_tls>& m_server,const websocketpp::connection_hdl &hdl, json &js) {
        try{
            json response;
            server::connection_ptr con = m_server.get_con_from_hdl(hdl);
            if (!con || con->get_state() != websocketpp::session::state::open) {
                response["status"] = selfdefine::frame::status::gone;
                response["body"] = json(nullptr);
                return response.dump();
            }
            string uid = js["uid"].get<string>();
            if (uid.empty()) {
                response["status"] = selfdefine::frame::status::bad_request;
                response["body"] = "MissCondition";
                return response.dump();
            }
            //布隆过滤器验证
            if(!redis_handler->BfExistByUID("ChatRoom","user",uid)){
                response["status"] = selfdefine::frame::status::unauthorized;
                response["body"] = "User Not Exists";
                return response.dump();
            }

            //尝试查询缓存
            string cache_key = "user:info:" + uid;
            uint64_t ttl = redis_handler->GenerateTTL(3600000*24*5,3600000*24*7);
            string cache_value;
            if (redis_handler->GetValue(cache_key,cache_value,ttl)) {
                json userinfo = json::parse(cache_value);
                response["status"] = selfdefine::frame::status::ok;
                response["body"] = userinfo;
                return response.dump();
            }
            //获取用户信息
            auto user = UserHandler::getUserInfoByID(uid);
            if (!user.first || !user.second) {
                response["body"] = "User Not Found";
                response["status"] = selfdefine::frame::status::not_found;
                return response.dump();
            }
            json userinfo;
            userinfo["uid"] = stoull(uid);
            userinfo["name"] = user.first->getName();
            userinfo["account"] = user.first->getUsername();
            userinfo["phone"] = user.second->getPhone();
            userinfo["email"] = user.second->getEmail();
            userinfo["created_at"] = user.second->getCreatedAt();
            //redis记录用户信息
            cache_value = userinfo.dump();
            redis_handler->CacheKey(cache_key,cache_value,ttl);

            response["status"] = selfdefine::frame::status::ok;
            response["body"] = userinfo;
            return response.dump();
        }catch (exception &e) {
            json response;
            response["body"] = e.what();
            response["status"] = selfdefine::frame::status::bad_request;
            LOG_DEBUG<<e.what();
            return response.dump();
        }
    }

    ///群组信息获取
    /**
     *@param js["gid"] 群组ID
     *@return 群组ID、群组名、群主id、创建时间、群描述、群人数、最近活跃时间
     *
     *用户查看群组详细信息
     */
    string GetGroupInfo(websocketpp::server<websocketpp::config::asio_tls>& m_server,const websocketpp::connection_hdl &hdl, json &js) {
        try{
            json response;
            server::connection_ptr con = m_server.get_con_from_hdl(hdl);
            if (!con || con->get_state() != websocketpp::session::state::open) {
                response["status"] = selfdefine::frame::status::gone;
                response["body"] = json(nullptr);
                return response.dump();
            }

            string gid = js["gid"].get<string>();
            if (gid.empty()) {
                response["status"] = selfdefine::frame::status::bad_request;
                response["body"] = "MissCondition";
                return response.dump();
            }
            //布隆过滤器验证
            if(!redis_handler->BfExistByUID("ChatRoom","chatgroup",gid)){
                response["status"] = selfdefine::frame::status::unauthorized;
                response["body"] = "User Not Exists";
                return response.dump();
            }
            //尝试查询缓存
            string cache_key = "group:info:" + gid;
            uint64_t ttl = redis_handler->GenerateTTL(3600000*1,3600000*2);
            string cache_value;
            if (redis_handler->GetValue(cache_key,cache_value,ttl)) {
                json groupinfo = json::parse(cache_value);
                response["status"] = selfdefine::frame::status::ok;
                response["body"] = groupinfo;
                return response.dump();
            }
            //获取群组信息
            json groupinfo = ChatGroupHandler::GetGroupInfo(gid);
            if (groupinfo.empty()) {
                response["body"] = "User Not Found";
                response["status"] = selfdefine::frame::status::not_found;
                return response.dump();
            }
            //redis短时间缓存信息
            cache_value = groupinfo.dump();
            redis_handler->CacheKey(cache_key,cache_value,ttl);

            response["status"] = selfdefine::frame::status::ok;
            response["body"] = groupinfo;
            return response.dump();
        }catch (exception &e) {
            json response;
            response["body"] = e.what();
            response["status"] = selfdefine::frame::status::bad_request;
            LOG_DEBUG<<e.what();
            return response.dump();
        }
    }

    ///获取好友列表
    /**
     *@param js["uid"] 用户ID
     *@return [(好友ID、好友名、在线状态),....]
     *
     *查询用户的好友列表，包含好友ID、好友名、在线状态
     */
    string GetFriendList(websocketpp::server<websocketpp::config::asio_tls>& m_server,const websocketpp::connection_hdl &hdl, json &js) {
        try{
            json response;
            server::connection_ptr con = m_server.get_con_from_hdl(hdl);
            if (!con || con->get_state() != websocketpp::session::state::open) {
                response["status"] = selfdefine::frame::status::gone;
                response["body"] = json(nullptr);
                return response.dump();
            }

            string uid = js["uid"].get<string>();
            if (uid.empty()) {
                response["status"] = selfdefine::frame::status::bad_request;
                response["body"] = "MissCondition";
                return response.dump();
            }
            //布隆过滤器验证
            if(!redis_handler->BfExistByUID("ChatRoom","user",uid)){
                response["status"] = selfdefine::frame::status::unauthorized;
                response["body"] = "User Not Exists";
                return response.dump();
            }
            //尝试查询缓存(仅包含uid、名称部分，在线状态不适合缓存)
            string cache_key = "user:friend:" + uid;
            uint64_t ttl = redis_handler->GenerateTTL(3600000*1,3600000*2); //缓存较短时间
            string cache_value;
            json flist = json::array();
            if (redis_handler->GetValue(cache_key,cache_value,ttl)) {
                flist = json::parse(cache_value);
                if (flist.empty()) {
                    response["body"] = "Empty";
                    response["status"] = selfdefine::frame::status::not_found;
                    return response.dump();
                }
            }else {
                //获取好友列表
                flist = FriendHandler::GetFriendList(uid);
                if (flist.empty()) {
                    response["body"] = "Empty";
                    response["status"] = selfdefine::frame::status::not_found;
                    return response.dump();
                }
                //redis短时间缓存信息
                cache_value = flist.dump();
                redis_handler->CacheKey(cache_key,cache_value,ttl);
            }
            //查询好友的在线状态
            string key = "local:"+uid;
            bool online = false;
            for (json& item : flist) {
                online = this->redis_handler->CacheExist("locak:"+item["uid"].get<string>());
                item["online"] = online;
            }

            response["status"] = selfdefine::frame::status::ok;
            response["body"] = flist;
            return response.dump();
        }catch (exception &e) {
            json response;
            response["body"] = e.what();
            response["status"] = selfdefine::frame::status::bad_request;
            LOG_DEBUG<<e.what();
            return response.dump();
        }
    }

    ///获取群组列表
    /**
     *@param js["uid"] 用户ID
     *@return [(群组ID、群组名),....]
     *
     *查询用户的群组列表，包含群组ID、群组名
     */
    string GetGroupList(websocketpp::server<websocketpp::config::asio_tls>& m_server,const websocketpp::connection_hdl &hdl, json &js) {
        try{
            json response;
            server::connection_ptr con = m_server.get_con_from_hdl(hdl);
            if (!con || con->get_state() != websocketpp::session::state::open) {
                response["status"] = selfdefine::frame::status::gone;
                response["body"] = json(nullptr);
                return response.dump();
            }

            string uid = js["uid"].get<string>();
            if (uid.empty()) {
                response["status"] = selfdefine::frame::status::bad_request;
                response["body"] = "MissCondition";
                return response.dump();
            }
            //布隆过滤器验证
            if(!redis_handler->BfExistByUID("ChatRoom","user",uid)){
                response["status"] = selfdefine::frame::status::unauthorized;
                response["body"] = "User Not Exists";
                return response.dump();
            }
            //尝试查询缓存(仅包含uid、名称部分，在线状态不适合缓存)
            string cache_key = "user:group:" + uid;
            uint64_t ttl = redis_handler->GenerateTTL(3600000*1,3600000*2); //缓存较短时间
            string cache_value;
            json glist = json::array();
            if (redis_handler->GetValue(cache_key,cache_value,ttl)) {
                glist = json::parse(cache_value);
                if (glist.empty()) {
                    response["body"] = "Empty";
                    response["status"] = selfdefine::frame::status::not_found;
                    return response.dump();
                }
            }else {
                //获取好友列表
                glist = ChatGroupHandler::GetGroupInfo(uid);
                if (glist.empty()) {
                    response["body"] = "Empty";
                    response["status"] = selfdefine::frame::status::not_found;
                    return response.dump();
                }
                //redis短时间缓存信息
                cache_value = glist.dump();
                redis_handler->CacheKey(cache_key,cache_value,ttl);
            }
            response["status"] = selfdefine::frame::status::ok;
            response["body"] = glist;
            return response.dump();
        }catch (exception &e) {
            json response;
            response["body"] = e.what();
            response["status"] = selfdefine::frame::status::bad_request;
            LOG_DEBUG<<e.what();
            return response.dump();
        }
    }

    ///获取请求列表
    /**
     *@param js["uid"] 用户ID
     *@return {
     *          freq:[(请求ID、发送者ID、接收者ID、请求状态、发送时间、验证消息),...],
     *          greq:[(请求ID、群组ID、用户ID、请求状态、发送时间、验证消息),...]
     *        }
     *
     *查询用户发送/接收的请求
     */
    string GetRequestList(websocketpp::server<websocketpp::config::asio_tls>& m_server,const websocketpp::connection_hdl &hdl, json &js) {
        try{
            json response;
            server::connection_ptr con = m_server.get_con_from_hdl(hdl);
            if (!con || con->get_state() != websocketpp::session::state::open) {
                response["status"] = selfdefine::frame::status::gone;
                response["body"] = json(nullptr);
                return response.dump();
            }

            string uid = js["uid"].get<string>();
            if (uid.empty()) {
                response["status"] = selfdefine::frame::status::bad_request;
                response["body"] = "MissCondition";
                return response.dump();
            }
            //布隆过滤器验证
            if(!redis_handler->BfExistByUID("ChatRoom","user",uid)){
                response["status"] = selfdefine::frame::status::unauthorized;
                response["body"] = "User Not Exists";
                return response.dump();
            }
            //尝试查询缓存
            string cache_key = "user:request:" + uid;
            uint64_t ttl = redis_handler->GenerateTTL(600000*5,600000*10); //缓存10min-15min较短时间
            string cache_value;
            json reqlist;
            if (redis_handler->GetValue(cache_key,cache_value,ttl)) {
                reqlist = json::parse(cache_value);
                if (reqlist.empty()) {
                    response["body"] = "Empty";
                    response["status"] = selfdefine::frame::status::not_found;
                    return response.dump();
                }
            }else {
                //获取好友请求列表
                reqlist["freq"] = RequestHandler::GetFriendRequestList(uid);
                //获取群组请求列表
                reqlist["greq"] = RequestHandler::GetGroupRequestList(uid);
                if (reqlist["freq"].empty() && reqlist["greq"].empty()) {
                    response["body"] = "Empty";
                    response["status"] = selfdefine::frame::status::not_found;
                    return response.dump();
                }
                //redis短时间缓存信息
                cache_value = reqlist.dump();
                redis_handler->CacheKey(cache_key,cache_value,ttl);
            }
            response["status"] = selfdefine::frame::status::ok;
            response["body"] = reqlist;
            return response.dump();
        }catch (exception &e) {
            json response;
            response["body"] = e.what();
            response["status"] = selfdefine::frame::status::bad_request;
            LOG_DEBUG<<e.what();
            return response.dump();
        }
    }

    ///获取群组成员列表
    /**
     *@param js["gid"] 群主ID
     *@return [(用户ID、用户名、角色、加入时间、上次活跃), ...]
     *
     *查询指定群组的成员列表
     */
    string GetGroupMemberList(websocketpp::server<websocketpp::config::asio_tls>& m_server,const websocketpp::connection_hdl &hdl, json &js) {
        try{
            json response;
            server::connection_ptr con = m_server.get_con_from_hdl(hdl);
            if (!con || con->get_state() != websocketpp::session::state::open) {
                response["status"] = selfdefine::frame::status::gone;
                response["body"] = json(nullptr);
                return response.dump();
            }

            string gid = js["gid"].get<string>();
            if (gid.empty()) {
                response["status"] = selfdefine::frame::status::bad_request;
                response["body"] = "MissCondition";
                return response.dump();
            }
            //布隆过滤器验证
            if(!redis_handler->BfExistByUID("ChatRoom","chatgroup",gid)){
                response["status"] = selfdefine::frame::status::unauthorized;
                response["body"] = "Group Not Exists";
                return response.dump();
            }
            //尝试查询缓存
            string cache_key = "group:members:" + gid;
            uint64_t ttl = redis_handler->GenerateTTL(600000*5,600000*10); //缓存10min-15min较短时间
            string cache_value;
            json reqlist;
            if (redis_handler->GetValue(cache_key,cache_value,ttl)) {
                reqlist = json::parse(cache_value);
                if (reqlist.empty()) {
                    response["body"] = "Empty";
                    response["status"] = selfdefine::frame::status::not_found;
                    return response.dump();
                }
            }else {
                //获取成员列表
                reqlist = ChatGroupHandler::getGroupMemberList(gid);
                if (reqlist.empty()) {
                    response["body"] = "Empty";
                    response["status"] = selfdefine::frame::status::not_found;
                    return response.dump();
                }
                //redis短时间缓存信息
                cache_value = reqlist.dump();
                redis_handler->CacheKey(cache_key,cache_value,ttl);
            }
            response["status"] = selfdefine::frame::status::ok;
            response["body"] = reqlist;
            return response.dump();
        }catch (exception &e) {
            json response;
            response["body"] = e.what();
            response["status"] = selfdefine::frame::status::bad_request;
            LOG_DEBUG<<e.what();
            return response.dump();
        }
    }

    ///搜索用户/群组
    /**
     *@param js["uid"] 用户ID
     *@param js["cond"] 搜索条件
     *@param js["type"] 条件类型: id、name
    *@return {
     *          user:[(用户ID、用户名),...],
     *          group:[(群组ID、群组名),...]
     *        }
     *
     *根据条件查询所有符合的用户或群组
     *
     */
    string ConditionSearch(websocketpp::server<websocketpp::config::asio_tls>& m_server,const websocketpp::connection_hdl &hdl, json &js) {
        try{
            json response;
            server::connection_ptr con = m_server.get_con_from_hdl(hdl);
            if (!con || con->get_state() != websocketpp::session::state::open) {
                response["status"] = selfdefine::frame::status::gone;
                response["body"] = json(nullptr);
                return response.dump();
            }
            string uid = js["uid"].get<string>();
            string cond_type = js["type"].get<string>();
            string cond = js["cond"].get<string>();
            if (uid.empty() || cond.empty() || cond_type.empty() || cond_type != "id" || cond_type != "name") {
                response["status"] = selfdefine::frame::status::bad_request;
                response["body"] = "Condition UnValid";
                return response.dump();
            }
            //布隆过滤器验证
            if(!redis_handler->BfExistByUID("ChatRoom","user",uid)){
                response["status"] = selfdefine::frame::status::unauthorized;
                response["body"] = "User Not Exists";
                return response.dump();
            }

            json results;
            //查询符合条件的好友
            results["user"] = UserHandler::ConditionSearch(cond,cond_type);
            //查询符合条件的群组
            results["group"] = ChatGroupHandler::ConditionSearch(cond,cond_type);
            if (results["user"].empty() && results["group"].empty()) {
                response["body"] = "Empty";
                response["status"] = selfdefine::frame::status::not_found;
                return response.dump();
            }
            response["status"] = selfdefine::frame::status::ok;
            response["body"] = results;
            return response.dump();
        }catch (exception &e) {
            json response;
            response["body"] = e.what();
            response["status"] = selfdefine::frame::status::bad_request;
            LOG_DEBUG<<e.what();
            return response.dump();
        }
    }


    ///发送好友申请
    /**
     *@param js["uid"] 用户ID
    *@param js["username"] 用户名称
    *@param js["tid"] 对方ID
    *@param js["tname"] 对方名称
    *@param js["msg"] 验证消息
    *@return 是否成功
     *
     *向对方发送带验证消息的好友申请，并向对方发送消息
     *（不立即删缓存，申请会先发送消息至对方，对方主动删除才会读取到脏数据，但删除不代表取消，一段时间后重新查询时会获取请求，无需一致性）
     *安全性检测：双方是否存在、是否已存在申请、是否已是好友、是否已达到双方好友上限（100）
     *需要使用分布式锁保证幂等性，还需要根据用户ID与对方ID，以一定规则组成key，解决双向并发问题
     */
    string sendFriendRequest(websocketpp::server<websocketpp::config::asio_tls>& m_server,const websocketpp::connection_hdl &hdl, json &js) {
        try{
            json response;
            server::connection_ptr con = m_server.get_con_from_hdl(hdl);
            if (!con || con->get_state() != websocketpp::session::state::open) {
                response["status"] = selfdefine::frame::status::gone;
                response["body"] = json(nullptr);
                return response.dump();
            }

            string fid = js["uid"].get<string>();
            string fname = js["username"].get<string>();
            string tid = js["tid"].get<string>();
            string tname = js["tname"].get<string>();
            string message = js["msg"].get<string>();
            if (fid.empty() || fname.empty() || tid.empty() || tname.empty()) {
                response["status"] = selfdefine::frame::status::bad_request;
                response["body"] = "Condition UnValid";
                return response.dump();
            }
            //布隆过滤器验证
            if(!redis_handler->BfExistByUID("ChatRoom","user",fid) || !redis_handler->BfExistByUID("ChatRoom","user",tid)){
                response["status"] = selfdefine::frame::status::unauthorized;
                response["body"] = "User Not Exists";
                return response.dump();
            }

            //分布式锁，根据min_id:max_id进行组合
            string pri_key;
            if (stoull(fid) > stoull(tid))
                pri_key = tid + ":" + fid;
            else
                pri_key = fid + ":" + tid;
            string cache_key = "lock:friend:request:"+pri_key;
            string cache_value = SSLService::generatorUUID();
            uint64_t ttl = redis_handler->GenerateTTL(30000,60000);
            distribute_lock lock(cache_key,cache_value,ttl>0?ttl:60000);
            if (!lock.is_success()) {
                //获取锁失败
                response["status"] = selfdefine::frame::status::conflict;
                response["body"] = "Request Has Exists";
                return response.dump();
            }


            //判断合法性
            auto is_valid = RequestHandler::CheckFriendRequestValid(fid,tid);
            if (!is_valid.first) {
                response["status"] = selfdefine::frame::status::unauthorized;
                response["body"] = is_valid.second;
                return response.dump();
            }


            //写入数据库
            auto req_id = Snowflake::GetInstance()->generateUniqueId();
            json data;
            data["fname"] = fname;
            data["tname"] = tname;
            if (!RequestHandler::AppendFriendRequest(to_string(req_id),fid,tid,message,data)) {
                response["status"] = selfdefine::frame::status::bad_request;
                response["body"] = "Request Failed";
                return response.dump();
            }
            //发送消息
            if (this->UserMap.count(tid)) {
                //对方处于同一服务器，直接转发
                auto& t_hdl = this->UserMap[tid];
                server::connection_ptr t_con = m_server.get_con_from_hdl(t_hdl);
                if (!t_con || t_con->get_state() != websocketpp::session::state::open) {
                    //放入离线消息
                    OfflineHandler::AppendOffline(fid,tid,0,js);
                    response["status"] = selfdefine::frame::status::ok;
                    response["body"] = "The other party's connection status is incorrect";
                    return response.dump();
                }
                websocketpp::lib::error_code ec;
                m_server.send(t_hdl,js.dump(),websocketpp::frame::opcode::TEXT,ec);
                if (ec) {
                    //放入离线消息
                    OfflineHandler::AppendOffline(fid,tid,0,js);
                    response["status"] = selfdefine::frame::status::ok;
                    response["body"] = ec.message();
                    return response.dump();
                }
            }else {
                //处于不同服务器或对方下线
                //查询redis对方服务器所在ID，未查询到代表对方离线，查询成功转发消息至对放所在服务器所在kafka
                string t_key = "local:" + tid;
                uint64_t t_ttl = redis_handler->GenerateTTL(3600000*24*5,3600000*24*7); //缓存10min-15min较短时间
                string t_value;
                json reqlist;
                if (!redis_handler->GetValue(t_key,t_value,t_ttl) || t_value.empty()) {
                    //查询失败/对方离线
                    //放入离线消息
                    OfflineHandler::AppendOffline(fid,tid,0,js);
                    response["status"] = selfdefine::frame::status::ok;
                    response["body"] = "The other party's connection status is incorrect";
                    return response.dump();
                }
                //转发消息至对方服务器，需要改变API避免对方服务器重复发送申请
                js["REL_API"] = js["API"];
                js["API"] = WS_API_TYPE::TRANSPROT_API;
                js["target_id"] = tid;
                js["uid"] = fid;
                js["msg_no"] = "0";
                this->kafka_producer->CommitMessage(t_value,js.dump());
            }
            response["status"] = selfdefine::frame::status::ok;
            response["body"] = "Send Success";
            return response.dump();
        }catch (exception &e) {
            json response;
            response["body"] = e.what();
            response["status"] = selfdefine::frame::status::bad_request;
            LOG_DEBUG<<e.what();
            return response.dump();
        }
    }


    ///发送入群申请
    /**
     *@param js["gid"] 群组ID
    *@param js["uid"] 用户ID
    *@param js["username"] 用户名称
    *@param js["msg"] 验证消息
    *@return 是否成功
     *
     *向对方发送带验证消息的入群申请，并向该群管理员以及群主发送消息
     *（不立即删缓存，申请会先发送消息至对方，对方主动删除才会读取到脏数据，但删除不代表取消，一段时间后重新查询时会获取请求，无需一致性）
     *安全性检测：双方是否存在、是否已存在申请、是否已入群、是否已达到入群上限或群人数上限（100）
     *需要使用分布式锁保证幂等性，根据用户ID与群组ID
     */
    string sendGroupRequest(websocketpp::server<websocketpp::config::asio_tls>& m_server,const websocketpp::connection_hdl &hdl, json &js) {
        try{
            json response;
            server::connection_ptr con = m_server.get_con_from_hdl(hdl);
            if (!con || con->get_state() != websocketpp::session::state::open) {
                response["status"] = selfdefine::frame::status::gone;
                response["body"] = json(nullptr);
                return response.dump();
            }

            string gid = js["gid"].get<string>();
            string uid = js["uid"].get<string>();
            string username = js["username"].get<string>();
            string message = js["msg"].get<string>();
            if (gid.empty() || uid.empty() || username.empty()) {
                response["status"] = selfdefine::frame::status::bad_request;
                response["body"] = "Condition UnValid";
                return response.dump();
            }
            //布隆过滤器验证
            if(!redis_handler->BfExistByUID("ChatRoom","chatgroup",gid) || !redis_handler->BfExistByUID("ChatRoom","user",uid)){
                response["status"] = selfdefine::frame::status::unauthorized;
                response["body"] = "Group or User Not Exists";
                return response.dump();
            }



            //分布式锁，根据min_id:max_id进行组合
            string cache_key = "lock:friend:request:"+gid+":"+uid;
            string cache_value = SSLService::generatorUUID();
            uint64_t ttl = redis_handler->GenerateTTL(30000,60000);
            distribute_lock lock(cache_key,cache_value,ttl>0?ttl:60000);
            if (!lock.is_success()) {
                //获取锁失败
                response["status"] = selfdefine::frame::status::conflict;
                response["body"] = "Request Has Exists";
                return response.dump();
            }

            //判断合法性
            auto is_valid = RequestHandler::CheckGroupRequestValid(gid,uid);
            if (!is_valid.first) {
                response["status"] = selfdefine::frame::status::unauthorized;
                response["body"] = is_valid.second;
                return response.dump();
            }

            //写入数据库
            auto req_id = Snowflake::GetInstance()->generateUniqueId();
            json data;
            data["name"] = username;
            if (!RequestHandler::AppendFriendRequest(to_string(req_id),gid,uid,message,data)) {
                response["status"] = selfdefine::frame::status::bad_request;
                response["body"] = "Request Failed";
                return response.dump();
            }
            //获取管理员与群主ID列表
            auto managers = ChatGroupHandler::GetGroupManagers(gid);
            for (string tid: managers) {
                //发送消息
                if (this->UserMap.count(tid)) {
                    //对方处于同一服务器，直接转发
                    auto& t_hdl = this->UserMap[tid];
                    server::connection_ptr t_con = m_server.get_con_from_hdl(t_hdl);
                    if (!t_con || t_con->get_state() != websocketpp::session::state::open) {
                        //放入离线消息
                        OfflineHandler::AppendOffline(uid,tid,0,js);
                        continue;
                    }
                    websocketpp::lib::error_code ec;
                    m_server.send(t_hdl,js.dump(),websocketpp::frame::opcode::TEXT,ec);
                    if (ec) {
                        //放入离线消息
                        OfflineHandler::AppendOffline(uid,tid,0,js);
                        continue;
                    }
                }else {
                    //处于不同服务器或对方下线
                    //查询redis对方服务器所在ID，未查询到代表对方离线，查询成功转发消息至对放所在服务器所在kafka
                    string t_key = "local:" + tid;
                    uint64_t t_ttl = redis_handler->GenerateTTL(3600000*24*5,3600000*24*7); //缓存10min-15min较短时间
                    string t_value;
                    json reqlist;
                    if (!redis_handler->GetValue(t_key,t_value,t_ttl) || t_value.empty()) {
                        //查询失败/对方离线
                        //放入离线消息
                        OfflineHandler::AppendOffline(uid,tid,0,js);
                        continue;
                    }
                    //转发消息至对方服务器，需要改变API避免对方服务器重复发送申请
                    js["REL_API"] = js["API"];
                    js["API"] = WS_API_TYPE::TRANSPROT_API;
                    js["target_id"] = tid;
                    js["uid"] = uid;
                    js["msg_no"] = "0";
                    this->kafka_producer->CommitMessage(t_value,js.dump());
                }
            }
            response["status"] = selfdefine::frame::status::ok;
            response["body"] = "Send Success";
            return response.dump();
        }catch (exception &e) {
            json response;
            response["body"] = e.what();
            response["status"] = selfdefine::frame::status::bad_request;
            LOG_DEBUG<<e.what();
            return response.dump();
        }
    }

    ///处理好友申请
    /**
     *@param js["req_id"] 请求ID
     *@param js["uid"] 用户ID
     *@param js["fid"] 发送者ID
    *@param js["accept"] 是否同意
    *@return 是否成功
     *
     *处理好友申请，只修改状态
     *删除相关缓存：user:friend:uid、user:request:uid
     *安全性检测：请求是否存在、请求状态是否已处理、是否已是好友、双方是否达到好友上限（100）
     *需要使用分布式锁保证幂等性，根据请求ID
     */
    string ProcessFriendRequest(websocketpp::server<websocketpp::config::asio_tls>& m_server,const websocketpp::connection_hdl &hdl, json &js) {
        try{
            json response;
            server::connection_ptr con = m_server.get_con_from_hdl(hdl);
            if (!con || con->get_state() != websocketpp::session::state::open) {
                response["status"] = selfdefine::frame::status::gone;
                response["body"] = json(nullptr);
                return response.dump();
            }

            string req_id = js["req_id"].get<string>();
            string uid = js["uid"].get<string>();
            string fid = js["fid"].get<string>();
            bool accept = js["accept"].get<bool>();
            if (req_id.empty() || uid.empty()) {
                response["status"] = selfdefine::frame::status::bad_request;
                response["body"] = "Condition UnValid";
                return response.dump();
            }
            //布隆过滤器验证
            if(!redis_handler->BfExistByUID("ChatRoom","user",uid) ||
                !redis_handler->BfExistByUID("ChatRoom","user",fid) ){
                response["status"] = selfdefine::frame::status::unauthorized;
                response["body"] = "UnKnow User";
                return response.dump();
            }


            //分布式锁，根据min_id:max_id进行组合
            string pri_key;
            if (stoull(fid) > stoull(uid))
                pri_key = uid + ":" + fid;
            else
                pri_key = fid + ":" + uid;
            string cache_key = "lock:friend:process:request:"+pri_key;
            string cache_value = SSLService::generatorUUID();
            uint64_t ttl = redis_handler->GenerateTTL(30000,60000);
            distribute_lock lock(cache_key,cache_value,ttl>0?ttl:60000);
            if (!lock.is_success()) {
                //获取锁失败
                response["status"] = selfdefine::frame::status::conflict;
                response["body"] = "Request Has Exists";
                return response.dump();
            }


            //判断合法性
            auto is_valid = RequestHandler::CheckFriendRequestProcessable(req_id,fid,uid);
            if (!is_valid.first) {
                response["status"] = selfdefine::frame::status::unauthorized;
                response["body"] = is_valid.second;
                return response.dump();
            }

            //修改数据库
            if (accept && !RequestHandler::AcquireFriendRequest(req_id,fid,uid)) {
                response["status"] = selfdefine::frame::status::bad_request;
                response["body"] = "Accept Failed";
                return response.dump();
            }else if (!accept && !RequestHandler::RejectFriendRequest(req_id)) {
                response["status"] = selfdefine::frame::status::bad_request;
                response["body"] = "Reject Failed";
                return response.dump();
            }
            //删除相关缓存: user:friend:uid、user:request:uid
            string keys = "user:friend:"+uid + " "+ "user:request:"+uid+ " "+ "user:friend:"+fid+ " "+ "user:request:"+fid;
            redis_handler->RemoveKey(keys);

            response["status"] = selfdefine::frame::status::ok;
            response["body"] = "Process Success";
            return response.dump();
        }catch (exception &e) {
            json response;
            response["body"] = e.what();
            response["status"] = selfdefine::frame::status::bad_request;
            LOG_DEBUG<<e.what();
            return response.dump();
        }
    }

    ///处理入群申请
    /**
     *@param js["req_id"] 请求ID
     *@param js["gid"] 群组ID
     *@param js["uid"] 用户ID
     *@param js["username"] 用户名称
     *@param js["accept"] 是否同意
     *@return 是否成功
     *
     *处理入群申请，只修改状态
     *删除相关缓存：user:group:uid、user:request:uid
     *安全性检测：请求是否存在、请求状态是否已处理、是否已是好友、双方是否达到好友上限（100）
     *需要使用分布式锁保证幂等性，根据请求ID
     */
    string ProcessGroupRequest(websocketpp::server<websocketpp::config::asio_tls>& m_server,const websocketpp::connection_hdl &hdl, json &js) {
        try{
            json response;
            server::connection_ptr con = m_server.get_con_from_hdl(hdl);
            if (!con || con->get_state() != websocketpp::session::state::open) {
                response["status"] = selfdefine::frame::status::gone;
                response["body"] = json(nullptr);
                return response.dump();
            }

            string req_id = js["req_id"].get<string>();
            string gid = js["gid"].get<string>();
            string uid = js["uid"].get<string>();
            string username = js["username"].get<string>();
            bool accept = js["accept"].get<bool>();
            if (req_id.empty() || gid.empty() || uid.empty() || username.empty()) {
                response["status"] = selfdefine::frame::status::bad_request;
                response["body"] = "Condition UnValid";
                return response.dump();
            }
            //布隆过滤器验证
            if(!redis_handler->BfExistByUID("ChatRoom","chatgroup",gid) ||
                !redis_handler->BfExistByUID("ChatRoom","user",uid) ){
                response["status"] = selfdefine::frame::status::unauthorized;
                response["body"] = "UnKnow User OR Group";
                return response.dump();
            }


            //分布式锁
            string cache_key = "lock:group:process:request:"+gid+":"+uid;
            string cache_value = SSLService::generatorUUID();
            uint64_t ttl = redis_handler->GenerateTTL(30000,60000);
            distribute_lock lock(cache_key,cache_value,ttl>0?ttl:60000);
            if (!lock.is_success()) {
                //获取锁失败
                response["status"] = selfdefine::frame::status::conflict;
                response["body"] = "Request Has Exists";
                return response.dump();
            }


            //判断合法性
            auto is_valid = RequestHandler::CheckGroupRequestProcessable(req_id,gid,uid);
            if (!is_valid.first) {
                response["status"] = selfdefine::frame::status::unauthorized;
                response["body"] = is_valid.second;
                return response.dump();
            }

            //修改数据库
            if (accept && !RequestHandler::AcquireGroupRequest(req_id,gid,uid,username)) {
                response["status"] = selfdefine::frame::status::bad_request;
                response["body"] = "Accept Failed";
                return response.dump();
            }else if (!accept && !RequestHandler::RejectGroupRequest(req_id)) {
                response["status"] = selfdefine::frame::status::bad_request;
                response["body"] = "Reject Failed";
                return response.dump();
            }
            //删除相关缓存: user:group:uid、user:request:uid
            string keys = "user:group:"+uid+" "+"user:request:"+uid;
            redis_handler->RemoveKey(keys);

            response["status"] = selfdefine::frame::status::ok;
            response["body"] = "Process Success";
            return response.dump();
        }catch (exception &e) {
            json response;
            response["body"] = e.what();
            response["status"] = selfdefine::frame::status::bad_request;
            LOG_DEBUG<<e.what();
            return response.dump();
        }
    }

    ///删除好友
    /**
     *@param js["uid"] 用户ID
     *@param js["fid"] 好友ID
     *@return 是否成功
     *
     *删除好友，包括离线消息、历史记录、好友申请
     *删除相关缓存：user:friend:uid、user:request:uid
     *安全性检测：用户是否存在、是否为好友
     *需要使用分布式锁保证幂等性，根据用户ID与好友ID
     */
    string DeleteFriend(websocketpp::server<websocketpp::config::asio_tls>& m_server,const websocketpp::connection_hdl &hdl, json &js) {
        try{
            json response;
            server::connection_ptr con = m_server.get_con_from_hdl(hdl);
            if (!con || con->get_state() != websocketpp::session::state::open) {
                response["status"] = selfdefine::frame::status::gone;
                response["body"] = json(nullptr);
                return response.dump();
            }

            string uid = js["uid"].get<string>();
            string fid = js["fid"].get<string>();
            if (fid.empty() || uid.empty()) {
                response["status"] = selfdefine::frame::status::bad_request;
                response["body"] = "Condition UnValid";
                return response.dump();
            }
            //布隆过滤器验证
            if(!redis_handler->BfExistByUID("ChatRoom","user",uid) ||
                !redis_handler->BfExistByUID("ChatRoom","user",fid) ){
                response["status"] = selfdefine::frame::status::unauthorized;
                response["body"] = "UnKnow User";
                return response.dump();
            }



            //分布式锁，根据min_id:max_id进行组合
            string pri_key;
            if (stoull(fid) > stoull(uid))
                pri_key = uid + ":" + fid;
            else
                pri_key = fid + ":" + uid;
            string cache_key = "lock:friend:delete:"+pri_key;
            string cache_value = SSLService::generatorUUID();
            uint64_t ttl = redis_handler->GenerateTTL(30000,60000);
            distribute_lock lock(cache_key,cache_value,ttl>0?ttl:60000);
            if (!lock.is_success()) {
                //获取锁失败
                response["status"] = selfdefine::frame::status::conflict;
                response["body"] = "Request Has Exists";
                return response.dump();
            }

            //判断合法性
            int is_friend = FriendHandler::is_Friend(uid,fid);
            if (is_friend == -1) {
                response["status"] = selfdefine::frame::status::service_unavailable;
                response["body"] = "Server Busy";
                return response.dump();
            }

            //修改数据库
            if (!FriendHandler::DelFriend(uid,fid)) {
                response["status"] = selfdefine::frame::status::bad_request;
                response["body"] = "Accept Failed";
                return response.dump();
            }
            //删除相关缓存: user:friend:uid、user:request:uid、user:histroy:uid:fid
            string keys = "user:friend:"+uid + " "+ "user:request:"+uid+ " "+ "user:friend:"+fid+ " "+
                "user:request:"+fid+ " "+ "user:histroy:"+uid+":"+fid+ " "+ "user:histroy:"+fid+":"+uid;
            redis_handler->RemoveKey(keys);
            response["status"] = selfdefine::frame::status::ok;
            response["body"] = "Process Success";
            return response.dump();
        }catch (exception &e) {
            json response;
            response["body"] = e.what();
            response["status"] = selfdefine::frame::status::bad_request;
            LOG_DEBUG<<e.what();
            return response.dump();
        }
    }

    ///删除群组
    /**
     *@param js["uid"] 用户ID
     *@param js["gid"] 群组ID
     *@return 是否成功
     *
     *根据用户角色触发不同操作：
     *      普通成员与管理员：移出成员表，删除与该群的离线消息、历史记录
     *      群主： 删除群聊，删除所有群成员，删除该群所有离线消息，删除该群所有历史记录，删除该群所有入群申请
     *删除相关缓存：user:group:uid、user:request:uid、user:histroy:uid:fid
     *安全性检测：用户和群组是否存在
     *需要使用分布式锁保证幂等性
     */
    string DeleteGroup(websocketpp::server<websocketpp::config::asio_tls>& m_server,const websocketpp::connection_hdl &hdl, json &js) {
        try{
            json response;
            server::connection_ptr con = m_server.get_con_from_hdl(hdl);
            if (!con || con->get_state() != websocketpp::session::state::open) {
                response["status"] = selfdefine::frame::status::gone;
                response["body"] = json(nullptr);
                return response.dump();
            }

            string uid = js["uid"].get<string>();
            string gid = js["gid"].get<string>();
            if (gid.empty() || uid.empty()) {
                response["status"] = selfdefine::frame::status::bad_request;
                response["body"] = "Condition UnValid";
                return response.dump();
            }
            //布隆过滤器验证
            if(!redis_handler->BfExistByUID("ChatRoom","chatgroup",gid) ||
                !redis_handler->BfExistByUID("ChatRoom","user",uid) ){
                response["status"] = selfdefine::frame::status::unauthorized;
                response["body"] = "UnKnow User OR Group";
                return response.dump();
                }

            // 获取用户权限: 0 - 成员、1 - 管理员、 2 - 群主
            int role = ChatGroupHandler::getUserRole(gid,uid);
            if (role == -1) {
                response["status"] = selfdefine::frame::status::service_unavailable;
                response["body"] = "Get User Role Failed";
                return response.dump();
            }

            //分布式锁
            string cache_key = "lock:group:delete:"+uid+":"+gid;
            string cache_value = SSLService::generatorUUID();
            uint64_t ttl = redis_handler->GenerateTTL(30000,60000);
            distribute_lock lock(cache_key,cache_value,ttl>0?ttl:60000);
            if (!lock.is_success()) {
                //获取锁失败
                response["status"] = selfdefine::frame::status::conflict;
                response["body"] = "Request Has Exists";
                return response.dump();
            }

            //修改数据库
            if (role != 2 && !ChatGroupHandler::QuitGroup(gid,uid)) {
                response["status"] = selfdefine::frame::status::bad_request;
                response["body"] = "Quit Failed";
                return response.dump();
            }else if (role == 2 && !ChatGroupHandler::DestoryGroup(gid)) {
                response["status"] = selfdefine::frame::status::bad_request;
                response["body"] = "Destroy Failed";
                return response.dump();
            }
            //删除相关缓存: user:group:uid、user:request:uid、user:histroy:uid:fid
            string keys = "user:group:"+uid + " "+ "user:request:"+uid+" "+ "user:histroy:"+uid+":"+gid;
            redis_handler->RemoveKey(keys);
            response["status"] = selfdefine::frame::status::ok;
            response["body"] = "Process Success";
            return response.dump();
        }catch (exception &e) {
            json response;
            response["body"] = e.what();
            response["status"] = selfdefine::frame::status::bad_request;
            LOG_DEBUG<<e.what();
            return response.dump();
        }
    }

    ///创建群组
    /**
     *@param js["uid"] 用户ID
     *@param js["username"] 用户名称
     *@param js["gname"] 群组名称
     *@param js["desc"] 群组介绍
     *@return gid
     *
     *创建群组，并添加用户至群组表设置权限为群主，添加布隆过滤器
     *安全性检测：用户是否存在、用户创建群组是否达到上限
     *需要使用分布式锁保证幂等性
     */
    string CreateGroup(websocketpp::server<websocketpp::config::asio_tls>& m_server,const websocketpp::connection_hdl &hdl, json &js) {
        try{
            json response;
            server::connection_ptr con = m_server.get_con_from_hdl(hdl);
            if (!con || con->get_state() != websocketpp::session::state::open) {
                response["status"] = selfdefine::frame::status::gone;
                response["body"] = json(nullptr);
                return response.dump();
            }

            string uid = js["uid"].get<string>();
            string username = js["username"].get<string>();
            string gname = js["gname"].get<string>();
            string desc = js["desc"].get<string>();
            if (username.empty() || uid.empty() || gname.empty()) {
                response["status"] = selfdefine::frame::status::bad_request;
                response["body"] = "Condition UnValid";
                return response.dump();
            }
            //布隆过滤器验证
            if ( !redis_handler->BfExistByUID("ChatRoom","user",uid) ){
                response["status"] = selfdefine::frame::status::unauthorized;
                response["body"] = "UnKnow User";
                return response.dump();
            }

            //分布式锁
            string cache_key = "lock:group:create:"+uid;
            string cache_value = SSLService::generatorUUID();
            uint64_t ttl = redis_handler->GenerateTTL(30000,60000);
            distribute_lock lock(cache_key,cache_value,ttl>0?ttl:60000);
            if (!lock.is_success()) {
                //获取锁失败
                response["status"] = selfdefine::frame::status::conflict;
                response["body"] = "Request Has Exists";
                return response.dump();
            }

            // 获取用户所加入群组数
            int group_num = UserHandler::userGroupNum(uid);
            if (group_num == -1) {
                response["status"] = selfdefine::frame::status::service_unavailable;
                response["body"] = "Get User Join Group Count Failed";
                return response.dump();
            }
            if (group_num >= 100) {
                response["status"] = selfdefine::frame::status::bad_request;
                response["body"] = "The user has reached the maximum number of groups";
                return response.dump();
            }
            uint64_t gid = Snowflake::GetInstance()->generateUniqueId();
            json data;
            data["name"] = username;
            data["desc"] = desc;
            if (!ChatGroupHandler::CreateGroup(to_string(gid),gname,uid,username,data.dump())) {
                response["status"] = selfdefine::frame::status::bad_request;
                response["body"] = "Create Group Failed";
                return response.dump();;
            }
            //添加布隆过滤器
            if (!DBCache::GetInstance()->bm_add(to_string(gid))) {
                ChatGroupHandler::DestoryGroup(to_string(gid));
                response["status"] = selfdefine::frame::status::bad_request;
                response["body"] = "Bloom Cache Failed";
                return response.dump();;
            }
            response["status"] = selfdefine::frame::status::ok;
            response["body"] = to_string(gid);
            return response.dump();
        }catch (exception &e) {
            json response;
            response["body"] = e.what();
            response["status"] = selfdefine::frame::status::bad_request;
            LOG_DEBUG<<e.what();
            return response.dump();
        }
    }

    ///踢出成员
    /**
     *@param js["uid"] 用户ID
     *@param js["tid"] 对方ID
     *@param js["gid"] 群组ID
     *@return 是否成功
     *
     *比较双方权限等级，只有自身权限等级比对方大时才能踢出
     *安全性检测：用户和群组是否存在，权限等级是否符合
     *需要使用分布式锁保证幂等性
     */
    string kickOutMember(websocketpp::server<websocketpp::config::asio_tls>& m_server,const websocketpp::connection_hdl &hdl, json &js) {
        try{
            json response;
            server::connection_ptr con = m_server.get_con_from_hdl(hdl);
            if (!con || con->get_state() != websocketpp::session::state::open) {
                response["status"] = selfdefine::frame::status::gone;
                response["body"] = json(nullptr);
                return response.dump();
            }

            string uid = js["uid"].get<string>();
            string gid = js["gid"].get<string>();
            string tid = js["tid"].get<string>();
            if (gid.empty() || uid.empty() || tid.empty()) {
                response["status"] = selfdefine::frame::status::bad_request;
                response["body"] = "Condition UnValid";
                return response.dump();
            }
            //布隆过滤器验证
            if(!redis_handler->BfExistByUID("ChatRoom","chatgroup",gid) ||
                !redis_handler->BfExistByUID("ChatRoom","user",uid) ||
                !redis_handler->BfExistByUID("ChatRoom","user",tid)){
                response["status"] = selfdefine::frame::status::unauthorized;
                response["body"] = "UnKnow User OR Group";
                return response.dump();
                }

            // 获取用户权限: 0 - 成员、1 - 管理员、 2 - 群主
            int role = ChatGroupHandler::getUserRole(gid,uid);
            int t_role = ChatGroupHandler::getUserRole(gid,tid);
            if (role == -1 || t_role == -1) {
                response["status"] = selfdefine::frame::status::service_unavailable;
                response["body"] = "Get User Role Failed";
                return response.dump();
            }
            if (role <= t_role) {
                response["status"] = selfdefine::frame::status::bad_request;
                response["body"] = "Insufficient permissions";
                return response.dump();
            }

            //分布式锁
            string cache_key = "lock:group:kickout:"+uid+":"+tid+":"+gid;
            string cache_value = SSLService::generatorUUID();
            uint64_t ttl = redis_handler->GenerateTTL(30000,60000);
            distribute_lock lock(cache_key,cache_value,ttl>0?ttl:60000);
            if (!lock.is_success()) {
                //获取锁失败
                response["status"] = selfdefine::frame::status::conflict;
                response["body"] = "Request Has Exists";
                return response.dump();
            }

            //修改数据库
            if (!ChatGroupHandler::QuitGroup(gid,tid)) {
                response["status"] = selfdefine::frame::status::bad_request;
                response["body"] = "Kick Out Failed";
                return response.dump();
            }
            //删除相关缓存: user:group:uid、user:request:uid、user:histroy:uid:fid
            string keys = "user:group:"+tid + " "+ "user:request:"+tid+" "+ "user:histroy:"+tid+":"+gid;
            redis_handler->RemoveKey(keys);
            response["status"] = selfdefine::frame::status::ok;
            response["body"] = "Kick Out Success";
            return response.dump();
        }catch (exception &e) {
            json response;
            response["body"] = e.what();
            response["status"] = selfdefine::frame::status::bad_request;
            LOG_DEBUG<<e.what();
            return response.dump();
        }
    }

    ///提升权限
    /**
     *@param js["uid"] 用户ID
     *@param js["tid"] 对方ID
     *@param js["gid"] 群组ID
     *@return 是否成功
     *
     *比较双方权限等级，只有自身权限为群主且对方权限等级为成员才进行提升
     *安全性检测：用户和群组是否存在，权限等级是否符合
     *需要使用分布式锁保证幂等性
     */
    string UpGroupMemberRole(websocketpp::server<websocketpp::config::asio_tls>& m_server,const websocketpp::connection_hdl &hdl, json &js) {
        try{
            json response;
            server::connection_ptr con = m_server.get_con_from_hdl(hdl);
            if (!con || con->get_state() != websocketpp::session::state::open) {
                response["status"] = selfdefine::frame::status::gone;
                response["body"] = json(nullptr);
                return response.dump();
            }

            string uid = js["uid"].get<string>();
            string gid = js["gid"].get<string>();
            string tid = js["tid"].get<string>();
            if (gid.empty() || uid.empty() || tid.empty()) {
                response["status"] = selfdefine::frame::status::bad_request;
                response["body"] = "Condition UnValid";
                return response.dump();
            }
            //布隆过滤器验证
            if(!redis_handler->BfExistByUID("ChatRoom","chatgroup",gid) ||
                !redis_handler->BfExistByUID("ChatRoom","user",uid) ||
                !redis_handler->BfExistByUID("ChatRoom","user",tid)){
                response["status"] = selfdefine::frame::status::unauthorized;
                response["body"] = "UnKnow User OR Group";
                return response.dump();
            }

            // 获取用户权限: 0 - 成员、1 - 管理员、 2 - 群主
            int role = ChatGroupHandler::getUserRole(gid,uid);
            int t_role = ChatGroupHandler::getUserRole(gid,tid);
            if (role == -1 || t_role == -1) {
                response["status"] = selfdefine::frame::status::service_unavailable;
                response["body"] = "Get User Role Failed";
                return response.dump();
            }
            if (role != 2 || t_role != 0) {
                response["status"] = selfdefine::frame::status::bad_request;
                response["body"] = "Permission denied";
                return response.dump();
            }

            //分布式锁
            string cache_key = "lock:group:uprole:"+tid+":"+gid;
            string cache_value = SSLService::generatorUUID();
            uint64_t ttl = redis_handler->GenerateTTL(30000,60000);
            distribute_lock lock(cache_key,cache_value,ttl>0?ttl:60000);
            if (!lock.is_success()) {
                //获取锁失败
                response["status"] = selfdefine::frame::status::conflict;
                response["body"] = "Request Has Exists";
                return response.dump();
            }

            //修改数据库
            if (!ChatGroupHandler::SetMemberRole(gid,tid,1)) {
                response["status"] = selfdefine::frame::status::bad_request;
                response["body"] = "Improve Member Permission Failed";
                return response.dump();
            }
            response["status"] = selfdefine::frame::status::ok;
            response["body"] = "Improve Member Permission Success";
            return response.dump();
        }catch (exception &e) {
            json response;
            response["body"] = e.what();
            response["status"] = selfdefine::frame::status::bad_request;
            LOG_DEBUG<<e.what();
            return response.dump();
        }
    }

    ///撤销权限
    /**
     *@param js["uid"] 用户ID
     *@param js["tid"] 对方ID
     *@param js["gid"] 群组ID
     *@return 是否成功
     *
     *比较双方权限等级，只有自身权限为群主且对方权限等级为管理员才进行撤销
     *安全性检测：用户和群组是否存在，权限等级是否符合
     *需要使用分布式锁保证幂等性
     */
     string RevokeGroupMemberRole(websocketpp::server<websocketpp::config::asio_tls>& m_server,const websocketpp::connection_hdl &hdl, json &js) {
        try{
            json response;
            server::connection_ptr con = m_server.get_con_from_hdl(hdl);
            if (!con || con->get_state() != websocketpp::session::state::open) {
                response["status"] = selfdefine::frame::status::gone;
                response["body"] = json(nullptr);
                return response.dump();
            }

            string uid = js["uid"].get<string>();
            string gid = js["gid"].get<string>();
            string tid = js["tid"].get<string>();
            if (gid.empty() || uid.empty() || tid.empty()) {
                response["status"] = selfdefine::frame::status::bad_request;
                response["body"] = "Condition UnValid";
                return response.dump();
            }
            //布隆过滤器验证
            if(!redis_handler->BfExistByUID("ChatRoom","chatgroup",gid) ||
                !redis_handler->BfExistByUID("ChatRoom","user",uid) ||
                !redis_handler->BfExistByUID("ChatRoom","user",tid)){
                response["status"] = selfdefine::frame::status::unauthorized;
                response["body"] = "UnKnow User OR Group";
                return response.dump();
            }

            // 获取用户权限: 0 - 成员、1 - 管理员、 2 - 群主
            int role = ChatGroupHandler::getUserRole(gid,uid);
            int t_role = ChatGroupHandler::getUserRole(gid,tid);
            if (role == -1 || t_role == -1) {
                response["status"] = selfdefine::frame::status::service_unavailable;
                response["body"] = "Get User Role Failed";
                return response.dump();
            }
            if (role != 2 || t_role != 1) {
                response["status"] = selfdefine::frame::status::bad_request;
                response["body"] = "Permission denied";
                return response.dump();
            }

            //分布式锁
            string cache_key = "lock:group:revoke:"+tid+":"+gid;
            string cache_value = SSLService::generatorUUID();
            uint64_t ttl = redis_handler->GenerateTTL(30000,60000);
            distribute_lock lock(cache_key,cache_value,ttl>0?ttl:60000);
            if (!lock.is_success()) {
                //获取锁失败
                response["status"] = selfdefine::frame::status::conflict;
                response["body"] = "Request Has Exists";
                return response.dump();
            }

            //修改数据库
            if (!ChatGroupHandler::SetMemberRole(gid,tid,0)) {
                response["status"] = selfdefine::frame::status::bad_request;
                response["body"] = "Revoke Member Permission Failed";
                return response.dump();
            }
            response["status"] = selfdefine::frame::status::ok;
            response["body"] = "Revoke Member Permission Success";
            return response.dump();
        }catch (exception &e) {
            json response;
            response["body"] = e.what();
            response["status"] = selfdefine::frame::status::bad_request;
            LOG_DEBUG<<e.what();
            return response.dump();
        }
     }

    ///单聊
    /**
     *@param js["uid"] 用户ID
     *@param js["fid"] 对方ID
     *@param js["msg_no"] 消息序号
     *@param js["message"] 消息
    *@return 是否成功
     *
     *转发消息
     *安全性检测：判断双方是否存在
     *需要使用分布式锁保证幂等性，根据请求ID、对方ID、消息ID
     */
    string chat(websocketpp::server<websocketpp::config::asio_tls>& m_server,const websocketpp::connection_hdl &hdl, json &js) {
        try{
            json response;
            server::connection_ptr con = m_server.get_con_from_hdl(hdl);
            if (!con || con->get_state() != websocketpp::session::state::open) {
                response["status"] = selfdefine::frame::status::gone;
                response["body"] = json(nullptr);
                return response.dump();
            }

            string uid = js["uid"].get<string>();
            string fid = js["fid"].get<string>();
            string msg_no = js["msg_no"].get<string>();
            string message = js["message"].get<string>();
            if (uid.empty() || fid.empty() || msg_no.empty()) {
                response["status"] = selfdefine::frame::status::bad_request;
                response["body"] = "Condition UnValid";
                return response.dump();
            }
            //布隆过滤器验证
            if(!redis_handler->BfExistByUID("ChatRoom","user",uid) ||
                !redis_handler->BfExistByUID("ChatRoom","user",fid)){
                response["status"] = selfdefine::frame::status::unauthorized;
                response["body"] = "User Not Exists";
                return response.dump();
            }

            //分布式锁，根据min_id:max_id进行组合
            string cache_key = "lock:user:chat:"+uid+":"+fid+":"+msg_no;
            string cache_value = SSLService::generatorUUID();
            uint64_t ttl = redis_handler->GenerateTTL(30000,60000);
            distribute_lock lock(cache_key,cache_value,ttl>0?ttl:60000);
            if (!lock.is_success()) {
                //获取锁失败
                response["status"] = selfdefine::frame::status::conflict;
                response["body"] = "Repeat Chat Request";
                return response.dump();
            }

            //发送消息
            if (this->UserMap.count(fid)) {
                //对方处于同一服务器，直接转发
                auto& t_hdl = this->UserMap[fid];
                server::connection_ptr t_con = m_server.get_con_from_hdl(t_hdl);
                if (!t_con || t_con->get_state() != websocketpp::session::state::open) {
                    //放入离线消息
                    OfflineHandler::AppendOffline(uid,fid,stoull(msg_no),js);
                    //更新用户最近会话记录
                    ttl = redis_handler->GenerateTTL(3600000*24*5,3600000*24*7);
                    redis_handler->UpdateRSL(uid,fid,js.dump(),ttl > 0?ttl:3600000*24*7);
                    redis_handler->UpdateRSL(fid,uid,js.dump(),ttl > 0?ttl:3600000*24*7);
                    response["status"] = selfdefine::frame::status::ok;
                    response["body"] = "The other party's connection status is incorrect";
                    return response.dump();
                }
                websocketpp::lib::error_code ec;
                m_server.send(t_hdl,js.dump(),websocketpp::frame::opcode::TEXT,ec);
                if (ec) {
                    //放入离线消息
                    OfflineHandler::AppendOffline(uid,fid,stoull(msg_no),js);
                    //更新用户最近会话记录
                    ttl = redis_handler->GenerateTTL(3600000*24*5,3600000*24*7);
                    redis_handler->UpdateRSL(uid,fid,js.dump(),ttl > 0?ttl:3600000*24*7);
                    redis_handler->UpdateRSL(fid,uid,js.dump(),ttl > 0?ttl:3600000*24*7);
                    response["status"] = selfdefine::frame::status::ok;
                    response["body"] = ec.message();
                    return response.dump();
                }
            }else {
                //处于不同服务器或对方下线
                //查询redis对方服务器所在ID，未查询到代表对方离线，查询成功转发消息至对放所在服务器所在kafka
                string t_key = "local:" + fid;
                uint64_t t_ttl = redis_handler->GenerateTTL(3600000*24*5,3600000*24*7);
                string t_value;
                json reqlist;
                if (!redis_handler->GetValue(t_key,t_value,t_ttl) || t_value.empty()) {
                    //查询失败/对方离线
                    //放入离线消息
                    OfflineHandler::AppendOffline(uid,fid,stoull(msg_no),js);
                    //更新用户最近会话记录
                    ttl = redis_handler->GenerateTTL(3600000*24*5,3600000*24*7);
                    redis_handler->UpdateRSL(uid,fid,js.dump(),ttl > 0?ttl:3600000*24*7);
                    redis_handler->UpdateRSL(fid,uid,js.dump(),ttl > 0?ttl:3600000*24*7);
                    response["status"] = selfdefine::frame::status::ok;
                    response["body"] = "The other party's connection status is incorrect";
                    return response.dump();
                }
                //转发消息至对方服务器，需要改变API避免对方服务器重复发送申请
                js["REL_API"] = js["API"];
                js["API"] = WS_API_TYPE::TRANSPROT_API;
                js["target_id"] = fid;
                js["uid"] = uid;
                js["msg_no"] = msg_no;
                this->kafka_producer->CommitMessage(t_value,js.dump());
            }
            //更新用户最近会话记录
            ttl = redis_handler->GenerateTTL(3600000*24*5,3600000*24*7);
            redis_handler->UpdateRSL(uid,fid,js.dump(),ttl > 0?ttl:3600000*24*7);
            redis_handler->UpdateRSL(fid,uid,js.dump(),ttl > 0?ttl:3600000*24*7);
            response["status"] = selfdefine::frame::status::ok;
            response["body"] = "Send Success";
            return response.dump();
        }catch (exception &e) {
            json response;
            response["body"] = e.what();
            response["status"] = selfdefine::frame::status::bad_request;
            LOG_DEBUG<<e.what();
            return response.dump();
        }
    }

    ///转发操作消息
    /**
     *@param js["API"] 当前API
     *@param js["REL_API"] 原API
     *@param js["target_id"] 转发目标ID
     *@param js["uid"] 用户ID
     *@param js["msg_no"] 消息序号
     *@return 是否成功
     *
     *转发操作消息至客户端处理，不额外转发避免嵌套
     */
    string transport(websocketpp::server<websocketpp::config::asio_tls>& m_server,const websocketpp::connection_hdl &hdl, json &js) {
        try{
            json response;
            server::connection_ptr con = m_server.get_con_from_hdl(hdl);
            if (!con || con->get_state() != websocketpp::session::state::open) {
                response["status"] = selfdefine::frame::status::gone;
                response["body"] = json(nullptr);
                return response.dump();
            }
            string uid = js["uid"].get<string>();
            string target_id = js["target_id"].get<string>();
            string msg_no = js["msg_no"].get<string>();
            if (uid.empty() || target_id.empty() || msg_no.empty()) {
                response["status"] = selfdefine::frame::status::bad_request;
                response["body"] = "Condition UnValid";
                return response.dump();
            }
            //布隆过滤器验证
            if(!redis_handler->BfExistByUID("ChatRoom","user",uid) ||
                !redis_handler->BfExistByUID("ChatRoom","user",target_id)){
                response["status"] = selfdefine::frame::status::unauthorized;
                response["body"] = "User Not Exists";
                return response.dump();
            }

            //发送消息
            if (this->UserMap.count(target_id)) {
                //对方处于同一服务器，直接转发
                auto& t_hdl = this->UserMap[target_id];
                server::connection_ptr t_con = m_server.get_con_from_hdl(t_hdl);
                if (!t_con || t_con->get_state() != websocketpp::session::state::open) {
                    OfflineHandler::AppendOffline(uid,target_id,stoull(msg_no),js);
                    response["status"] = selfdefine::frame::status::ok;
                    response["body"] = "The other party's connection status is incorrect";
                    return response.dump();
                }
                websocketpp::lib::error_code ec;
                m_server.send(t_hdl,js.dump(),websocketpp::frame::opcode::TEXT,ec);
                if (ec) {
                    OfflineHandler::AppendOffline(uid,target_id,stoull(msg_no),js);
                    response["status"] = selfdefine::frame::status::ok;
                    response["body"] = ec.message();
                    return response.dump();
                }
            }
            response["status"] = selfdefine::frame::status::ok;
            response["body"] = "Send Success";
            return response.dump();
        }catch (exception &e) {
            json response;
            response["body"] = e.what();
            response["status"] = selfdefine::frame::status::bad_request;
            LOG_DEBUG<<e.what();
            return response.dump();
        }
    }

    ///群聊
    /**
     *@param js["uid"] 用户ID
     *@param js["gid"] 群组ID
     *@param js["message"] 消息
     *@return 是否成功
     *
     *广播群聊消息，不依赖用户的消息ID，改为服务端生成全局单调递增ID
     *安全性检测：判断双方是否存在
     *需要使用分布式锁保证幂等性，根据群组ID、用户ID
     */
    string GroupChat(websocketpp::server<websocketpp::config::asio_tls>& m_server,const websocketpp::connection_hdl &hdl, json &js) {
        try{
            json response;
            server::connection_ptr con = m_server.get_con_from_hdl(hdl);
            if (!con || con->get_state() != websocketpp::session::state::open) {
                response["status"] = selfdefine::frame::status::gone;
                response["body"] = json(nullptr);
                return response.dump();
            }

            string uid = js["uid"].get<string>();
            string gid = js["gid"].get<string>();
            string message = js["message"].get<string>();
            if (uid.empty() || gid.empty()) {
                response["status"] = selfdefine::frame::status::bad_request;
                response["body"] = "Condition UnValid";
                return response.dump();
            }
            //布隆过滤器验证
            if(!redis_handler->BfExistByUID("ChatRoom","user",uid) ||
                !redis_handler->BfExistByUID("ChatRoom","chatgroup",gid)){
                response["status"] = selfdefine::frame::status::unauthorized;
                response["body"] = "User Or Group is Not Exists";
                return response.dump();
            }

            //分布式锁
            string cache_key = "lock:group:chat:"+uid+":"+gid;
            string cache_value = SSLService::generatorUUID();
            uint64_t ttl = redis_handler->GenerateTTL(30000,60000);
            distribute_lock lock(cache_key,cache_value,ttl>0?ttl:60000);
            if (!lock.is_success()) {
                //获取锁失败
                response["status"] = selfdefine::frame::status::conflict;
                response["body"] = "Repeat Chat Request";
                return response.dump();
            }
            uint64_t msg_no = Snowflake::GetInstance2()->generateUniqueId();
            //发送消息
            auto members = ChatGroupHandler::getGroupMemberIDList(gid);
            for (string tid: members) {
                //发送消息
                if (tid == uid) {continue;}
                if (this->UserMap.count(tid)) {
                    //对方处于同一服务器，直接转发
                    auto& t_hdl = this->UserMap[tid];
                    server::connection_ptr t_con = m_server.get_con_from_hdl(t_hdl);
                    if (!t_con || t_con->get_state() != websocketpp::session::state::open) {
                        //放入离线消息
                        OfflineHandler::AppendOffline(uid,tid,msg_no,js);
                        //需要更新成员最近会话记录
                        ttl = redis_handler->GenerateTTL(3600000*24*5,3600000*24*7);
                        redis_handler->UpdateRSL(tid,gid,js.dump(),ttl > 0?ttl:3600000*24*7);
                        continue;
                    }
                    websocketpp::lib::error_code ec;
                    m_server.send(t_hdl,js.dump(),websocketpp::frame::opcode::TEXT,ec);
                    if (ec) {
                        //放入离线消息
                        OfflineHandler::AppendOffline(uid,tid,msg_no,js);
                        //需要更新成员最近会话记录
                        ttl = redis_handler->GenerateTTL(3600000*24*5,3600000*24*7);
                        redis_handler->UpdateRSL(tid,gid,js.dump(),ttl > 0?ttl:3600000*24*7);
                        continue;
                    }
                }else {
                    //处于不同服务器或对方下线
                    //查询redis对方服务器所在ID，未查询到代表对方离线，查询成功转发消息至对放所在服务器所在kafka
                    string t_key = "local:" + tid;
                    uint64_t t_ttl = redis_handler->GenerateTTL(3600000*24*5,3600000*24*7); //缓存10min-15min较短时间
                    string t_value;
                    json reqlist;
                    if (!redis_handler->GetValue(t_key,t_value,t_ttl) || t_value.empty()) {
                        //查询失败/对方离线
                        //放入离线消息
                        OfflineHandler::AppendOffline(uid,tid,msg_no,js);
                        //需要更新成员最近会话记录
                        ttl = redis_handler->GenerateTTL(3600000*24*5,3600000*24*7);
                        redis_handler->UpdateRSL(tid,gid,js.dump(),ttl > 0?ttl:3600000*24*7);
                        continue;
                    }
                    //转发消息至对方服务器，需要改变API避免对方服务器重复发送申请
                    js["REL_API"] = js["API"];
                    js["API"] = WS_API_TYPE::TRANSPROT_API;
                    js["target_id"] = tid;
                    js["uid"] = uid;
                    js["msg_no"] = to_string(msg_no);
                    this->kafka_producer->CommitMessage(t_value,js.dump());
                }
                //需要更新成员最近会话记录
                ttl = redis_handler->GenerateTTL(3600000*24*5,3600000*24*7);
                redis_handler->UpdateRSL(tid,gid,js.dump(),ttl > 0?ttl:3600000*24*7);
            }
            //更新用户最近会话记录
            ttl = redis_handler->GenerateTTL(3600000*24*5,3600000*24*7);
            redis_handler->UpdateRSL(uid,gid,js.dump(),ttl > 0?ttl:3600000*24*7);
            response["status"] = selfdefine::frame::status::ok;
            response["body"] = "Send Success";
            return response.dump();
        }catch (exception &e) {
            json response;
            response["body"] = e.what();
            response["status"] = selfdefine::frame::status::bad_request;
            LOG_DEBUG<<e.what();
            return response.dump();
        }
    }

    ///备份历史记录
    /**
     *@param js["uid"] 用户ID
     *@param js["session_id"] 会话ID
     *@param js["history"] 消息历史
     *@return 是否成功
     *
     *保存历史记录至数据库，限制历史消息最大1000条，超出截取
     */
    string SaveHistroy(websocketpp::server<websocketpp::config::asio_tls>& m_server,const websocketpp::connection_hdl &hdl, json &js) {
        try{
            json response;
            server::connection_ptr con = m_server.get_con_from_hdl(hdl);
            if (!con || con->get_state() != websocketpp::session::state::open) {
                response["status"] = selfdefine::frame::status::gone;
                response["body"] = json(nullptr);
                return response.dump();
            }

            string uid = js["uid"].get<string>();
            string session_id = js["session_id"].get<string>();
            string history = js["history"].get<string>();
            if (uid.empty() || session_id.empty()) {
                response["status"] = selfdefine::frame::status::bad_request;
                response["body"] = "Condition UnValid";
                return response.dump();
            }
            //布隆过滤器验证
            if(!redis_handler->BfExistByUID("ChatRoom","user",uid)){
                response["status"] = selfdefine::frame::status::unauthorized;
                response["body"] = "User is Not Exists";
                return response.dump();
            }

            {
                json his = json::parse(history);
                if (his.size() > 1000) {
                    int diff = his.size() - 1000;
                    his.erase(his.begin(),his.begin()+diff);
                    history = his.dump();
                }
            }
            uint64_t size_kb = (sizeof(history) + history.capacity())/1024;

            //分布式锁
            string cache_key = "lock:user:histroy:"+uid+":"+session_id;
            string cache_value = SSLService::generatorUUID();
            uint64_t ttl = redis_handler->GenerateTTL(30000,60000);
            distribute_lock lock(cache_key,cache_value,ttl>0?ttl:60000);
            if (!lock.is_success()) {
                //获取锁失败
                response["status"] = selfdefine::frame::status::conflict;
                response["body"] = "Repeat Save Request";
                return response.dump();
            }
            //写数据库
            HistoryHandler::save(uid,session_id,size_kb,history);
            cache_key = "user:histroy:"+uid+":"+session_id;
            cache_value = history;
            ttl = redis_handler->GenerateTTL(600000*5,600000*10);
            redis_handler->CacheKey(cache_key,cache_value,ttl);
            response["status"] = selfdefine::frame::status::ok;
            response["body"] = "Save Success";
            return response.dump();
        }catch (exception &e) {
            json response;
            response["body"] = e.what();
            response["status"] = selfdefine::frame::status::bad_request;
            LOG_DEBUG<<e.what();
            return response.dump();
        }
    }


    ///获取历史记录
    /**
     *@param js["uid"] 用户ID
     *@param js["session_id"] 会话ID
     *@return 历史记录
     *
     *获取最大1000条历史数据
     */
    string GetHistroy(websocketpp::server<websocketpp::config::asio_tls>& m_server,const websocketpp::connection_hdl &hdl, json &js) {
        try{
            json response;
            server::connection_ptr con = m_server.get_con_from_hdl(hdl);
            if (!con || con->get_state() != websocketpp::session::state::open) {
                response["status"] = selfdefine::frame::status::gone;
                response["body"] = json(nullptr);
                return response.dump();
            }

            string uid = js["uid"].get<string>();
            string session_id = js["session_id"].get<string>();
            if (uid.empty() || session_id.empty()) {
                response["status"] = selfdefine::frame::status::bad_request;
                response["body"] = "Condition UnValid";
                return response.dump();
            }
            //布隆过滤器验证
            if(!redis_handler->BfExistByUID("ChatRoom","user",uid)){
                response["status"] = selfdefine::frame::status::unauthorized;
                response["body"] = "User is Not Exists";
                return response.dump();
            }
            //尝试查询缓存
            string cache_key = "user:histroy:"+uid+":"+session_id;
            uint64_t ttl = redis_handler->GenerateTTL(600000*5,600000*10);
            string cache_value;
            if (redis_handler->GetValue(cache_key,cache_value,ttl)) {
                string history = json::parse(cache_value);
                response["body"] = history;
                response["status"] = selfdefine::frame::status::ok;
                return response.dump();
            }else {
                //获取好友请求列表
                string history = HistoryHandler::get(uid,session_id);
                if (!history.empty()) {
                    cache_value = history;
                    redis_handler->CacheKey(cache_key,cache_value,ttl);
                }
            }
            response["status"] = selfdefine::frame::status::ok;
            response["body"] = cache_value;
            return response.dump();
        }catch (exception &e) {
            json response;
            response["body"] = e.what();
            response["status"] = selfdefine::frame::status::bad_request;
            LOG_DEBUG<<e.what();
            return response.dump();
        }
    }

    /// 刷新令牌
    /**
    * @param m_server 服务器句柄
    * @param hdl  连接句柄
    * @param js   参数
    * @return json序列化结果
    *
    *1. 客户端发送刷新令牌，请求新访问令牌与刷新令牌
    *2. 验证刷新令牌有效性
    *3. 成功生成两个jwt作为token(短期token、刷新token)返回给客户端，服务端存储刷新token token:userid:刷新token 用户ID TTL
    *4. 短期token用于api请求，刷新token用于检测用户连接有效性，便于及时控制连接
    *5. 后续客户端使用短期token发起wss连接，服务端存储对应连接的映射
    */
    string fresh(websocketpp::server<websocketpp::config::asio_tls>& m_server,const websocketpp::connection_hdl &hdl, json &js) {
        try{
            json response;
            server::connection_ptr con = m_server.get_con_from_hdl(hdl);
            if (!con || con->get_state() != websocketpp::session::state::open) {
                response["status"] = selfdefine::frame::status::gone;
                response["body"] = json(nullptr);
                return response.dump();
            }
            //获取uid与jti
            string token = js["refresh_token"].get<string>();
            auto decode  = jwt::decode(token);
            string jti = decode.get_id();
            string subject = decode.get_subject();
            string uid = js["uid"].get<string>();
            if (jti.empty() || subject.empty() || uid.empty()) {
                response["status"] = selfdefine::frame::status::bad_request;
                response["body"] = "token error";
                return response.dump();
            }
            //验证uid
            //查询redis验证有效性（查询后删除，避免旧token依然有效，提高安全性）
            string token_key = "reflushtoken:"+jti;
            //需使用分布式锁保证并发安全,粗粒度
            string cache_key = "lock:user:fresh:"+uid;
            string cache_value = SSLService::generatorUUID();
            uint64_t ttl = redis_handler->GenerateTTL(30000,60000);
            distribute_lock lock(cache_key,cache_value,ttl>0?ttl:60000);
            if (!lock.is_success()) {
                //获取锁失败
                response["status"] = selfdefine::frame::status::internal_server_error;
                response["body"] = "lock error";
                return response.dump();
            }
            if (!redis_handler->FindToken(token_key)) {
                //jwt失效，重新登录
                response["status"] = selfdefine::frame::status::token_expire;
                response["body"] = "expire";
                return response.dump();
            }
            //验证令牌
            unordered_map<string,jwt::claim> except_claim;
            except_claim["scope"] = jwt::claim(string(REFLUSH_SCOPE));//确认令牌类型
            if (SSLService::verifyJWT(token,SERVER_ID,except_claim,subject,jti,60) != SSLService::verify_status::OK) {
                //不论是过期还是验证失败，均要求重新登录
                response["status"] = selfdefine::frame::status::unauthorized;
                response["body"] = "unauthorized";
                return response.dump();
            }
            //验证成功，生成新访问令牌与刷新令牌
            except_claim["scope"] = jwt::claim(string(ACCESS_SCOPE));
            string access_token = SSLService::getJWT(SERVER_ID,subject,except_claim,
                    chrono::system_clock::now()+chrono::minutes(60),
                    chrono::system_clock::now());
            if (access_token.empty()) {
                response["status"] = selfdefine::frame::status::service_unavailable;
                response["body"] = "Generate Access Token Failed";
                return response.dump();
            }
            //生成刷新令牌，有效期7天
            except_claim["scope"] = jwt::claim(string(REFLUSH_SCOPE));
            //需要生成JTI防止重放攻击,添加SERVER_ID保证分布式环境安全
            string new_jti = SERVER_ID + SSLService::generatorUUID();
            string reflush_token = SSLService::getJWT(SERVER_ID,subject,except_claim,
                chrono::system_clock::now()+chrono::hours(24*7),
                chrono::system_clock::now(),
                new_jti);
            if (reflush_token.empty()) {
                response["status"] = selfdefine::frame::status::service_unavailable;
                response["body"] = "Generate Refresh Token Failed";
                return response.dump();
            }
            //存储JTI到redis
            stringstream ss;
            ss<<"reflushtoken:"<<new_jti;
            try{
                ttl = redis_handler->GenerateTTL(3600000*24*5,3600000*24*7);
                if (!redis_handler->CacheKey(ss.str(),"1",ttl>0?ttl:3600000*24*7)) {
                    //缓存失败
                    response["status"] = selfdefine::frame::status::internal_server_error;
                    response["body"] = "Cache Error";
                    return response.dump();
                }
                lock.distroy();
            }catch (runtime_error& re) {
                LOG_ERROR<<"runtime_error: "<<re.what();
                response["status"] = selfdefine::frame::status::service_unavailable;
                response["body"] = re.what();
                return response.dump();
            }
            //返回给客户端响应
            response["status"] = selfdefine::frame::status::ok;
            response["body"] = json::array();
            response["body"]["access_token"] = access_token;
            response["body"]["reflush_token"] = reflush_token;
            return response.dump();
        }catch (const std::exception &e) {
            json response;
            response["status"] = selfdefine::frame::status::bad_request;
            response["body"] = e.what();
            return response.dump();
        }
    }

    ///获取个人信息
    string GetInfo(websocketpp::server<websocketpp::config::asio_tls>& m_server,const websocketpp::connection_hdl &hdl, json &js) {
        try{
            json response;
            server::connection_ptr con = m_server.get_con_from_hdl(hdl);
            if (!con || con->get_state() != websocketpp::session::state::open) {
                response["status"] = selfdefine::frame::status::gone;
                response["body"] = json(nullptr);
                return response.dump();
            }
            string uid = js["uid"].get<string>();
            if (uid.empty()) {
                response["status"] = selfdefine::frame::status::bad_request;
                response["body"] = "MissCondition";
                return response.dump();
            }
            //尝试查询缓存
            string cache_key = "user:info:" + uid;
            uint64_t ttl = redis_handler->GenerateTTL(3600000*24*5,3600000*24*7);
            string cache_value;
            if (redis_handler->GetValue(cache_key,cache_value,ttl)) {
                json userinfo = json::parse(cache_value);
                //DEBUG: 查询好友列表与群聊列表
                userinfo["friendlist"] = FriendHandler::GetFriendList(uid);
                userinfo["grouplist"] = ChatGroupHandler::getGroupList(uid);
                //获取最近会话记录
                userinfo["rsl"] = redis_handler->GetRSL(uid,ttl);

                response["status"] = selfdefine::frame::status::ok;
                response["body"] = userinfo;
                return response.dump();
            }
            //获取用户信息
            auto user = UserHandler::getUserInfoByID(uid);
            if (!user.first || !user.second) {
                response["body"] = "User Not Found";
                response["status"] = selfdefine::frame::status::not_found;
                return response.dump();
            }
            json userinfo;
            userinfo["uid"] = stoull(uid);
            userinfo["name"] = user.first->getName();
            userinfo["account"] = user.first->getUsername();
            userinfo["phone"] = user.second->getPhone();
            userinfo["email"] = user.second->getEmail();
            userinfo["created_at"] = user.second->getCreatedAt();
            //redis记录用户信息
            cache_value = userinfo.dump();
            redis_handler->CacheKey(cache_key,cache_value,ttl);

            //DEBUG: 查询好友列表与群聊列表
            userinfo["friendlist"] = FriendHandler::GetFriendList(uid);
            userinfo["grouplist"] = ChatGroupHandler::getGroupList(uid);
            //获取最近会话记录
            userinfo["rsl"] = redis_handler->GetRSL(uid,ttl);


            response["status"] = selfdefine::frame::status::ok;
            response["body"] = userinfo;

            return response.dump();
        }catch (exception &e) {
            json response;
            response["body"] = e.what();
            response["status"] = selfdefine::frame::status::bad_request;
            LOG_DEBUG<<e.what();
            return response.dump();
        }
    }

    ///存储用户连接
    ///用户登录成功时，不应该由open_handler获取信息，open_handler记录用户连接与状态，用户信息客户端进行wss请求
    bool on_open(const string& uid,const websocketpp::connection_hdl &hdl) {
        try{
            uint64_t ttl = redis_handler->GenerateTTL(3600000*24*5,3600000*24*7);
            //记录用户登录信息
            if (!redis_handler->CacheKey("local:"+uid,SERVER_ID,ttl))
                return false;
            this->UserMap[uid] = hdl;
            return true;
        }catch (std::exception &e) {
            LOG_DEBUG<<e.what();
            return false;
        }
    }

    ///移除用户连接
    void on_remove(const string& uid) {
        try{
            //用户不存在直接返回
            if (this->UserMap.count(uid) == 0) {
                return;
            }
            redis_handler->RemoveKey("local:"+uid);
            this->UserMap.erase(uid);
            UserHandler::UpdateUserLastLogin(uid);
        }catch (std::exception &e) {
            LOG_DEBUG<<e.what();
        }
    }

    ~WebSocketService() {
        for (auto& item : this->UserMap) {
            this->on_remove(item.first);
        }
    }
private:
    WebSocketService(){
        HttpServiceMap.insert({HTTP_API_TYPE::LOGIN_API,bind(&WebSocketService::login,this,_1,_2,_3)});
        HttpServiceMap.insert({HTTP_API_TYPE::REGISTER_API,bind(&WebSocketService::registe,this,_1,_2,_3)});
        WsServiceMap.insert({WS_API_TYPE::REFRESH_TOKEN_API,bind(&WebSocketService::fresh,this,_1,_2,_3)});
        WsServiceMap.insert({WS_API_TYPE::INFO_API,bind(&WebSocketService::GetInfo,this,_1,_2,_3)});
        WsServiceMap.insert({WS_API_TYPE::TRANSPROT_API,bind(&WebSocketService::transport,this,_1,_2,_3)});
        WsServiceMap.insert({WS_API_TYPE::USERINFO_API,bind(&WebSocketService::GetUserInfo,this,_1,_2,_3)});
        WsServiceMap.insert({WS_API_TYPE::GROUPINFO_API,bind(&WebSocketService::GetGroupInfo,this,_1,_2,_3)});
        WsServiceMap.insert({WS_API_TYPE::FRIENDLIST_API,bind(&WebSocketService::GetFriendList,this,_1,_2,_3)});
        WsServiceMap.insert({WS_API_TYPE::GROUPLIST_API,bind(&WebSocketService::GetGroupList,this,_1,_2,_3)});
        WsServiceMap.insert({WS_API_TYPE::REQUESTLIST_API,bind(&WebSocketService::GetInfo,this,_1,_2,_3)});
        WsServiceMap.insert({WS_API_TYPE::GROUPMEMBERLIST_API,bind(&WebSocketService::GetGroupMemberList,this,_1,_2,_3)});
        WsServiceMap.insert({WS_API_TYPE::SEARCH_API,bind(&WebSocketService::ConditionSearch,this,_1,_2,_3)});
        WsServiceMap.insert({WS_API_TYPE::SENDFRIENDREQUEST_API,bind(&WebSocketService::sendFriendRequest,this,_1,_2,_3)});
        WsServiceMap.insert({WS_API_TYPE::SENDGROUPREQUEST_API,bind(&WebSocketService::sendGroupRequest,this,_1,_2,_3)});
        WsServiceMap.insert({WS_API_TYPE::PROCESSFRIENDREQUEST_API,bind(&WebSocketService::ProcessFriendRequest,this,_1,_2,_3)});
        WsServiceMap.insert({WS_API_TYPE::PROCESSGROUPREQUEST_API,bind(&WebSocketService::ProcessGroupRequest,this,_1,_2,_3)});
        WsServiceMap.insert({WS_API_TYPE::DELETEFRIEND_API,bind(&WebSocketService::DeleteFriend,this,_1,_2,_3)});
        WsServiceMap.insert({WS_API_TYPE::QUITGROUP_API,bind(&WebSocketService::DeleteGroup,this,_1,_2,_3)});
        WsServiceMap.insert({WS_API_TYPE::CREATEGROUP_API,bind(&WebSocketService::CreateGroup,this,_1,_2,_3)});
        WsServiceMap.insert({WS_API_TYPE::KICKOUTMEMBER_API,bind(&WebSocketService::kickOutMember,this,_1,_2,_3)});
        WsServiceMap.insert({WS_API_TYPE::UPMEMBERROLE_API,bind(&WebSocketService::UpGroupMemberRole,this,_1,_2,_3)});
        WsServiceMap.insert({WS_API_TYPE::REVOKEMEMBERROLE_API,bind(&WebSocketService::RevokeGroupMemberRole,this,_1,_2,_3)});
        WsServiceMap.insert({WS_API_TYPE::CHAT_API,bind(&WebSocketService::chat,this,_1,_2,_3)});
        WsServiceMap.insert({WS_API_TYPE::GROUPCHAT_API,bind(&WebSocketService::GroupChat,this,_1,_2,_3)});
        WsServiceMap.insert({WS_API_TYPE::SAVEHISTROY_API,bind(&WebSocketService::SaveHistroy,this,_1,_2,_3)});
        WsServiceMap.insert({WS_API_TYPE::HISTROY_API,bind(&WebSocketService::GetHistroy,this,_1,_2,_3)});
        user_handler = make_shared<UserHandler>();
        offline_handler = make_shared<OfflineHandler>();
        group_handler = make_shared<ChatGroupHandler>();
        redis_handler = make_shared<RedisHandler>();
        this->kafka_producer = make_shared<KafkaMessageProducer>(SERVER_ID);
        this->kafka_producer->Init({SERVER_ID});
    }
    //建立类型id与业务回调函数的映射关系
    unordered_map<string,WebSocketMsgHandler> WsServiceMap;   //消息类型与业务函数的映射表
    unordered_map<string,HttpMsgHandler> HttpServiceMap;   //消息类型与业务函数的映射表
    //用户账号与用户连接的映射表
    unordered_map<string,websocketpp::connection_hdl> UserMap;
    //数据表处理类
    shared_ptr<UserHandler> user_handler;
    shared_ptr<OfflineHandler> offline_handler;
    shared_ptr<ChatGroupHandler> group_handler;
    shared_ptr<RedisHandler> redis_handler;
    shared_ptr<KafkaMessageProducer> kafka_producer;
    shared_mutex m;
};


//
// Created by laswer on 2025/10/17.
//
#pragma once
#include <boost/core/default_allocator.hpp>

#include "chatservice.hpp"
#include "HeadFile.h"
const int MAX_CONN = 10;

const string MYSQL_HOST = "192.168.250.100";
const int MYSQL_PORT_ = 3306;
const string MYSQL_USER = "laswer";
const string MYSQL_PWD  =  "2836992987";
const string MYSQL_DEFAULT_DB = "ChatRoom";
const string MYSQL_CHARSET = "utf8mb4";
const int MYSQL_TIMEOUT = 60;

const string REDIS_HOST = "192.168.250.100";
const int REDIS_PORT = 6379;
const string REDIS_PWD = "2836992987";

const string REDIS_LOCK_LUA_PATH = "./lua/redis_lock.lua";
const string REDIS_UNLOCK_LUA_PATH = "./lua/redis_unlock.lua";
const string REDIS_FIND_LUA_PATH = "./lua/redis_find.lua";
const string REDIS_ADD_LUA_PATH = "./lua/redis_add.lua";
const string REDIS_REMOVE_LUA_PATH = "./lua/redis_remove.lua";
const string REDIS_FIND_NO_BLOOM_LUA_PATH = "./lua/redis_find_no_bloom.lua";
const string REDIS_GET_AND_REMOVE_LUA_PATH = "./lua/redis_get_and_remove.lua";
const string REDIS_ZADD_LUA_PATH = "./lua/redis_zadd.lua";
const string REDIS_ZRANGE_LUA_PATH = "./lua/redis_zrange.lua";
const string REDIS_GET_RSL_LUA_PATH = "./lua/redis_get_rsl.lua";
const string REDIS_UPDATE_RSL_LUA_PATH = "./lua/redis_update_rsl.lua";

const int TRYAGAIN_COUNT = 3;
const string BLOOMKEY = "bloom";
const string BLOOMRATE = "0.00001";
const string BLOOMCAPACITY = "1000000";
const int SLEEPSEC = 1;

typedef struct  mysqlhead
{
    size_t port;
    size_t timeout;
    size_t retry;
    string host;
    string database;
    string user;
    string password;
    string charset;
    mysqlhead(const string& n_host = MYSQL_HOST,
                            const size_t& n_port = MYSQL_PORT_,
                            const string& n_user = MYSQL_USER,
                            const string& n_password = MYSQL_PWD,
                            const string& n_database = MYSQL_DEFAULT_DB,
                            const size_t& timeout_sec = MYSQL_TIMEOUT,
                            const size_t& retry_count = TRYAGAIN_COUNT):port(n_port),timeout(timeout_sec),retry(retry_count),host(n_host),database(n_database),user(n_user),password(n_password),charset(MYSQL_CHARSET){}
}ConnectionHead;

typedef struct Result{
    uint64_t res;
    unsigned int  FieldsNum;
    shared_ptr<vector<string>> FieldsName;
    shared_ptr<vector<vector<string>>> str_vec;
    Result():res(0),FieldsNum(0),FieldsName(nullptr),str_vec(nullptr){}
}MySQLResult;

class MysqlObject {
private:
    atomic<bool> ensuring{false};
    MYSQL* mysql;
    MYSQL_RES* mysqlres;
    MYSQL_STMT* mysql_stmt;
    weak_ptr<ConnectionHead> ConnectionHeadPtr;

    void createConnection() {
        if (this->mysql)
            throw runtime_error("MysqlObject::createConnection(): mysql conn error - mysql连接已关闭");
        this->mysql = mysql_init(nullptr);
        if (this->mysql == nullptr) {
            throw runtime_error("MysqlObject::createConnection(): mysql_init error - 可能因为内存分配导致的失败");
        }
        auto head = this->ConnectionHeadPtr.lock();
        if (head) {
            mysql_options(this->mysql,MYSQL_SET_CHARSET_NAME, head->charset.c_str());
            mysql_options(this->mysql,MYSQL_OPT_CONNECT_TIMEOUT,&head->timeout);
            mysql_options(this->mysql,MYSQL_OPT_READ_TIMEOUT,&head->timeout);
            //mysql_options(this->mysql,MYSQL_OPT_RECONNECT,&head->retry);
        }else {
            string errmsg = "UNKNOWN";
            if (this->mysql){
                errmsg = mysql_error(this->mysql);
                mysql_close(this->mysql);
                this->mysql = nullptr;
            }
            throw runtime_error("MysqlObject::createConnection(): mysql_options error - "+errmsg);
        }
        auto res = mysql_real_connect(this->mysql,head->host.c_str(),head->user.c_str(),head->password.c_str(),head->database.c_str(),head->port,nullptr,CLIENT_MULTI_STATEMENTS);
        if(res == nullptr){
            string errmsg = "UNKNOWN";
            if (this->mysql){
                errmsg = mysql_error(this->mysql);
                mysql_close(this->mysql);
                this->mysql = nullptr;
            }
            throw runtime_error("MysqlObject::createConnection(): mysql_real_connect error - "+errmsg);
        }
        LOG_INFO<<"Mysql连接成功 ("<<head->host<<":"<<head->port<<")";

    }

    // 移除 SQL 注释（防止被恶意绕过）
    static std::string removeSqlComments(const std::string& sql) {
        std::string result = sql;

        // 处理单行注释 (-- ...)
        std::regex singleLineComment(R"(--[^\n]*\n)");
        result = std::regex_replace(result, singleLineComment, "\n");

        return result;
    }

    // 判断是否为 INSERT 语句（支持所有变体）
    static bool isInsertStatement(const std::string& sql) {
        // 1. 先清理注释（关键安全步骤）
        std::string cleanSql = removeSqlComments(sql);

        // 2. 精确匹配 INSERT 语句模式
        static const std::regex insertRegex(
            R"(^\s*(INSERT\s+(IGNORE\s+)?|REPLACE\s+)INTO\b)",
            std::regex_constants::icase  // 大小写不敏感
        );

        return std::regex_search(cleanSql, insertRegex);
    }

public:
    bool is_Alive() {
        try {
            return this->mysql && (mysql_ping(this->mysql) == 0);
        }catch (exception& e) {
            LOG_ERROR<<e.what();
            return false;
        }

    }

    bool EnsureMySQL() {
        if (is_Alive()) {
            return true;
        }
        bool expect = false;
        if (!this->ensuring.compare_exchange_strong(expect, true,memory_order_acq_rel)) {
            //如果ensuring为true，代表当前连接正在进行保活，返回true
            auto start = chrono::steady_clock::now();
            auto head = this->ConnectionHeadPtr.lock();
            auto timeout_seconds = head ? head->timeout : 30;
            while (this->ensuring.load(memory_order_acquire)) {
                if (chrono::steady_clock::now() - start > chrono::seconds(timeout_seconds)) {
                    LOG_ERROR << "MysqlObject::EnsureMySQL(): EnsureMySQL(): 等待重连超时";
                    return false;
                }
                std::this_thread::yield();
            }
            // 返回重连后的实际状态
            return is_Alive();
        }
        if (is_Alive()) {
            this->ensuring.store(false, memory_order_release);
            return true;
        }
        if (this->mysql) {
            mysql_close(this->mysql);
            this->mysql = nullptr;
        }
        try {
            this->createConnection();
            this->ensuring.store(false, memory_order_release);
            return true;
        }catch (exception& e) {
            LOG_ERROR<<"MysqlObject::EnsureMySQL(): MySQL保活重连失败："<<e.what();
        }
        this->ensuring.store(false, memory_order_release);
        return false;
    }

    MysqlObject(const shared_ptr<ConnectionHead>& head):mysqlres(nullptr),mysql_stmt(nullptr) {
        this->ConnectionHeadPtr = head;
        int count = 0;
        while (count++ < head->retry) {
            try {
                this->createConnection();
                return;
            }catch (exception& e) {
                LOG_ERROR<<"MysqlObject::MysqlObject(): 第 "<<count +1 << "次连接失败："<<e.what();
                std::this_thread::yield();
            }
        }
        throw runtime_error("MysqlObject::MysqlObject(): MySQL连接失败");
    }
    ~MysqlObject() {
        close();
    }

    void close() {
        try {
            if (this->mysql){
                mysql_close(this->mysql);
                this->mysql = nullptr;
            }
            if (this->mysqlres){
                mysql_free_result(this->mysqlres);
                this->mysqlres = nullptr;
            }
            if (this->mysql_stmt){
                mysql_stmt_close(this->mysql_stmt);
                this->mysql_stmt = nullptr;
            }
        } catch (const std::exception& e) {
            LOG_ERROR<<e.what();
        }
    }

    bool close_autocommit() {
        if (this->mysql)
            mysql_autocommit(this->mysql,false);
        else {
            return false;
        }
        return true;
    }

    bool open_autocommit() {
        if (this->mysql)
            mysql_autocommit(this->mysql,true);
        else {
            return false;
        }
        return true;
    }

    template<typename ... Args>
    bool excuteDDL_param(const std::string& sql, Args&&... args) {
        //拒绝任何DDL操作
        return false;
        if (!is_Alive())
            return false;
        if (this->mysql_stmt != nullptr) {
            mysql_stmt_close(this->mysql_stmt);
            delete this->mysql_stmt;
            this->mysql_stmt = nullptr;
        }
        try {
            this->mysql_stmt = mysql_stmt_init(this->mysql);
            this->mysql_stmt = mysql_stmt_init(this->mysql);
            if (!this->mysql_stmt) {
                string info = mysql_error(this->mysql);
                throw runtime_error("MysqlObject::excuteDDL_param -- 参数化查询初始化句柄错误: "+info);
            }
            if (mysql_stmt_prepare(this->mysql_stmt,sql.c_str(),sql.size()) != 0) {
                //LOG_ERROR<<"MysqlObject::excute_param -- 参数化查询准备SQL语句错误: "<<mysql_stmt_error(this->mysql_stmt);
                string info = mysql_stmt_error(this->mysql_stmt);
                throw runtime_error("MysqlObject::excuteDDL_param -- 参数化查询准备SQL语句错误: "+info);
            }

            size_t param_count = mysql_stmt_param_count(this->mysql_stmt);
            size_t args_count = sizeof...(args);

            if (param_count != args_count) {
                throw runtime_error("MysqlObject::excuteDDL_param -- 参数数量不匹配: "+sql);
            }
            if (param_count > 0) {
                vector<MYSQL_BIND> binds = createBindings(forward<Args>(args)...);
                if (mysql_stmt_bind_param(this->mysql_stmt,binds.data()) != 0) {
                    string info = mysql_stmt_error(this->mysql_stmt);
                    cleanupBindings(binds);
                    throw runtime_error("MysqlObject::excuteDDL_param -- 绑定参数失败: "+info);
                }
                if (mysql_stmt_execute(this->mysql_stmt) != 0) {
                    string info = mysql_stmt_error(this->mysql_stmt);
                    cleanupBindings(binds);
                    throw runtime_error("MysqlObject::excuteDDL_param -- 执行sql失败: "+info);
                }
                //cleanupBindings(binds);
            }else {
                if (mysql_stmt_execute(this->mysql_stmt) != 0) {
                    string info = mysql_stmt_error(this->mysql_stmt);
                    throw runtime_error("MysqlObject::excuteDDL_param -- 执行sql失败: "+info);
                }
            }
            mysql_stmt_close(this->mysql_stmt);
            this->mysql_stmt = nullptr;
            return true;
        }catch (const exception& e) {
            if (this->mysql_stmt != nullptr) {
                mysql_stmt_close(this->mysql_stmt);
                this->mysql_stmt = nullptr;
            }
            LOG_ERROR<<e.what();
            return false;
        }
    }

    // 开启事务 (关闭自动提交)
    bool beginTransaction() {
        if (!this->mysql)return false;
        if (mysql_autocommit(this->mysql,false)) {
            LOG_INFO<<"Transaction Start Failed: "<<std::string(mysql_error(this->mysql));
            return false;
        }

        return true;
    }

    // 提交事务
    bool commit() {
        if (!this->mysql)return false;
        if (mysql_commit(this->mysql) != 0) {
            LOG_INFO<<"Commit failed: "<<std::string(mysql_error(this->mysql));
            return false;
        }
        mysql_autocommit(mysql, true); // 恢复自动提交
        return true;
    }

    // 回滚事务
    void rollback() {
        try {
            mysql_rollback(mysql);
            mysql_autocommit(mysql, true); // 恢复自动提交
        }catch (exception& e) {
            LOG_INFO<<"Rollback failed: "<<std::string(e.what());
        }
    }

    template<typename ... Args>
    shared_ptr<MySQLResult> excuteDML_param(const std::string& sql, Args&&... args) {
        if (!is_Alive())
            return nullptr;
        if (this->mysql_stmt != nullptr) {
            mysql_stmt_close(this->mysql_stmt);
            this->mysql_stmt = nullptr;
        }
        if (this->mysqlres != nullptr) {
            mysql_free_result(this->mysqlres);
            this->mysqlres = nullptr;
        }
        vector<MYSQL_BIND> binds;
        try {
            shared_ptr<MySQLResult> res = make_shared<MySQLResult>();
            //transform(sql.begin(),sql.end(),sql.begin(),::tolower);
            this->mysql_stmt = mysql_stmt_init(this->mysql);
            if (!this->mysql_stmt) {
                string info = mysql_error(this->mysql);
                throw runtime_error("MysqlObject::excuteDML_param -- 参数化查询初始化句柄错误: "+info);
            }
            if (mysql_stmt_prepare(this->mysql_stmt,sql.c_str(),sql.size()) != 0) {
                string info = mysql_stmt_error(this->mysql_stmt);
                throw runtime_error("MysqlObject::excuteDML_param -- 参数化查询准备SQL语句错误: "+info);
            }

            size_t param_count = mysql_stmt_param_count(this->mysql_stmt);
            size_t args_count = sizeof...(args);

            if (param_count != args_count) {
                throw runtime_error("MysqlObject::excuteDML_param -- 参数数量不匹配: "+sql);
            }
            // if (param_count > 0) {
            binds = createBindings(std::forward<Args>(args)...);
            if (mysql_stmt_bind_param(this->mysql_stmt,binds.data()) != 0) {
                string info = mysql_stmt_error(this->mysql_stmt);
                cleanupBindings(binds);
                throw runtime_error("MysqlObject::excuteDML_param -- 绑定参数失败: "+info);
            }
                //cleanupBindings(binds);
            //}
            if (mysql_stmt_execute(this->mysql_stmt) != 0) {
                string info = mysql_stmt_error(this->mysql_stmt);
                cleanupBindings(binds);
                throw runtime_error("MysqlObject::excuteDML_param -- 执行sql失败: "+info);
            }
            if (mysql_stmt_store_result(this->mysql_stmt) != 0) {
                string info = mysql_stmt_error(this->mysql_stmt);
                cleanupBindings(binds);
                throw runtime_error("MysqlObject::excuteDML_param -- 存储结果失败: "+info);
            }
            this->mysqlres = mysql_stmt_result_metadata(this->mysql_stmt);
            if (!this->mysqlres) {
                if (mysql_field_count(this->mysql) > 0)
                    throw runtime_error("MysqlObject::excuteDML_param -- 查询成功但获取结果集失败");
                //执行无结果集的操作（insert、delete、update），返回影响行数
                res->FieldsNum = 0;
                //插入比较特殊，返回插入id
                //string p = "insert into";
                if (isInsertStatement(sql)) {
                    res->res = 1;
                }else {
                    res->res = mysql_stmt_affected_rows(this->mysql_stmt);
                }
                //res->res = (sql.compare(0,p.length(),p) == 0) ?mysql_stmt_insert_id(this->mysql_stmt):mysql_stmt_affected_rows(this->mysql_stmt);
                cleanupBindings(binds);
                return res;
            }
            res->FieldsNum = mysql_num_fields(this->mysqlres);
            res->FieldsName = make_shared<vector<string>>(res->FieldsNum);
            std::vector<MYSQL_BIND> result_binds(res->FieldsNum);
            std::vector<std::vector<char>> string_buffers(res->FieldsNum, std::vector<char>(256));
            std::vector<unsigned long> lengths(res->FieldsNum);

            MYSQL_FIELD* fields = mysql_fetch_fields(this->mysqlres);//由MYSQL_RES管理，无需手动释放

            //结果保存为string
            for (int i = 0; i < res->FieldsNum; ++i) {
                memset(&result_binds[i], 0, sizeof(MYSQL_BIND));
                result_binds[i].buffer_type = MYSQL_TYPE_STRING;
                result_binds[i].buffer = string_buffers[i].data();
                result_binds[i].buffer_length = string_buffers[i].size();
                result_binds[i].length = &lengths[i];

                res->FieldsName->at(i) = fields[i].name;
            }

            if (mysql_stmt_bind_result(this->mysql_stmt, result_binds.data()) != 0) {
                cleanupBindings(binds);
                throw std::runtime_error("MysqlObject::excuteDML_param -- 绑定结果集失败");
            }
            res->str_vec = make_shared<vector<vector<string>>>();
            res->res = 0;
            // 获取所有行数据
            while (mysql_stmt_fetch(this->mysql_stmt) == 0) {
                std::vector<std::string> row;
                for (int i = 0; i < res->FieldsNum; ++i) {
                    row.emplace_back(string_buffers[i].data(), lengths[i]);
                }
                res->res++;
                res->str_vec->emplace_back(::move(row));
            }
            cleanupBindings(binds);
            mysql_stmt_close(this->mysql_stmt);
            this->mysql_stmt = nullptr;
            mysql_free_result(this->mysqlres);
            this->mysqlres = nullptr;
            return res;
        }catch (const exception& e) {
            if (this->mysql_stmt != nullptr) {
                mysql_stmt_close(this->mysql_stmt);
                this->mysql_stmt = nullptr;
            }
            if (this->mysqlres != nullptr) {
                mysql_free_result(this->mysqlres);
                this->mysqlres = nullptr;
            }
            if (!binds.empty()) {
                cleanupBindings(binds);
            }
            LOG_ERROR<<e.what();
            return nullptr;
        }
    }

private:
    template<typename... Args>
    vector<MYSQL_BIND> createBindings(Args&&... args) {
        vector<MYSQL_BIND> binds;
        binds.reserve(sizeof...(Args));
        createBindingImpl(binds, std::forward<Args>(args)...);
        return binds;
    }
    void createBindingImpl(vector<MYSQL_BIND>& binds) {}

    //模板泛型编程

    //整形特例化
    //4字节有符号整形类型特例化
    template<typename T,typename... Rest>
    enable_if_t<is_integral_v<decay_t<T>> && !is_same_v<decay_t<T>,bool> && is_signed_v<decay_t<T>> && (sizeof(decay_t<T>) == 4)>
    createBindingImpl(vector<MYSQL_BIND>& binds,T&& value,Rest&&... rest) {
        MYSQL_BIND bind;
        memset(&bind,0,sizeof(bind));
        bind.buffer_type = MYSQL_TYPE_LONG;
        bind.buffer = new int32_t(value);
        bind.is_unsigned = false;
        binds.push_back(bind);
        createBindingImpl(binds,std::forward<Rest>(rest)...);
    }

    //8字节有符号整形类型特例化
    template<typename T,typename... Rest>
    enable_if_t<is_integral_v<decay_t<T>> && !is_same_v<decay_t<T>,bool> && is_signed_v<decay_t<T>> && (sizeof(decay_t<T>) == 8)>
    createBindingImpl(vector<MYSQL_BIND>& binds,T&& value,Rest&&... rest) {
        MYSQL_BIND bind;
        memset(&bind,0,sizeof(bind));
        bind.buffer_type = MYSQL_TYPE_LONGLONG;
        bind.buffer = new int64_t(value);
        bind.is_unsigned = false;
        binds.push_back(bind);
        createBindingImpl(binds,std::forward<Rest>(rest)...);
    }
    //4字节无符号整形类型特例化
    template<typename T,typename... Rest>
    enable_if_t<is_integral_v<decay_t<T>> && !is_same_v<decay_t<T>,bool> && is_unsigned_v<decay_t<T>> && (sizeof(decay_t<T>) == 4)>
    createBindingImpl(vector<MYSQL_BIND>& binds,T&& value,Rest&&... rest) {
        MYSQL_BIND bind;
        memset(&bind,0,sizeof(bind));
        bind.buffer_type = MYSQL_TYPE_LONG;
        bind.buffer = new uint32_t(value);
        bind.is_unsigned = true;
        binds.push_back(bind);
        createBindingImpl(binds,std::forward<Rest>(rest)...);
    }

    //8字节无符号整形类型特例化
    template<typename T,typename... Rest>
    enable_if_t<is_integral_v<decay_t<T>> && !is_same_v<decay_t<T>,bool> && is_unsigned_v<decay_t<T>> && (sizeof(decay_t<T>) == 8)>
    createBindingImpl(vector<MYSQL_BIND>& binds,T&& value,Rest&&... rest) {
        MYSQL_BIND bind;
        memset(&bind,0,sizeof(bind));
        bind.buffer_type = MYSQL_TYPE_LONGLONG;
        bind.buffer = new uint64_t(value);
        bind.is_unsigned = true;
        binds.push_back(bind);
        createBindingImpl(binds,std::forward<Rest>(rest)...);
    }

    // template<typename T,typename... Rest>
    // enable_if_t<is_integral_v<decay_t<T>> && !is_same_v<decay_t<T>,bool>>
    // createBindingImpl(vector<MYSQL_BIND>& binds,T&& value,Rest&&... rest) {
    //     MYSQL_BIND bind;
    //     memset(&bind,0,sizeof(bind));
    //     if (is_same_v<T,int>) {
    //         bind.buffer_type = MYSQL_TYPE_LONG;
    //         bind.buffer = new int32_t(value);
    //         bind.is_unsigned = false;
    //     }else if (is_same_v<T,long>) {
    //         bind.buffer_type = MYSQL_TYPE_LONGLONG;
    //         bind.buffer = new long(value);
    //         bind.is_unsigned = false;
    //     }else if (is_same_v<T,uint64_t>){
    //         bind.buffer_type = MYSQL_TYPE_LONGLONG;
    //         bind.buffer = new long(value);
    //         bind.is_unsigned = true;
    //     }else if (is_unsigned_v<T>) {
    //         bind.buffer_type = MYSQL_TYPE_LONG;
    //         bind.buffer = new uint32_t(value);
    //         bind.is_unsigned = true;
    //     }
    //     binds.push_back(bind);
    //     createBindingImpl(binds,std::forward<Rest>(rest)...);
    // }
    //浮点型特例化
    template<typename T,typename... Rest>
    enable_if_t<is_floating_point_v<decay_t<T>> && (sizeof(decay_t<T>) == 4)>
    createBindingImpl(vector<MYSQL_BIND>& binds,T&& value,Rest&&... rest) {
        MYSQL_BIND bind;
        memset(&bind,0,sizeof(bind));
        bind.buffer_type = MYSQL_TYPE_FLOAT;
        bind.buffer = new float(value);
        binds.push_back(bind);
        createBindingImpl(binds,std::forward<Rest>(rest)...);
    }
    template<typename T,typename... Rest>
    enable_if_t<is_floating_point_v<decay_t<T>> && (sizeof(decay_t<T>) == 8)>
    createBindingImpl(vector<MYSQL_BIND>& binds,T&& value,Rest&&... rest) {
        MYSQL_BIND bind;
        memset(&bind,0,sizeof(bind));
        bind.buffer_type = MYSQL_TYPE_DOUBLE;
        bind.buffer = new double(value);
        binds.push_back(bind);
        createBindingImpl(binds,std::forward<Rest>(rest)...);
    }
    // template<typename T,typename... Rest>
    // enable_if_t<is_floating_point_v<decay_t<T>> && !is_same_v<decay_t<T>,bool>>
    // createBindingImpl(vector<MYSQL_BIND>& binds,T&& value,Rest&&... rest) {
    //     MYSQL_BIND bind;
    //     memset(&bind,0,sizeof(bind));
    //     if constexpr (is_same_v<T,float>) {
    //         bind.buffer_type = MYSQL_TYPE_FLOAT;
    //         bind.buffer = new float(value);
    //     }else if constexpr (is_same_v<T,double>) {
    //         bind.buffer_type = MYSQL_TYPE_DOUBLE;
    //         bind.buffer = new double(value);
    //     }
    //     binds.push_back(bind);
    //     createBindingImpl(binds,std::forward<Rest>(rest)...);
    // }
    //字符串特例化
    template<typename T, typename... Rest>
    enable_if_t<std::is_same_v<decay_t<T>, std::string>>
    createBindingImpl(vector<MYSQL_BIND>& binds,T&& value,Rest&&... rest) {
        MYSQL_BIND bind;
        memset(&bind,0,sizeof(bind));
        bind.buffer_type = MYSQL_TYPE_STRING;

        string str = forward<T>(value);
        char* buffer = new char[str.length() + 1];
        strcpy(buffer, str.c_str());
        bind.buffer = buffer;
        bind.buffer_length = str.length();

        binds.push_back(bind);
        createBindingImpl(binds,std::forward<Rest>(rest)...);
    }
    // C风格字符串特例化
    template<typename T,typename... Rest>
    enable_if_t<std::is_same_v<decay_t<T>,const char*> || std::is_same_v<decay_t<T>,char*>>
    createBindingImpl(std::vector<MYSQL_BIND>& binds, T value, Rest&&... rest) {
        createBindingImpl(binds, std::string(value), std::forward<Rest>(rest)...);
    }

    //json特例化
    template<typename T,typename... Rest>
    enable_if_t<std::is_same_v<decay_t<T>,json>>
    createBindingImpl(vector<MYSQL_BIND>& binds,T&& value,Rest&&... rest) {
        MYSQL_BIND bind;
        memset(&bind,0,sizeof(bind));
        bind.buffer_type = MYSQL_TYPE_JSON;
        json js = forward<T>(value);
        string str = js.dump();
        char* buffer = new char[str.length() + 1];
        strcpy(buffer, str.c_str());
        bind.buffer = buffer;
        bind.buffer_length = str.length();

        binds.push_back(bind);
        createBindingImpl(binds,std::forward<Rest>(rest)...);
    }

    //bool类型特例化
    template<typename T,typename... Rest>
    enable_if_t<is_same_v<decay_t<T>,bool>>
    createBindingImpl(vector<MYSQL_BIND>& binds,T&& value,Rest&&... rest) {
        MYSQL_BIND bind;
        memset(&bind,0,sizeof(bind));
        bind.buffer_type = MYSQL_TYPE_BOOL;
        bind.buffer = new bool(value);
        binds.push_back(bind);
        createBindingImpl(binds,std::forward<Rest>(rest)...);
    }

    // 清理绑定内存
    void cleanupBindings(std::vector<MYSQL_BIND>& binds) {
        for (auto& bind : binds) {
            if (bind.buffer) {
                switch (bind.buffer_type) {
                    case MYSQL_TYPE_LONG:
                        if (bind.is_unsigned)
                            delete static_cast<uint32_t*>(bind.buffer);
                        else
                            delete static_cast<int32_t*>(bind.buffer);
                        break;
                    case MYSQL_TYPE_LONGLONG:
                        delete static_cast<long*>(bind.buffer);
                        break;
                    case MYSQL_TYPE_FLOAT:
                        delete static_cast<float*>(bind.buffer);
                        break;
                    case MYSQL_TYPE_DOUBLE:
                        delete static_cast<double*>(bind.buffer);
                        break;
                    case MYSQL_TYPE_BOOL:
                        delete static_cast<bool*>(bind.buffer);
                        break;
                    default:
                        delete[] static_cast<char*>(bind.buffer);
                        break;
                }
                bind.buffer = nullptr;
            }
        }
        binds.clear();
    }
};


typedef struct redisresult{
    int count;
    int len;
    int res;
    string str;
    shared_ptr<vector<string>> vec;
    bool empty;
    redisresult():empty(true),count(0),len(0),res(0),str("(nil)"),vec(nullptr){}
}RedisResult;

class RedisObject {
private:
    atomic<bool> ensuring{false};
    uint16_t connect_interval_ms;
    uint16_t retry_count;
    uint32_t connect_timeout;
    uint64_t port;
    string host;
    weak_ptr<unordered_map<string,string>> lua_map;
    redisContext* redis;
    redisReply* result;

    bool CreateRedisConnect() {
        uint32_t count = 0;
        while (count <= retry_count) {
            if (count > 0) {
                LOG_WARN << "RedisObject:CreateRedisConnect(): Redis连接失败，第 " << count << " 次重试...";
                this_thread::sleep_for(chrono::milliseconds(connect_interval_ms));
            }

            // 设置连接超时
            timeval timeout = {
                connect_timeout,
                0
            };

            this->redis = redisConnectWithTimeout(host.c_str(), port, timeout);

            if (this->redis == nullptr || this->redis->err) {
                string error_msg = this->redis ? this->redis->errstr : "内存分配失败";
                LOG_ERROR << "RedisObject:CreateRedisConnect(): Redis连接错误: " << error_msg;

                if (this->redis) {
                    redisFree(this->redis);
                    this->redis = nullptr;
                }

                count++;
                continue;
            }

            // 设置命令超时
            if (redisSetTimeout(this->redis, timeout) != REDIS_OK) {
                LOG_WARN << "RedisObject:CreateRedisConnect(): 设置Redis命令超时失败";
            }
            redisCommand(this->redis, "AUTH %s", REDIS_PWD.c_str());
            LOG_INFO << "RedisObject:CreateRedisConnect(): Redis连接成功 (" << host << ":" << port << ")";
            return true;
        }

        LOG_ERROR << "RedisObject:CreateRedisConnect(): Redis连接失败，已达到最大重试次数: " << retry_count;
        return false;
    }

    bool GetLuaScript(const string& script_path,string& lua_str) {
    auto lua_ = this->lua_map.lock();
    if (lua_) {
        if (lua_->count(script_path)<=0) {
            LOG_ERROR<<"RedisObject::GetLuaScript(): Lua脚本 "<<script_path<<" 不存在";
            return false;
        }
        lua_str = (*lua_)[script_path];
    }else {
        if (!fs::exists(script_path)) {
            LOG_ERROR<<"RedisObject::GetLuaScript(): Lua脚本 "<<script_path<<" 不存在";
            return false;
        }
        ifstream lua_io(script_path);
        if (!lua_io.is_open()) {
            LOG_ERROR<<"RedisObject::GetLuaScript(): Lua脚本 "<<script_path<<" 打开失败";
            return false;
        }
        stringstream buffer;
        buffer << lua_io.rdbuf();
        lua_str = buffer.str();
        lua_io.close();
    }
    if (lua_str.empty()) {
        LOG_INFO<<"RedisObject::GetLuaScript(): Lua脚本 "<<script_path<<" 为空";
        return false;
    }
    return true;
}
public:
    bool RedisEnsure() {
        try{
            if (this->redis != nullptr && this->redis->err == 0) {
                return true;
            }
            bool expect = false;
            if (!this->ensuring.compare_exchange_strong(expect, true,memory_order_acq_rel)) {
                //如果ensuring为true，代表当前连接正在进行保活，返回true
                auto start = chrono::steady_clock::now();
                while (this->ensuring.load(memory_order_acquire)) {
                    if (chrono::steady_clock::now() - start > chrono::seconds(connect_timeout)) {
                        LOG_ERROR << "RedisEnsure(): 等待重连超时";
                        return false;
                    }
                    std::this_thread::yield();
                }
                // 返回重连后的实际状态
                return (this->redis && !this->redis->err);
            }
            if (this->redis and !this->redis->err) {
                this->ensuring.store(false, memory_order_release);
                return true;
            }
            if (this->redis) {
                redisFree(this->redis);
                this->redis = nullptr;
            }
            //this->redis = redisConnect(host.c_str(),port);
            if (!this->CreateRedisConnect()) {
                LOG_ERROR<<"RedisObject::RedisEnsure(): redis断线重连失败";
                this->ensuring.store(false,memory_order_release);
                return false;
            }
            this->ensuring.store(false,memory_order_release);
            return true;
        }catch (exception& e) {
            LOG_DEBUG<<"RedisObject::GetLuaScript(): "<<e.what();
            return false;
        }
    }


    RedisObject(const shared_ptr<unordered_map<string,string>>& luas,const string& _host = REDIS_HOST,const uint64_t& _port = REDIS_PORT,const uint16_t& retry = 3,const uint32_t& timeout_sec = 30,const uint16_t& interval_ms = 1000):connect_interval_ms(interval_ms),retry_count(retry),connect_timeout(timeout_sec),port(_port),host(_host),lua_map(luas),redis(nullptr),result(nullptr) {
        //this->redis = redisConnect(host.c_str(),port);
        if (!this->CreateRedisConnect()) {
            throw runtime_error("RedisObject::RedisObject(): redisConnect error!");
        }
    }
    ~RedisObject(){
        destory();
    }

    void destory(){
        if(this->redis) {
            redisFree(this->redis);
            this->redis = nullptr;
        }
        if(this->result) {
            freeReplyObject(this->result);
            this->result = nullptr;
        }
    }

    shared_ptr<RedisResult> call(vector<string>& argv) {
        try {
            if (!RedisEnsure()) {
                return nullptr;
            }
            vector<const char*> args;
            vector<size_t> lens;
            args.reserve(argv.size());
            lens.reserve(argv.size());
            for (string& str : argv) {
                args.emplace_back(str.data());
                lens.emplace_back(str.length());
            }
            this->result = (redisReply*)redisCommandArgv(this->redis,argv.size(),args.data(),lens.data());
            if (this->result == nullptr) {
                throw runtime_error("RedisObject::call(): 执行失败: "+string(this->redis->errstr));
            }
            shared_ptr<RedisResult> res = make_shared<RedisResult>();
            switch (this->result->type) {
                case REDIS_REPLY_VERB:
                case REDIS_REPLY_BIGNUM:
                case REDIS_REPLY_DOUBLE:
                case REDIS_REPLY_STATUS:
                case REDIS_REPLY_STRING:
                    res->empty = false;
                    res->len = this->result->len;
                    res->str = string(this->result->str);
                    break;
                case REDIS_REPLY_INTEGER:
                    res->empty = false;
                    res->res = this->result->integer;
                    break;
                case REDIS_REPLY_ARRAY:
                    res->empty = false;
                    res->vec = make_shared<vector<string>>();
                    res->count = 0;
                    for(int i = 0;i<this->result->elements;i++){
                        if(this->result->element[i]->type == REDIS_REPLY_STRING)
                        {
                            res->count++;
                            res->vec->emplace_back(string(this->result->element[i]->str));
                        }else if (this->result->element[i]->type == REDIS_REPLY_INTEGER) {
                            res->count++;
                            res->vec->emplace_back(to_string(this->result->element[i]->integer));
                        }
                        else if(this->result->element[i]->type == REDIS_REPLY_ARRAY)
                        {
                            for(int j = 0;j<this->result->element[i]->elements;j++){
                                res->count++;
                                res->vec->emplace_back(string(this->result->element[i]->element[j]->str));
                            }
                        }
                    }
                    break;
                case REDIS_REPLY_NIL:
                    res->empty = true;
                    break;
                case REDIS_REPLY_ERROR:
                {
                    string error_msg = this->result->str ?
                        string(this->result->str, this->result->len) : "未知错误";
                    throw runtime_error("RedisObject::call(): Redis错误: " + error_msg);
                }

                default:
                {
                    string error_msg = this->result->str ?
                        string(this->result->str, this->result->len) : "未知错误类型";
                    throw runtime_error("RedisObject::call(): 结果解析错误: " + error_msg);
                }
            }
            freeReplyObject(this->result);
            this->result = nullptr;
            return res;
        }catch(exception& e) {
            if (this->result) {
                freeReplyObject(this->result);
                this->result = nullptr;
            }
            LOG_ERROR<<e.what();
            return nullptr;
        }
    }

    ///获取指定key的剩余过期时间ms，返回值大于0代表未过期、0代表已过期、-1代表没有设置过期时间、-2代表键不存在、-3代表执行错误
    long long ttl(const string& key) {
        if (!RedisEnsure()) {
            return -3;
        }
        vector<string> argv{
            "PTTL",key,"1"
        };
        auto res = call(argv);
        if (!res || res->empty) {
            LOG_INFO<<"RedisObject::lock(): 获取剩余时间 "<<key<<" 失败";
            return -3;
        }
        return res->res;
    }

    bool get_and_remove(const string& key) {
        if (!RedisEnsure()) {
            return false;
        }
        string lua_str;
        if (!GetLuaScript(REDIS_GET_AND_REMOVE_LUA_PATH,lua_str)) {
            return false;
        }
        vector<string> argv{
            "EVAL",lua_str,"1",key
        };
        auto res = call(argv);
        if (!res || res->empty) {
            LOG_INFO<<"RedisObject::add_and_remove(): 获取或删除 "<<key<<" 错误";
            return false;
        }
        return true;
    }

    bool zadd(const string& key,const string& msg,const string& scope,const string& ttl_sec) {
        if (!RedisEnsure()) {
            return false;
        }
        string lua_str;
        if (!GetLuaScript(REDIS_ZADD_LUA_PATH,lua_str)) {
            return false;
        }
        vector<string> argv{
            "EVAL",lua_str,"1",key,msg,scope,ttl_sec
        };
        auto res = call(argv);
        if (!res || res->empty) {
            LOG_INFO<<"RedisObject::zadd(): 添加有序列表 "<<key<<" 错误: "<<msg;
            return false;
        }
        return true;
    }

    vector<string> zrange(const string& key,const string& start,const string& end,const bool& is_del) {
        if (!RedisEnsure()) {
            return {};
        }
        string lua_str;
        if (!GetLuaScript(REDIS_ZRANGE_LUA_PATH,lua_str)) {
            return {};
        }
        vector<string> argv{
            "EVAL",lua_str,"1",key,start,end,is_del?"1":"0"
        };
        auto res = call(argv);
        if (!res || res->empty || !res->count) {
            LOG_INFO<<"RedisObject::zrange(): 获取有序列表元素 "<<key<<" 错误";
            return {};
        }
        return *res->vec;
    }

    void update_rsl(const string& uid,const string& session_id,const string& msg,const string& ttl_sec) {
        if (!RedisEnsure()) {
            return;
        }
        string lua_str;
        if (!GetLuaScript(REDIS_UPDATE_RSL_LUA_PATH,lua_str)) {
            return;
        }
        vector<string> argv{
            "EVAL",lua_str,"2",uid,session_id,msg,ttl_sec
        };
        call(argv);
    }

    vector<vector<string>> get_rsl(const string& uid,const string& ttl,const int& field_count = 3) {
        if (!RedisEnsure()) {
            return {};
        }
        string lua_str;
        if (!GetLuaScript(REDIS_GET_RSL_LUA_PATH,lua_str)) {
            return {};
        }
        vector<string> argv{
            "EVAL",lua_str,"1",uid,ttl
        };
        auto res = call(argv);
        if (!res || res->empty || !res->count) {
            LOG_INFO<<"RedisObject::get_rsl(): 最近访问记录为空";
            return {};
        }
        vector<vector<string>> rsl;
        if (field_count > 0) {
            rsl.reserve(res->count / field_count);
        }
        try{
            for (auto i = 0;i < res->count;i += field_count) {
                vector<string> item;
                item.reserve(field_count);
                for (auto j = 0;j < field_count;++j)
                    item.emplace_back(res->vec->at(i+j));
                rsl.emplace_back(std::move(item));
            }
            return rsl;
        }catch (...) {
            LOG_INFO<<"RedisObject::get_rsl(): 解析rsl结果错误";
            return {};
        }
    }


    bool lock(const string& key,const string& value,const string& ttl) {
        if (!RedisEnsure()) {
            return false;
        }
        string lua_str;
        if (!GetLuaScript(REDIS_LOCK_LUA_PATH,lua_str)) {
            return false;
        }
        vector<string> argv{
            "EVAL",lua_str,"1",key,value,ttl
        };
        auto res = call(argv);
        if (!res || res->empty) {
            LOG_INFO<<"RedisObject::lock(): 获取分布式锁 "<<key<<" 错误";
            return false;
        }
        if (res->res == 0) {
            LOG_INFO<<"RedisObject::lock(): 获取分布式锁 "<<key<<" 失败，锁已被占用";
            return false;
        }
        return true;
    }

    bool unlock(const string& key,const string& value) {
        if (!RedisEnsure()) {
            return false;
        }
        string lua_str;
        if (!GetLuaScript(REDIS_UNLOCK_LUA_PATH,lua_str)) {
            return false;
        }
        vector<string> argv{
            "EVAL",lua_str,"1",key,value
        };
        auto res = call(argv);
        if (!res || res->empty) {
            LOG_INFO<<"RedisObject::unlock(): 释放分布式锁 "<<key<<" 错误";
            return false;
        }
        if (res->res == 0) {
            LOG_INFO<<"RedisObject::unlock(): 释放分布式锁 "<<key<<" 失败，锁已不属于自己";
            return false;
        }
        return true;
    }

    bool exist(const string& key) {
        if (!RedisEnsure()) {
            return false;
        }

        vector<string> argv{
            "EXISTS",key
        };
        auto res = call(argv);
        if (res->res == 0) {
            return false;
        }
        return true;
    }

    string bloom_find(const string& bf_key,const string& key,const uint64_t& ttl) {
        if (!RedisEnsure()) {
            return {};
        }
        string lua_str;
        if (!GetLuaScript(REDIS_FIND_LUA_PATH,lua_str)) {
            return {};
        }
        vector<string> argv{
            "EVAL",lua_str,"2",bf_key,key,to_string(ttl)
        };
        try {
            auto res = call(argv);
            if (!res || !res->vec || res->empty) {
                LOG_INFO<<"RedisObject::bloom_find(): Redis查询 "<<key<<" 错误";
                return {};
            }
            int ret_type = stoi(res->vec->at(0));
            if (ret_type == -1) {
                LOG_INFO<<"RedisObject::bloom_find(): 键 "<<key<<" 不存在";
                return {};
            }else if (ret_type == 0) {
                LOG_INFO<<"RedisObject::bloom_find(): 键 "<<key<<" 不存在于Redis,继续在数据库中查找";
                return "__CONTINUE__";
            }
            return res->vec->at(1);
        }catch (const exception& e) {
            LOG_ERROR<<"RedisObject::bloom_find(): 查找 "<<key<<" 错误："<<e.what();
            return {};
        }
    }

    string find(const string& key,const uint64_t& ttl) {
        if (!RedisEnsure()) {
            return {};
        }
        string lua_str;
        if (!GetLuaScript(REDIS_FIND_NO_BLOOM_LUA_PATH,lua_str)) {
            return {};
        }
        vector<string> argv{
            "EVAL",lua_str,"1",key,to_string(ttl)
        };
        try {
            auto res = call(argv);
            if (!res || !res->vec || res->empty) {
                LOG_INFO<<"RedisObject::find(): Redis查询 "<<key<<" 错误";
                return {};
            }
            int ret_type = stoi(res->vec->at(0));
            if (ret_type == 0) {
                LOG_INFO<<"RedisObject::find(): 键 "<<key<<" 不存在于Redis,继续在数据库中查找";
                return string("__CONTINUE__");
            }
            return res->vec->at(1);
        }catch (const exception& e) {
            LOG_ERROR<<"RedisObject::find(): 查找 "<<key<<" 错误："<<e.what();
            return {};
        }
    }

    bool add(const string& key,const string& value,const uint64_t& ttl) {
        if (!RedisEnsure()) {
            return false;
        }
        string lua_str;
        if (!GetLuaScript(REDIS_ADD_LUA_PATH,lua_str)) {
            return false;
        }
        vector<string> argv{
            "EVAL",lua_str,"1",key,value,to_string(ttl)
        };
        auto res = call(argv);
        if (!res || res->empty) {
            LOG_INFO<<"RedisObject::add(): Redis缓存 "<<key<<" 错误";
            return false;
        }
        if (res->res == 0) {
            LOG_INFO<<"RedisObject::add(): Redis缓存 "<<key<<" 失败，键已存在";
            return false;
        }
        return true;
    }

    bool del(const string& key) {
        if (!RedisEnsure()) {
            return false;
        }
        string lua_str;
        if (!GetLuaScript(REDIS_REMOVE_LUA_PATH,lua_str)) {
            return false;
        }
        vector<string> argv{
            "EVAL",lua_str,"1",key
        };
        auto res = call(argv);
        if (!res || res->empty) {
            LOG_INFO<<"RedisObject::del(): Redis删除 "<<key<<" 错误";
            return false;
        }
        if (res->res == 0) {
            LOG_INFO<<"RedisObject::del(): Redis删除 "<<key<<" 失败，键不存在";
            return false;
        }
        return true;
    }

    bool bf_add(const string& bf_key,const string& key) {
        if (!RedisEnsure()) {
            return false;
        }
        vector<string> argv{
            "BF.ADD",bf_key,key
        };
        auto res = call(argv);
        if (!res || res->empty) {
            LOG_INFO<<"RedisObject::bf_add(): Bloom过滤器 "<<bf_key<<" 添加键 "<<key<<" 错误";
            return false;
        }
        if (res->res == 0) {
            LOG_INFO<<"RedisObject::bf_add(): Bloom过滤器 "<<bf_key<<" 添加键 "<<key<<" 失败,键已存在";
            return false;
        }
        return true;
    }

    bool bf_exists(const string& bf_key,const string& key) {
        if (!RedisEnsure()) {
            return false;
        }
        vector<string> argv{
            "BF.EXISTS",bf_key,key
        };
        auto res = call(argv);
        if (!res || res->empty) {
            LOG_INFO<<"RedisObject::bf_exists(): Bloom过滤器 "<<bf_key<<" 添加键 "<<key<<" 错误";
            return false;
        }
        if (res->res == 0)
            return false;
        return true;
    }

};



class LockRenewThread {
private:
    struct RenewTask {
        uint64_t ttl;
        atomic<chrono::steady_clock::time_point> last_renew_time{chrono::steady_clock::now()};
        string lock_key;
        string lock_value;
        function<void(bool)> callback;

        RenewTask():ttl(0),callback(nullptr) {}
        RenewTask(const string& _key,const string& _value,const uint64_t& _ttl,const function<void(bool)>& _callback):
                    ttl(_ttl),
                    lock_key(_key),
                    lock_value(_value),
                    callback(_callback){}
        RenewTask(const RenewTask& right):
                    ttl(right.ttl),
                    lock_key(right.lock_key),
                    lock_value(right.lock_value),
                    callback(right.callback)
        {
            this->last_renew_time.store(right.last_renew_time.load(memory_order_acquire),memory_order_release);
        }
        RenewTask& operator=(const RenewTask& right) {
            if (this != &right) {
                this->ttl = right.ttl;
                this->lock_key = right.lock_key;
                this->lock_value = right.lock_value;
                this->callback = right.callback;
                this->last_renew_time.store(right.last_renew_time.load(memory_order_acquire),memory_order_release);
            }
            return *this;
        }
    };

    atomic<bool> running{false};
    chrono::milliseconds check_interval_ms;
    unordered_map<string,RenewTask> renew_tasks;
    mutex m;
    condition_variable cd;
    // 使用函数对象代替具体的 Redis 连接进行解耦
    function<bool(const string&, const string&, const uint64_t&)> renew_operation;
    thread renew_thread;

    //开启续期线程
    void startRenewScheduler() {
        renew_thread = thread([this]() {
            LOG_INFO << "锁续期调度器启动";
            vector<shared_ptr<RenewTask>> tasks_to_renew;
            while (running.load(memory_order_acquire)) {
                // 收集需要续期的任务
                tasks_to_renew.clear();
                {
                    unique_lock<mutex> lock(m);

                    // 等待直到有任务需要续期或超时
                    cd.wait_for(lock, this->check_interval_ms, [this]() {
                        return !running.load(memory_order_relaxed) || hasUrgentTasks();
                    });

                    if (!running.load(memory_order_acquire)) break;

                    auto now = chrono::steady_clock::now();
                    for (auto& [_, task] : renew_tasks) {
                        // 检查是否到了续期时间（过期时间的1/3）
                        auto next_renew = task.last_renew_time.load(memory_order_acquire) +
                                        chrono::seconds(task.ttl / 3);
                        if (now >= next_renew) {
                            tasks_to_renew.emplace_back(make_shared<RenewTask>(task));
                            //task.last_renew_time = now;
                        }
                    }
                }

                // 执行续期
                if (!tasks_to_renew.empty()) {
                    executeBatchRenew(tasks_to_renew);
                }
                cleanupExpiredTasks();
            }

            LOG_INFO << "锁续期调度器停止";
        });
    }

    void stopRenewScheduler() {
        bool t_running = true;
        if (!this->running.compare_exchange_strong(t_running, false,memory_order_release,memory_order_relaxed))
            return;
        cd.notify_all();
        if (renew_thread.joinable()) {
            renew_thread.join();
        }
        lock_guard<mutex> lock(m);
        this->renew_tasks.clear();
    }

    bool hasUrgentTasks() {
        auto now = chrono::steady_clock::now();
        for (const auto& [_, task] : renew_tasks) {
            auto time_until_renew = task.last_renew_time.load(memory_order_acquire) + chrono::milliseconds(task.ttl / 3);
            if ( now >= time_until_renew || (time_until_renew - now) <= this->check_interval_ms) {
                return true;
            }
        }
        return false;
    }

    void executeBatchRenew(const vector<shared_ptr<RenewTask>>& tasks) {
        LOG_DEBUG << "执行批量锁续期，数量: " << tasks.size();

        for (const auto& task : tasks) {
            bool success = this->renew_operation(task->lock_key, task->lock_value, task->ttl);
            task->last_renew_time.store(chrono::steady_clock::now(),memory_order_release);
            if (!success) {
                LOG_ERROR << "锁续期失败: " << task->lock_key;
                // 续期失败，从管理中移除
                unregisterLock(task->lock_key);
            }
            if (task->callback) {
                task->callback(success);
            }
        }
    }

    // 清理长时间没有续期的任务
    void cleanupExpiredTasks() {}
public:
    LockRenewThread(const chrono::milliseconds& interval,const function<bool(const string&,const string&,const uint64_t&)>& renew_opt):check_interval_ms(interval),renew_operation(renew_opt) {
        this->running.store(true,memory_order_release);
        this->startRenewScheduler();
    }

    ~LockRenewThread() {
        this->stopRenewScheduler();
    }
    // 注册锁续期
    bool registerLock(const string& lock_key,
                     const string& lock_value,
                     const uint64_t& ttl,
                     const function<void(bool)>& callback = nullptr) {
        {
            lock_guard<mutex> lock(m);
            renew_tasks[lock_key] = RenewTask(lock_key,lock_value,ttl,callback);
        }
        cd.notify_one();
        LOG_DEBUG << "注册锁续期: " << lock_key;
        return true;
    }

    // 取消锁续期
    bool unregisterLock(const string& lock_key) {
        lock_guard<mutex> lock(m);
        if (renew_tasks.erase(lock_key)) {
            LOG_DEBUG << "取消锁续期: " << lock_key;
            return true;
        }
        return false;
    }

    // 检查锁是否在续期管理中
    bool isLockRegistered(const string& lock_key) {
        lock_guard<mutex> lock(m);
        return renew_tasks.find(lock_key) != renew_tasks.end();
    }
};

template<typename T>
class ConnectPoolInterface {
public:
    virtual ~ConnectPoolInterface() = default;
    virtual shared_ptr<T> take() = 0;
    virtual shared_ptr<T> try_take() = 0;
    virtual bool recycle(shared_ptr<T>&) = 0;

    virtual size_t size() = 0;
    virtual void resize(const size_t&) = 0;

    virtual bool available() = 0;
};

template<typename T>
class ConnectPool{};

template<>
class ConnectPool<RedisObject>:public ConnectPoolInterface<RedisObject> {
    enum ConnStatus {
        ENABLE = 0,
        WORKING,
        CREATING,
        DESTROYED
    };
    struct RedisConn {
        atomic<ConnStatus> status{DESTROYED};
        atomic<uint64_t> last_update_time{0};
        shared_ptr<RedisObject> conn;
        RedisConn():conn(nullptr){}
        RedisConn(const shared_ptr<RedisObject>& _conn,const ConnStatus& statu = DESTROYED, const uint64_t& last_update = 0):status(statu),conn(_conn),last_update_time(last_update){}
        RedisConn(const RedisConn& right) {
            this->status.store(right.status.load());
            this->last_update_time.store(right.last_update_time.load());
            this->conn = right.conn;
        }
        RedisConn& operator=(const RedisConn& right) {
            this->status.store(right.status.load());
            this->last_update_time.store(right.last_update_time.load());
            this->conn = right.conn;
            return *this;
        }
        ~RedisConn(){conn.reset();}
    };
    atomic<bool> running{false};
    uint16_t retry_count;
    uint32_t base_size;   //最低连接数
    uint32_t max_size;    //最大连接数
    atomic<uint32_t> total_num{0};    //当前连接数
    atomic<uint32_t> active_num{0};    //当前连接数
    uint32_t connect_timeout_sec;  //连接超时时间
    uint32_t max_waiting_time_sec; //连接最大空闲时间
    uint32_t renew_check_interval_ms; //续期锁线程检测时间
    uint32_t port;
    atomic<uint32_t> available_offset{0}; //偏移量
    string host;
    shared_ptr<unordered_map<string,string>> lua_scripts;
    shared_ptr<vector<RedisConn>> pool;
    shared_ptr<LockRenewThread> renew_thread;

    shared_mutex m;
    thread clear_thread;

    shared_ptr<RedisObject> create_redis_object(){
        try {
            auto conn = make_shared<RedisObject>(this->lua_scripts,this->host,this->port,this->retry_count,this->connect_timeout_sec,this->renew_check_interval_ms);
            if (!conn) {
                LOG_WARN<<"ConnectPool<RedisObject>::create_redis_object(): 创建新Redis连接实例失败";
                return nullptr;
            }
            return conn;
        } catch (exception& e) {
            LOG_ERROR<<e.what();
            return nullptr;
        }
    }

    uint64_t getCurrentTimestamp() const {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    RedisConn* CreateNewConnection() {
        if (this->total_num.load(memory_order_acquire) >= this->max_size)
            return nullptr;
        ConnStatus except = ConnStatus::DESTROYED;
        // size_t size = this->total_num.load(memory_order_acquire);
        for (auto i = 0; i< this->max_size;++i) {
            auto& conn = pool->at(i);
            except = ConnStatus::DESTROYED;
            if (!conn.conn and conn.status.compare_exchange_strong(except,ConnStatus::CREATING,memory_order_acq_rel)) {
                shared_ptr<RedisObject> redis_conn = create_redis_object();
                if (!redis_conn) {
                    conn.status.store(DESTROYED,memory_order_release);
                    return nullptr;
                }
                pool->at(i).conn = redis_conn;
                this->total_num.fetch_add(1,memory_order_release);
                except = CREATING;
                if (!conn.status.compare_exchange_strong(except,ENABLE,memory_order_acq_rel)) {
                    conn.status.store(DESTROYED,memory_order_release);
                }
                return &conn;
            }
        }
        return nullptr;
    }

    RedisConn* findConnectionEntry(const std::shared_ptr<RedisObject>& conn) {
        shared_lock<shared_mutex> lock(m);
        for (auto& i : *this->pool) {
            if (i.conn.get() == conn.get()) {
                return &i;
            }
        }
        return nullptr;
    }

    //CAS获取
    shared_ptr<RedisObject> tryGetAvailableConnection() {
        size_t offset = available_offset.load(std::memory_order_acquire);

        for (size_t i = 0; i < max_size; ++i) {
            size_t index = (offset + i) % max_size;
            auto& conn = pool->at(index);
            // CAS获取连接
            ConnStatus expected = ENABLE;
            if (conn.conn && conn.status.compare_exchange_strong(expected,WORKING,memory_order_acq_rel)) {
                    conn.last_update_time.store(getCurrentTimestamp(),std::memory_order_release);
                    available_offset.store((index + 1) % max_size, std::memory_order_release);
                    active_num.fetch_add(1, std::memory_order_acq_rel);
                    return conn.conn;
                }
        }
        return nullptr;
    }

    //阻塞获取
    shared_ptr<RedisObject> blockGetAvailableConnection() {
        size_t offset = available_offset.load(std::memory_order_acquire);
        unique_lock<shared_mutex> lock(m);
        for (size_t i = 0; i < max_size; ++i) {
            size_t index = (offset + i) % max_size;
            auto& conn = pool->at(index);
            // CAS获取连接
            ConnStatus expected = ENABLE;
            if (conn.conn && conn.status.compare_exchange_strong(expected,WORKING,memory_order_acq_rel)) {
                conn.last_update_time.store(getCurrentTimestamp(),std::memory_order_release);
                available_offset.store((index + 1) % max_size, std::memory_order_release);
                active_num.fetch_add(1, std::memory_order_acq_rel);
                return conn.conn;
            }
        }
        return nullptr;
    }

    bool renew_opt(const string& key, const string& value, const uint64_t& ttl) {
        string renew_script = R"(
            if redis.call('GET', KEYS[1]) == ARGV[1] then
                return redis.call('PEXPIRE', KEYS[1], tonumber(ARGV[2]))
            else
                return 0
            end
        )";

        vector<string> argv = {
            "EVAL", renew_script, "1",
            key, value, to_string(ttl)
        };
        auto redis = this->try_take();
        if (!redis)
            return false;
        auto res = redis->call(argv);
        return res && !res->empty && res->res == 1;
    }

    void load_script(const vector<string>& scripts) {
        string lua_str;
        for (auto& script : scripts) {
            if (!fs::exists(script)) {
                LOG_ERROR<<"ConnectPool<RedisObject>::load_script(): Lua脚本 "<<script<<" 不存在";
                return;
            }
            ifstream lua_io(script);
            if (!lua_io.is_open()) {
                LOG_ERROR<<"ConnectPool<RedisObject>::load_script(): Lua脚本 "<<script<<" 打开失败";
                return;
            }
            stringstream buffer;
            buffer << lua_io.rdbuf();
            lua_str = buffer.str();
            lua_io.close();
            (*this->lua_scripts)[script] = lua_str;
        }
    }

    void init() {
        this->lua_scripts = make_shared<unordered_map<string,string>>();
        this->pool = make_shared<vector<RedisConn>>(this->max_size,RedisConn(nullptr,DESTROYED,getCurrentTimestamp()));
        // this->pool->reserve(this->max_size);
        this->load_script({
            REDIS_ADD_LUA_PATH,
            REDIS_FIND_LUA_PATH,
            REDIS_LOCK_LUA_PATH,
            REDIS_UNLOCK_LUA_PATH,
            REDIS_REMOVE_LUA_PATH,
            REDIS_FIND_NO_BLOOM_LUA_PATH,
            REDIS_GET_AND_REMOVE_LUA_PATH,
            REDIS_ZADD_LUA_PATH,
            REDIS_ZRANGE_LUA_PATH,
            REDIS_GET_RSL_LUA_PATH,
            REDIS_UPDATE_RSL_LUA_PATH
        });
        for (; this->total_num.load(memory_order_acquire) < this->base_size;) {
            this->CreateNewConnection();
        }

        this->renew_thread = make_shared<LockRenewThread>(chrono::milliseconds(this->renew_check_interval_ms),bind(&ConnectPool<RedisObject>::renew_opt,this,_1,_2,_3));
        auto clear_opt = [&]() {
            auto interval = chrono::milliseconds(this->renew_check_interval_ms);
            this_thread::sleep_for(interval);
            size_t count = 0;
            while (this->running.load(memory_order_acquire)) {
                {
                    if (this->total_num.load(memory_order_acquire) <= this->base_size) {
                        continue;
                    }
                    //获取需清理的连接
                    auto size = this->max_size;
                    for (auto i = 0; i < size; ++i) {
                        //只有清理线程才会减少pool的大小，而清理线程是单线程，因此不会有下标问题
                        // if (i>=this->pool->size())break;
                        auto& conn = this->pool->at(i);
                        if (conn.conn == nullptr || conn.status.load(memory_order_acquire) == WORKING) {
                            continue;
                        }
                        if (getCurrentTimestamp() - conn.last_update_time.load(memory_order_acquire) > max_waiting_time_sec*1000) {
                            if (conn.status.load(memory_order_acquire) == WORKING) {
                                continue;
                            }
                            conn.conn = nullptr;
                            conn.status.store(DESTROYED, std::memory_order_release);
                            this->total_num.fetch_sub(1, std::memory_order_release);
                            continue;
                        }
                        if (conn.conn == nullptr || !conn.conn->RedisEnsure()) {
                            //对该连接进行保活，如果失败表示异常，对其销毁
                            if (conn.status.load(memory_order_acquire) == WORKING) {
                                continue;
                            }
                            conn.conn = nullptr;
                            conn.status.store(DESTROYED, std::memory_order_release);
                            this->total_num.fetch_sub(1, std::memory_order_release);
                            continue;
                        }
                    }
                    if (this->active_num.load(memory_order_acquire) > this->total_num.load(memory_order_acquire))
                        this->active_num.store(this->total_num.load(memory_order_acquire), std::memory_order_release);
                }
                {
                    //维护最小连接数，只维护固定次数，避免长时间阻塞业务
                    // unique_lock<shared_mutex> lock(m);
                    count = this->total_num.load(memory_order_acquire);
                    while (count++ < this->base_size) {
                        this->CreateNewConnection();
                    }
                }
                this_thread::sleep_for(interval);
            }
        };
        this->clear_thread = thread(clear_opt);
    }

    void destroy() {
        this->running.store(false,memory_order_release);
        this->pool->clear();
        this->renew_thread.reset();
        if (this->clear_thread.joinable()) {
            this->clear_thread.join();
        }
    }


public:
    ConnectPool(const string& ip, const uint32_t& port,const uint16_t& retry,
        const uint32_t& min_num,const uint32_t& max_num,
        const uint32_t& timeout_sec,const uint32_t& waiting_sec,const uint32_t& renew_interval_ms):
    retry_count(retry),base_size(min_num),max_size(max_num),connect_timeout_sec(timeout_sec),max_waiting_time_sec(waiting_sec),renew_check_interval_ms(renew_interval_ms),port(port),host(ip) {
        this->running.store(true,memory_order_release);
        init();
    }
    ~ConnectPool() override {
        destroy();
    }

    //阻塞获取
    shared_ptr<RedisObject> take() override {
        // 无锁尝试获取可用连接
        if (!this->running.load(memory_order_acquire)) {return nullptr;}
        if (this->total_num.load(memory_order_acquire)> 0 ) {
            return blockGetAvailableConnection();
        }
        // 连接池未满时创建新连接
        if (total_num.load(std::memory_order_acquire) < max_size) {
            // unique_lock lock(m);
            auto conn = CreateNewConnection();
            if (conn) {
                conn->status.store(WORKING, std::memory_order_release);
                this->active_num.fetch_add(1,memory_order_acq_rel);
            }else {
                return nullptr;
            }
        }

        return nullptr;
    }

    shared_ptr<LockRenewThread> takeRenewThread() {
        return this->renew_thread;
    }

    shared_ptr<RedisObject> try_take() override {
        // 无锁尝试获取可用连接
        if (!this->running.load(memory_order_acquire)) {return nullptr;}
        if (this->total_num.load(memory_order_acquire)> 0 ) {
            return tryGetAvailableConnection();
        }
        // 连接池未满时创建新连接
        if (total_num.load(std::memory_order_acquire) < max_size) {
            // unique_lock lock(m);
            auto conn = CreateNewConnection();
            if (conn) {
                conn->status.store(WORKING, std::memory_order_release);
                this->active_num.fetch_add(1,memory_order_acq_rel);
            }else {
                return nullptr;
            }
        }
        return nullptr;
    }


    bool recycle(shared_ptr<RedisObject>& conn) override {
        if (!this->running.load(memory_order_acquire) || !conn) {return false;}

        // 查找连接条目并更新状态
        auto entry = findConnectionEntry(conn);
        ConnStatus expected = WORKING;
        if (entry && entry->status.compare_exchange_strong(expected,ENABLE)) {
            entry->last_update_time.store(getCurrentTimestamp(), std::memory_order_release);
            active_num.fetch_sub(1,memory_order_release);
            conn = nullptr;
            return true;
        }
        conn = nullptr;
        return false;
    }

    size_t size() override {
        return this->total_num.load(memory_order_acquire);
    }
    void resize(const size_t& size) override {
        return;
        unique_lock<shared_mutex> lock(m);
        this->max_size = size;
    }

    bool available() override {
        return this->running.load(memory_order_acquire);
    }

};


class RedisConnectionPoolBuilder:public enable_shared_from_this<RedisConnectionPoolBuilder>{
private:
    uint16_t retry_count;
    uint16_t base_size;   //最低连接数
    uint32_t max_size;    //最大连接数
    uint32_t connect_timeout_sec;  //连接超时时间
    uint32_t max_waiting_time_sec; //连接最大空闲时间
    uint32_t renew_check_interval_ms; //续期锁线程检测时间
    uint32_t port;
    string host;

    RedisConnectionPoolBuilder(const string& ip = REDIS_HOST,const uint32_t& port = REDIS_PORT,const uint16_t& retry = 3,
        const uint16_t& min_num = 10,const uint32_t& max_num = 15,
        const uint32_t& timeout_sec = 30,const uint32_t& waiting_sec = 20,const uint32_t& renew_interval_ms = 1000):
    retry_count(retry),base_size(min_num),max_size(max_num),connect_timeout_sec(timeout_sec),max_waiting_time_sec(waiting_sec),renew_check_interval_ms(renew_interval_ms),port(port),host(ip){

    }
public:
    RedisConnectionPoolBuilder(const RedisConnectionPoolBuilder&) = delete;
    RedisConnectionPoolBuilder& operator=(const RedisConnectionPoolBuilder&) = delete;
    RedisConnectionPoolBuilder(RedisConnectionPoolBuilder&&) = delete;
    RedisConnectionPoolBuilder& operator=(RedisConnectionPoolBuilder&&) = delete;

    ~RedisConnectionPoolBuilder() {}
    static shared_ptr<RedisConnectionPoolBuilder> GetInstance() {
        static shared_ptr<RedisConnectionPoolBuilder> builder = shared_ptr<RedisConnectionPoolBuilder>(new RedisConnectionPoolBuilder());
        return builder;
    }
    shared_ptr<RedisConnectionPoolBuilder> setRetryCount(const uint32_t& count) {
        this->retry_count = count;
        return shared_from_this();
    }
    shared_ptr<RedisConnectionPoolBuilder> setMinSize(const uint32_t& size) {
        this->base_size = size;
        return shared_from_this();
    }
    shared_ptr<RedisConnectionPoolBuilder> setMaxSize(const uint32_t& size) {
        this->max_size = size;
        return shared_from_this();
    }
    shared_ptr<RedisConnectionPoolBuilder> setConnTimeout(const uint32_t& sec) {
        this->connect_timeout_sec = sec;
        return shared_from_this();
    }
    shared_ptr<RedisConnectionPoolBuilder> setMaxWaitTime(const uint32_t& sec) {
        this->max_waiting_time_sec = sec;
        return shared_from_this();
    }
    shared_ptr<RedisConnectionPoolBuilder> setRenewInterval(const uint32_t& ms) {
        this->renew_check_interval_ms = ms;
        return shared_from_this();
    }
    shared_ptr<RedisConnectionPoolBuilder> setHost(const string& ip) {
        this->host = ip;
        return shared_from_this();
    }
    shared_ptr<RedisConnectionPoolBuilder> setPort(const uint32_t& _port) {
        this->port = _port;
        return shared_from_this();
    }

    /// 构建Redis连接池，全局只创建一次，因此再调用build()前，应完成对应的配置
    /// @return 成功返回shared_ptr<ConnectPool<RedisObject>>，失败返回nullptr
    shared_ptr<ConnectPool<RedisObject>> build() {
        static shared_ptr<ConnectPool<RedisObject>> redis_pool = make_shared<ConnectPool<RedisObject>>(this->host,this->port,this->retry_count,
                    this->base_size,this->max_size,
                    this->connect_timeout_sec,this->max_waiting_time_sec,this->renew_check_interval_ms);
        return redis_pool;
    }

};


template<>
class ConnectPool<MysqlObject>:public ConnectPoolInterface<MysqlObject> {
    enum ConnStatus {
        ENABLE = 0,
        WORKING,
        CREATING,
        DESTROYED
    };
    struct MysqlConn {
        atomic<ConnStatus> status{DESTROYED};
        atomic<uint64_t> last_update_time{0};
        shared_ptr<MysqlObject> conn;
        MysqlConn():conn(nullptr){}
        MysqlConn(const shared_ptr<MysqlObject>& _conn,const ConnStatus& statu = DESTROYED, const uint64_t& last_update = 0):status(statu),conn(_conn),last_update_time(last_update){}
        MysqlConn(const MysqlConn& right) {
            this->status.store(right.status.load());
            this->last_update_time.store(right.last_update_time.load());
            this->conn = right.conn;
        }
        MysqlConn& operator=(const MysqlConn& right) {
            this->status.store(right.status.load());
            this->last_update_time.store(right.last_update_time.load());
            this->conn = right.conn;
            return *this;
        }
        ~MysqlConn(){conn.reset();}
    };
    atomic<bool> running{false};
    uint16_t retry_count;
    uint32_t base_size;   //最低连接数
    uint32_t max_size;    //最大连接数
    atomic<uint32_t> total_num{0};    //当前连接数
    atomic<uint32_t> active_num{0};    //当前连接数
    uint32_t connect_timeout_sec;  //连接超时时间
    uint32_t max_waiting_time_sec; //连接最大空闲时间
    uint32_t keepalive_check_interval_ms; //保活线程检测时间
    atomic<uint32_t> available_offset{0}; //偏移量
    shared_ptr<ConnectionHead> conn_head;
    shared_ptr<vector<MysqlConn>> pool;

    shared_mutex m;
    thread clear_thread;

    shared_ptr<MysqlObject> create_mysql_object(){
        try {
            auto conn = make_shared<MysqlObject>(this->conn_head);
            if (!conn) {
                LOG_WARN<<"ConnectPool<MysqlObject>::create_mysql_object(): 创建新MySQL连接实例失败";
                return nullptr;
            }
            return conn;
        } catch (exception& e) {
            LOG_ERROR<<e.what();
            return nullptr;
        }
    }

    uint64_t getCurrentTimestamp() const {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    MysqlConn* CreateNewConnection() {
        if (this->total_num.load(memory_order_acquire) >= this->max_size)
            return nullptr;
        ConnStatus except = ConnStatus::DESTROYED;
        for (auto i = 0; i< this->max_size;++i) {
            auto& conn = pool->at(i);
            except = ConnStatus::DESTROYED;
            if (!conn.conn and conn.status.compare_exchange_strong(except,ConnStatus::CREATING,memory_order_acq_rel)) {
                shared_ptr<MysqlObject> redis_conn = create_mysql_object();
                if (!redis_conn) {
                    conn.status.store(DESTROYED,memory_order_release);
                    return nullptr;
                }
                pool->at(i).conn = redis_conn;
                this->total_num.fetch_add(1,memory_order_release);
                except = CREATING;
                if (!conn.status.compare_exchange_strong(except,ENABLE,memory_order_acq_rel)) {
                    conn.status.store(DESTROYED,memory_order_release);
                }
                return &conn;
            }
        }
        return nullptr;
    }

    MysqlConn* findConnectionEntry(const std::shared_ptr<MysqlObject>& conn) {
        shared_lock<shared_mutex> lock(m);
        for (auto& i : *this->pool) {
            if (i.conn.get() == conn.get()) {
                return &i;
            }
        }
        return nullptr;
    }

    //CAS获取
    shared_ptr<MysqlObject> tryGetAvailableConnection() {
        size_t offset = available_offset.load(std::memory_order_acquire);

        for (size_t i = 0; i < max_size; ++i) {
            size_t index = (offset + i) % max_size;
            auto& conn = pool->at(index);
            // CAS获取连接
            ConnStatus expected = ENABLE;
            if (conn.conn && conn.status.compare_exchange_strong(expected,WORKING,memory_order_acq_rel)) {
                conn.last_update_time.store(getCurrentTimestamp(),std::memory_order_release);
                available_offset.store((index + 1) % max_size, std::memory_order_release);
                active_num.fetch_add(1, std::memory_order_acq_rel);
                return conn.conn;
            }
        }
        return nullptr;
    }

    //阻塞获取
    shared_ptr<MysqlObject> blockGetAvailableConnection() {
        size_t offset = available_offset.load(std::memory_order_acquire);
        shared_lock<shared_mutex> lock(m);
        for (size_t i = 0; i < max_size; ++i) {
            size_t index = (offset + i) % max_size;
            auto& conn = pool->at(index);
            // CAS获取连接
            ConnStatus expected = ENABLE;
            if (conn.conn && conn.status.compare_exchange_strong(expected,WORKING,memory_order_acq_rel)) {
                conn.last_update_time.store(getCurrentTimestamp(),std::memory_order_release);
                available_offset.store((index + 1) % max_size, std::memory_order_release);
                active_num.fetch_add(1, std::memory_order_acq_rel);
                return conn.conn;
            }
        }
        return nullptr;
    }


    void init() {
        this->pool = make_shared<vector<MysqlConn>>(this->max_size,MysqlConn(nullptr,DESTROYED,getCurrentTimestamp()));
        for (; this->total_num.load(memory_order_acquire) < this->base_size;) {
            this->CreateNewConnection();
        }

        auto clear_opt = [&]() {
            auto interval = chrono::milliseconds(this->keepalive_check_interval_ms);
            this_thread::sleep_for(interval);
            size_t count = 0;
            while (this->running.load(memory_order_acquire)) {
                {
                    //获取需清理的连接
                    if (this->total_num.load(memory_order_acquire) <= this->base_size) {
                        continue;
                    }
                    auto size = this->max_size;
                    for (auto i = 0; i < size; ++i) {
                        //只有清理线程才会减少pool的大小，而清理线程是单线程，因此不会有下标问题
                        auto& conn = this->pool->at(i);
                        if (conn.conn == nullptr || conn.status.load(memory_order_acquire) == WORKING) {
                            continue;
                        }
                        if (getCurrentTimestamp() - conn.last_update_time.load(memory_order_acquire) > max_waiting_time_sec*1000) {
                            if (conn.status.load(memory_order_acquire) == WORKING) {
                                continue;
                            }
                            conn.conn = nullptr;
                            conn.status.store(DESTROYED, std::memory_order_release);
                            this->total_num.fetch_sub(1, std::memory_order_release);
                            continue;
                        }
                        if (conn.conn == nullptr || !conn.conn->EnsureMySQL()) {
                            //对该连接进行保活，如果失败表示异常，对其销毁
                            if (conn.status.load(memory_order_acquire) == WORKING) {
                                continue;
                            }
                            conn.conn = nullptr;
                            conn.status.store(DESTROYED, std::memory_order_release);
                            this->total_num.fetch_sub(1, std::memory_order_release);
                            continue;
                        }
                    }
                    if (this->active_num.load(memory_order_acquire) > this->total_num.load(memory_order_acquire))
                        this->active_num.store(this->total_num.load(memory_order_acquire), std::memory_order_release);
                }
                {
                    //维护最小连接数，只维护固定次数，避免长时间阻塞业务
                    // unique_lock<shared_mutex> lock(m);
                    count = this->total_num.load(memory_order_acquire);
                    while (count++ < this->base_size) {
                        this->CreateNewConnection();
                    }
                }
                this_thread::sleep_for(interval);
            }
        };
        this->clear_thread = thread(clear_opt);
    }

    void destroy() {
        this->running.store(false,memory_order_release);
        this->pool->clear();
        if (this->clear_thread.joinable()) {
            this->clear_thread.join();
        }
    }


public:
    ConnectPool(const shared_ptr<ConnectionHead>& head,const uint16_t& retry,
        const uint32_t& min_num,const uint32_t& max_num,
        const uint32_t& timeout_sec,const uint32_t& waiting_sec,const uint32_t& keepalive_interval_ms):
    retry_count(retry),base_size(min_num),max_size(max_num),connect_timeout_sec(timeout_sec),max_waiting_time_sec(waiting_sec),keepalive_check_interval_ms(keepalive_interval_ms),conn_head(head){
        this->running.store(true,memory_order_release);
        init();
    }
    ~ConnectPool() override {
        destroy();
    }

    //阻塞获取
    shared_ptr<MysqlObject> take() override {
        // 无锁尝试获取可用连接
        if (!this->running.load(memory_order_acquire)) {return nullptr;}
        if (this->total_num.load(memory_order_acquire)> 0 ) {
            return blockGetAvailableConnection();
        }
        // 连接池未满时创建新连接
        if (total_num.load(std::memory_order_acquire) < max_size) {
            // unique_lock lock(m);
            auto conn = CreateNewConnection();
            if (conn) {
                conn->status.store(WORKING, std::memory_order_release);
                this->active_num.fetch_add(1,memory_order_acq_rel);
            }else {
                return nullptr;
            }
        }

        return nullptr;
    }

    shared_ptr<MysqlObject> try_take() override {
        // 无锁尝试获取可用连接
        if (!this->running.load(memory_order_acquire)) {return nullptr;}
        if (this->total_num.load(memory_order_acquire)> 0 ) {
            return tryGetAvailableConnection();
        }
        // 连接池未满时创建新连接
        if (total_num.load(std::memory_order_acquire) < max_size) {
            // unique_lock lock(m);
            auto conn = CreateNewConnection();
            if (conn) {
                conn->status.store(WORKING, std::memory_order_release);
                this->active_num.fetch_add(1,memory_order_acq_rel);
            }else {
                return nullptr;
            }
        }

        return nullptr;
    }


    bool recycle(shared_ptr<MysqlObject>& conn) override {
        if (!this->running.load(memory_order_acquire) || !conn) {return false;}
        // 查找连接条目并更新状态
        auto entry = findConnectionEntry(conn);
        ConnStatus expected = WORKING;
        if (entry && entry->status.compare_exchange_strong(expected,ENABLE)) {
            entry->last_update_time.store(getCurrentTimestamp(), std::memory_order_release);
            active_num.fetch_sub(1,memory_order_release);
            conn = nullptr;
            return true;
        }
        conn = nullptr;
        return false;
    }

    size_t size() override {
        return this->total_num.load(memory_order_acquire);
    }
    void resize(const size_t& size) override {
        return;
        unique_lock<shared_mutex> lock(m);
        this->max_size = size;
    }

    bool available() override {
        return this->running.load(memory_order_acquire);
    }

};

class MysqlConnectionPoolBuilder:public enable_shared_from_this<MysqlConnectionPoolBuilder>{
private:
    uint16_t retry_count;
    uint16_t base_size;   //最低连接数
    uint32_t max_size;    //最大连接数
    uint32_t connect_timeout_sec;  //连接超时时间
    uint32_t max_waiting_time_sec; //连接最大空闲时间
    uint32_t keepalive_check_interval_ms; //续期锁线程检测时间
    shared_ptr<ConnectionHead> conn_head;

    MysqlConnectionPoolBuilder(const string& ip = MYSQL_HOST,const uint32_t& port = MYSQL_PORT_,
        const string& db_name = MYSQL_DEFAULT_DB,const string& user_name=MYSQL_USER,const string& pwd = MYSQL_PWD,const string& _charset = MYSQL_CHARSET,
        const uint16_t& retry = 3,
        const uint16_t& min_num = 10,const uint32_t& max_num = 15,
        const uint32_t& timeout_sec = 30,const uint32_t& waiting_sec = 20,const uint32_t& keepalive_interval_ms = 1000):
    retry_count(retry),base_size(min_num),max_size(max_num),
    connect_timeout_sec(timeout_sec),max_waiting_time_sec(waiting_sec),keepalive_check_interval_ms(keepalive_interval_ms){
        conn_head = make_shared<ConnectionHead>(ip,port,user_name,pwd,db_name,connect_timeout_sec,retry_count);
    }
public:
    MysqlConnectionPoolBuilder(const MysqlConnectionPoolBuilder&) = delete;
    MysqlConnectionPoolBuilder& operator=(const MysqlConnectionPoolBuilder&) = delete;
    MysqlConnectionPoolBuilder(MysqlConnectionPoolBuilder&&) = delete;
    MysqlConnectionPoolBuilder& operator=(MysqlConnectionPoolBuilder&&) = delete;

    ~MysqlConnectionPoolBuilder() {}
    static shared_ptr<MysqlConnectionPoolBuilder> GetInstance() {
        static shared_ptr<MysqlConnectionPoolBuilder> builder = shared_ptr<MysqlConnectionPoolBuilder>(new MysqlConnectionPoolBuilder());
        return builder;
    }
    shared_ptr<MysqlConnectionPoolBuilder> setRetryCount(const uint32_t& count) {
        this->retry_count = count;
        this->conn_head->retry = count;
        return shared_from_this();
    }
    shared_ptr<MysqlConnectionPoolBuilder> setMinSize(const uint32_t& size) {
        this->base_size = size;
        return shared_from_this();
    }
    shared_ptr<MysqlConnectionPoolBuilder> setMaxSize(const uint32_t& size) {
        this->max_size = size;
        return shared_from_this();
    }
    shared_ptr<MysqlConnectionPoolBuilder> setConnTimeout(const uint32_t& sec) {
        this->connect_timeout_sec = sec;
        this->conn_head->timeout = sec;
        return shared_from_this();
    }
    shared_ptr<MysqlConnectionPoolBuilder> setMaxWaitTime(const uint32_t& sec) {
        this->max_waiting_time_sec = sec;
        return shared_from_this();
    }
    shared_ptr<MysqlConnectionPoolBuilder> setRenewInterval(const uint32_t& ms) {
        this->keepalive_check_interval_ms = ms;
        return shared_from_this();
    }
    shared_ptr<MysqlConnectionPoolBuilder> setHost(const string& ip) {
        this->conn_head->host = ip;
        return shared_from_this();
    }
    shared_ptr<MysqlConnectionPoolBuilder> setPort(const uint32_t& _port) {
        this->conn_head->port = _port;
        return shared_from_this();
    }

    shared_ptr<MysqlConnectionPoolBuilder> setDatabase(const string& db_name) {
        this->conn_head->database = db_name;
        return shared_from_this();
    }

    shared_ptr<MysqlConnectionPoolBuilder> setUser(const uint32_t& user_name) {
        this->conn_head->user = user_name;
        return shared_from_this();
    }

    shared_ptr<MysqlConnectionPoolBuilder> setPwd(const uint32_t& _pwd) {
        this->conn_head->password = _pwd;
        return shared_from_this();
    }

    shared_ptr<MysqlConnectionPoolBuilder> setCharset(const uint32_t& _charset) {
        this->conn_head->charset = _charset;
        return shared_from_this();
    }

    shared_ptr<ConnectPool<MysqlObject>> build() {
        static shared_ptr<ConnectPool<MysqlObject>> mysql_pool = make_shared<ConnectPool<MysqlObject>>(
            this->conn_head,retry_count,base_size,max_size,connect_timeout_sec,max_waiting_time_sec,keepalive_check_interval_ms);
        return mysql_pool;
    }
};

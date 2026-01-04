//
// Created by laswer on 2025/11/13.
//
#ifndef DB_CACHE_H
#define DB_CACHE_H

#include "HeadFile.h"
#include "new_connectpool.hpp"
#include "TaskQuene.hpp"
#include "randomid.hpp"
#include "sslservice.hpp"
#include "dbhandler/user_handler.hpp"

#define CACHE_KEY_STRATEGY_RAW      1  //原始字符串，用于调试
#define CACHE_KEY_STRATEGY_BASE64   2  //Base64
#define CACHE_KEY_STRATEGY_SHA256   3  //SHA256

// 设置当前策略
#define CACHE_KEY_STRATEGY CACHE_KEY_STRATEGY_RAW

class DBCacheWriteException : public runtime_error {
private:
    string DBtype;
    string ErrInfo;
    string message;
public:
    DBCacheWriteException(const string& msg,const string& type,const string& info):runtime_error(msg),DBtype(type),ErrInfo(info) {}
    string getMessage() {return message;}
    string getType() {return DBtype;}
    string getInfo() {return ErrInfo;}
};

class DBCache {
private:
    static random_device rd;
    mt19937_64 gen;
    mutex m;
    condition_variable cd;
    DBCache():gen(rd()) {
        //实例化连接池对象，使用默认参数
        RedisConnectionPoolBuilder::GetInstance()->build();
        MysqlConnectionPoolBuilder::GetInstance()->build();
    }

    // 序列化数据
    string serializeData(const json& data) {
        return data.dump();
    }

    // 反序列化数据
    json deserializeData(const string& data) {
        if (data.empty() || data == "__NULL__") {
            return json();
        }
        try {
            return json::parse(data);
        } catch (const exception& e) {
            LOG_ERROR << "DBCache::deserializeData(): parse data to json error: " << e.what();
            return json();
        }
    }

    //将MySQL查询获得的结果转换为JSON
    json praseMySQLResult(shared_ptr<MySQLResult> res) {
        //结果集为空
        if (!res or !res->FieldsNum)return json();
        json rows = json::array();
        try {
            for (auto row : *(res->str_vec)) {
                //遍历行
                json j;
                for (auto i = 0; i < res->FieldsNum; ++i) {
                    j[res->FieldsName->at(i)] =  row.at(i);
                }
                rows.emplace_back(j);
            }
            return rows;
        }catch (exception& e) {
            LOG_ERROR<<"DBCache::praseMySQLResult(): "<<e.what();
            return json();
        }
    }

    //base64
    string base64Encode(const string& input) {
        BIO* b64 = BIO_new(BIO_f_base64());
        BIO* bio = BIO_new(BIO_s_mem());
        if (!b64 or !bio) {
            if (b64)
                BIO_free(b64);
            if (bio)
                BIO_free(bio);
            throw runtime_error("DBCache:base64Encode(): BIO_new() failed");
        }
        //bio生命周期交给b64链式管理
        b64 = BIO_push(b64,bio);
        //将所有数据编码为一行
        BIO_set_flags(b64,BIO_FLAGS_BASE64_NO_NL);
        if (BIO_write(b64,input.c_str(),input.length())<=0) {
            BIO_free_all(b64);
            throw runtime_error("DBCache:base64Encode(): BIO_write() failed");
        }
        if (BIO_flush(b64) != 1) {
            BIO_free_all(b64);
            throw runtime_error("DBCache:base64Encode(): BIO_flush() failed");
        }
        BUF_MEM* mem_ptr = nullptr;
        BIO_get_mem_ptr(bio,&mem_ptr);
        string out;
        if (mem_ptr and mem_ptr -> length>0)
            //这段数据的内存交给out管理，此时如果对mem_ptr释放会造成重复释放
            out.assign(mem_ptr->data,mem_ptr->length);
        else {
            BIO_free_all(b64);
            throw runtime_error("DBCache:base64Encode(): BIO_get_mem_ptr() failed");
        }
        //会链式释放
        BIO_free_all(b64);
        return out;
    }

    //SHA256
    string SHA256Encode(const string& input) {
        unsigned char digest[SHA256_DIGEST_LENGTH];
        SHA256((unsigned char *)input.c_str(), input.length(), digest);
        //16进制转换，提高redis性能
        stringstream ss;
        for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
            ss<<std::hex<<std::setw(2)<<std::setfill('0') << int(digest[i]);
        }
        return ss.str();
    }
public:
    DBCache(const DBCache&) = delete;
    DBCache& operator=(const DBCache&) = delete;
    DBCache(DBCache&&) = delete;
    DBCache& operator=(DBCache&&) = delete;
    ~DBCache() = default;

    static shared_ptr<DBCache> GetInstance() {
        static shared_ptr<DBCache> instance = shared_ptr<DBCache>(new DBCache());
        return instance;
    }



    uint64_t GenerateTTL(const uint64_t& min,const uint64_t& max) {
        if (min <= 0 or min > max)
            return 0;
        unique_lock<mutex> lock(m);
        uniform_int_distribution<uint64_t> random_number(min,max);
        return random_number(gen);
    }

    /// 生成Redis缓存键，支持base64编码、SHA256哈希，默认返回生字符串，可在配置文件中修改策略
    /// @param database 缓存键所对应的ySQL的数据库
    /// @param table    缓存键所对应的MySQL的数据表
    /// @param primary  主键，对于联合主键，应手动进行处理后再处理
    /// @param hex      分隔符，默认":"
    /// @return str 处理后的缓存键
    string GeneratePrimaryKey(const string& database,const string& table,const string& primary,const string& hex = ":"){
        if (CACHE_KEY_STRATEGY == CACHE_KEY_STRATEGY_BASE64)
            return this->base64Encode(database+hex+table+hex+primary);
        else if (CACHE_KEY_STRATEGY == CACHE_KEY_STRATEGY_SHA256)
            return this->SHA256Encode(database+hex+table+hex+primary);
        else
            return database+hex+table+hex+primary;
    }

    /// 布隆过滤器检测
    /// @param key 查询缓存键
    /// @return 查询结果，如果连接池异常，抛出错误，此时应该向服务端返回服务器错误
    bool bm_exists(const string& key){
        auto pool = RedisConnectionPoolBuilder::GetInstance()->build();
        auto conn = pool->try_take();
        if (!conn) {
            pool->recycle(conn);
            throw runtime_error("DBCache:bm_exists(): pool does not exist or conn get faild");
        }
        bool res = conn->bf_exists(BLOOMKEY,key);
        pool->recycle(conn);
        return res;
    }

    /// 布隆过滤器添加
    /// @param key 添加缓存键
    /// @return 添加结果，如果连接池异常，抛出错误，此时应该向服务端返回服务器错误
    bool bm_add(const string& key){
        auto pool = RedisConnectionPoolBuilder::GetInstance()->build();
        auto conn = pool->try_take();
        if (!conn) {
            pool->recycle(conn);
            throw runtime_error("DBCache:bm_add(): pool does not exist or conn get faild");
        }
        bool res = conn->bf_add(BLOOMKEY,key);
        pool->recycle(conn);
        return res;
    }

    /// 带布隆过滤器的查找
    /// @param cache_key 缓存键
    /// @param ttl      超时时间，成功查询时会重置超时时间
    /// @param param_sql MySQL参数化查询语句
    /// @param args     MYSQL参数化查询语句参数
    /// @return 当存在时，返回缓存数据并转换为json，写入缓存，不存在则返回空json
    template<typename ... Args>
    json CacheFindWithBloom(const string& database,const string& table, const string& primary_key,const uint64_t& ttl,const string& hex, const string& param_sql, Args&&... args) {
        shared_ptr<RedisObject> conn = nullptr;
        shared_ptr<MysqlObject> mysql_conn = nullptr;
        auto cache_key = this->GeneratePrimaryKey(database,table,primary_key,hex);
        try{
            auto pool = RedisConnectionPoolBuilder::GetInstance()->build();
            conn = pool->try_take();
            if (!conn) {
                throw runtime_error("DBCache:CacheFindWithBloom(): redis pool does not exist or conn get faild");
            }
            string redis_res = conn->bloom_find(BLOOMKEY,cache_key,ttl);
            if (redis_res != "__CONTINUE__" ) {
                //此时要么存在缓存中，返回缓存值，要么不存在数据库中，返回空
                pool->recycle(conn);
                return deserializeData(redis_res);
            }
            //继续查找数据库
            auto mysql_pool = MysqlConnectionPoolBuilder::GetInstance()->build();
            mysql_conn = mysql_pool->try_take();
            if (!mysql_conn) {
                pool->recycle(conn);
                throw runtime_error("DBCache:CacheFindWithBloom(): mysql pool does not exist or conn get faild");
            }
            auto mysql_res = mysql_conn->excuteDML_param(param_sql,std::forward<Args>(args)...);
            mysql_pool->recycle(mysql_conn);
            if (mysql_res == nullptr) {
                pool->recycle(conn);
                return json();
            }
            json query_res = praseMySQLResult(mysql_res);
            //当查询结果为空,缓存空值防止击穿
            if (query_res.empty()) {
                conn->add(cache_key,"__NULL__",ttl);
                pool->recycle(conn);
                return json();
            }
            //当查询结果不为空，将其写入缓存
            conn->add(cache_key,serializeData(query_res),ttl);
            pool->recycle(conn);
            return query_res;
        }catch (exception& e) {
            if (conn) {
                RedisConnectionPoolBuilder::GetInstance()->build()->recycle(conn);
            }
            if (mysql_conn) {
                MysqlConnectionPoolBuilder::GetInstance()->build()->recycle(mysql_conn);
            }
            LOG_ERROR<<"DBCache:CacheFindWithBloom(): "<<cache_key<<" -- "<<param_sql<<" -- "<<e.what();
            return json();
        }
    }

    /// 带布隆过滤器的查找
    /// @param cache_key 缓存键
    /// @param ttl      超时时间，成功查询时会重置超时时间
    /// @param param_sql MySQL参数化查询语句
    /// @param args     MYSQL参数化查询语句参数
    /// @return 当存在时，返回缓存数据并转换为json，写入缓存，不存在则返回空json
    template<typename ... Args>
    json CacheFind(const string& database,const string& table, const string& primary_key,const uint64_t& ttl,const string& hex, const string& param_sql, Args&&... args) {
        shared_ptr<RedisObject> conn = nullptr;
        shared_ptr<MysqlObject> mysql_conn = nullptr;
        auto cache_key = this->GeneratePrimaryKey(database,table,primary_key,hex);
        string lock_key = "LOCK:CacheFind:"+primary_key;
        string lock_id = SSLService::generatorUUID();
        bool is_locked = false;
        try{
            auto pool = RedisConnectionPoolBuilder::GetInstance()->build();
            conn = pool->try_take();
            if (!conn) {
                LOG_DEBUG<<"DBCache:CacheFind(): redis pool does not exist or conn get faild";
                return {};
            }
            string redis_res = conn->find(cache_key,ttl);
            if (redis_res != "__CONTINUE__" ) {
                //此时要么存在缓存中，返回缓存值，要么不存在数据库中，返回空
                pool->recycle(conn);
                return deserializeData(redis_res);
            }
            //继续查找数据库
            //分布式锁保证安全,上限60s
            if (!try_lock(lock_key,lock_id,60000)) {
                pool->recycle(conn);
                LOG_DEBUG<<"DBCache:CacheFind(): LOCK Failed";
                return {};
            }
            is_locked = true;

            auto mysql_pool = MysqlConnectionPoolBuilder::GetInstance()->build();
            mysql_conn = mysql_pool->try_take();
            if (!mysql_conn) {
                unlock(lock_key,lock_id);
                pool->recycle(conn);
                LOG_DEBUG<<"DBCache:CacheFind(): mysql pool does not exist or conn get faild";
                return {};
            }
            auto mysql_res = mysql_conn->excuteDML_param(param_sql,std::forward<Args>(args)...);
            mysql_pool->recycle(mysql_conn);
            if (mysql_res == nullptr) {
                unlock(lock_key,lock_id);
                pool->recycle(conn);
                return {};
            }
            json query_res = praseMySQLResult(mysql_res);
            //当查询结果为空,缓存空值防止击穿
            if (query_res.empty()) {
                conn->add(cache_key,"__NULL__",ttl);
                unlock(lock_key,lock_id);
                pool->recycle(conn);
                return {};
            }
            //当查询结果不为空,添加缓存
            conn->add(cache_key,serializeData(query_res),ttl);
            unlock(lock_key,lock_id);
            pool->recycle(conn);
            return query_res;
        }catch (exception& e) {
            if (conn) {
                RedisConnectionPoolBuilder::GetInstance()->build()->recycle(conn);
            }
            if (mysql_conn) {
                MysqlConnectionPoolBuilder::GetInstance()->build()->recycle(mysql_conn);
            }
            if (is_locked)
                unlock(lock_key,lock_id);
            LOG_ERROR<<"DBCache:CacheFind(): "<<cache_key<<" -- "<<param_sql<<" -- "<<e.what();
            return {};
        }
    }

    static bool try_lock(const string& cache_key,const string& value,const uint64_t& ttl) {
        shared_ptr<RedisObject> redis_conn = nullptr;
        try {
            auto redis_pool = RedisConnectionPoolBuilder::GetInstance()->build();
            redis_conn = redis_pool->try_take();
            if (!redis_conn) {
                LOG_ERROR<<"DBCache:try_lock(): redis pool does not exist or conn get faild";
                return false;
            }
            //加锁成功，添加锁续期
            if (redis_conn->lock(cache_key,value,to_string(ttl))) {
                redis_pool->takeRenewThread()->registerLock(cache_key,value,ttl);
                return true;
            }
            redis_pool->recycle(redis_conn);
            return false;
        }catch (exception& e) {
            if (redis_conn) {
                RedisConnectionPoolBuilder::GetInstance()->build()->recycle(redis_conn);
            }
            LOG_ERROR<<"DBCache:try_lock(): "<<cache_key<<" -- "<<value<<" -- "<<e.what();
            return false;
        }
    }

    static bool lock_for(const string& cache_key,const string& value,const uint64_t& ttl,const uint64_t& time,const uint64_t& count) {
        auto now = chrono::steady_clock::now();
        auto max_wait = chrono::milliseconds(time);
        auto interval = max_wait/count;
        try {
            for (auto i = chrono::steady_clock::now();i - now < max_wait;i = chrono::steady_clock::now()) {
                if (try_lock(cache_key,value,ttl)) {
                    return true;
                }
                this_thread::sleep_for(interval);
            }
            return false;
        }catch (exception& e) {
            LOG_ERROR<<"DBCache:lock(): "<<cache_key<<" -- "<<value<<" -- "<<e.what();
            return false;
        }
    }

    static void unlock(const string& cache_key,const string& value) {
        shared_ptr<RedisObject> redis_conn = nullptr;
        try {
            auto redis_pool = RedisConnectionPoolBuilder::GetInstance()->build();
            redis_conn = redis_pool->try_take();
            if (!redis_conn) {
                LOG_ERROR<<"DBCache:unlock(): redis pool does not exist or conn get faild";
                return;
            }
            if (redis_pool->takeRenewThread()->isLockRegistered(cache_key))
                redis_pool->takeRenewThread()->unregisterLock(cache_key);
            redis_conn->unlock(cache_key,value);
            redis_pool->recycle(redis_conn);
        }catch (exception& e) {
            if (redis_conn) {
                RedisConnectionPoolBuilder::GetInstance()->build()->recycle(redis_conn);
            }
            LOG_ERROR<<"DBCache:unlock(): "<<cache_key<<" -- "<<value<<" -- "<<e.what();
        }
    }

    ///更新数据库，延迟双删
    template<typename ... Args>
    bool CacheUpdate(const string& database,const string& table, const string& primary_key,const uint64_t& ttl,const string& hex, const string& param_sql,Args&&... args) {
        shared_ptr<RedisObject> redis_conn = nullptr;
        shared_ptr<MysqlObject> mysql_conn = nullptr;
        auto cache_key = this->GeneratePrimaryKey(database,table,primary_key,hex);
        auto lock_key = "LOCK:CacheUpdate:"+cache_key;
        try {
            auto redis_pool = RedisConnectionPoolBuilder::GetInstance()->build();
            redis_conn = redis_pool->try_take();
            if (!redis_conn) {
                throw runtime_error("DBCache:CacheUpdate(): redis pool does not exist or conn get faild");
            }
            redis_conn->del(cache_key);
            redis_pool->recycle(redis_conn);
            auto mysql_pool = MysqlConnectionPoolBuilder::GetInstance()->build();
            mysql_conn = mysql_pool->try_take();
            if (!mysql_conn) {
                throw runtime_error("DBCache:CacheUpdate(): redis pool does not exist or conn get faild");
            }
            //更新失败
            this->lock_for(lock_key,primary_key,ttl,1000,10);
            if (!mysql_conn->excuteDML_param(param_sql,std::forward<Args>(args)...)) {
                this->unlock(lock_key,primary_key);
                mysql_pool->recycle(mysql_conn);
                throw DBCacheWriteException("DBCache:CacheUpdate(): MySQL Excute DDL Operate " + param_sql +" Faild","MYSQL",param_sql);
            }
            this->unlock(lock_key,primary_key);
            mysql_pool->recycle(mysql_conn);
            //延迟删
            function<void()> func([&cache_key]() {
                shared_ptr<RedisObject> redis_conn = nullptr;
                try {
                    auto redis_pool = RedisConnectionPoolBuilder::GetInstance()->build();
                    redis_conn = redis_pool->try_take();
                    if (!redis_conn) {
                        throw runtime_error("DBCache:CacheUpdate(): redis pool does not exist or conn get faild");
                    }
                    redis_conn->del(cache_key);
                    redis_pool->recycle(redis_conn);
                }catch (exception& e) {
                    if (redis_conn) {
                        RedisConnectionPoolBuilder::GetInstance()->build()->recycle(redis_conn);
                    }
                    LOG_ERROR<<"DBCache:CacheUpdate(): delay delete error: "<<cache_key<<" -- "<<e.what();
                }
            });
            //添加定时任务，设为20ms
            shared_ptr<TimeWheelData> task = make_shared<TimeWheelData>(func,MILLISECONDS*2);
            TimeWheel5::getInstance()->InsertTimeWheel5(task);
            return true;
        }catch (exception& e) {
            if (redis_conn) {
                RedisConnectionPoolBuilder::GetInstance()->build()->recycle(redis_conn);
            }
            if (mysql_conn) {
                MysqlConnectionPoolBuilder::GetInstance()->build()->recycle(mysql_conn);
            }
            LOG_ERROR<<"DBCache:CacheUpdate(): "<<cache_key<<" -- "<<param_sql<<" -- "<<e.what();
            return false;
        }
    }


};
random_device DBCache::rd;

class distribute_lock {
    bool res;
    string cache_key;
    string cache_value;
public:
    explicit distribute_lock(const string& cache_key,const string& value,const uint64_t& ttl,const uint64_t& time = 0,const uint64_t& count = 0) {
        if (cache_key.empty() || value.empty()) {
            res = false;
            return;
        }
        this->cache_key = cache_key;
        this->cache_value = value;
        if (time and count)
            res = DBCache::lock_for(cache_key,value,ttl,time,count);
        else
            res = DBCache::try_lock(cache_key,value,ttl);
    }
    bool is_success() {
        return res;
    }
    void distroy() {
        if (res)
            DBCache::unlock(cache_key,cache_value);
        res = false;
    }
    ~distribute_lock() {
        distroy();
    }
};
#endif
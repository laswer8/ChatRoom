#pragma once

#include "dbcache.hpp"

class RedisHandler {
    shared_mutex mx;
public:
    ///获取最近会话记录
    vector<vector<string>> GetRSL(const string& uid,const uint64_t& ttl) {
        if (uid.empty()) {
            return {};
        }
        try{
            vector<vector<string>> res;
            {
                auto redispool = RedisConnectionPoolBuilder::GetInstance()->build();
                if (!redispool) {
                    return {};
                }
                shared_lock<shared_mutex> lock(mx);
                auto conn = redispool->try_take();
                if (!conn) {
                    //队列已满
                    return {};
                }
                res = conn->get_rsl(uid,to_string(ttl));
                redispool->recycle(conn);
            }
            return res;
        }catch(...) {
            return {};
        }
    }

    ///更新最近会话记录
    void UpdateRSL(const string& uid,const string& session_id,const string& msg,const uint64_t& ttl_sec) {
        if (uid.empty() || session_id.empty() || ttl_sec <= 0) {
            return;
        }
        try{
            auto redispool = RedisConnectionPoolBuilder::GetInstance()->build();
            if (!redispool) {
                return;
            }
            unique_lock<shared_mutex> lock(mx);
            auto conn = redispool->try_take();
            if (!conn) {
                //队列已满
                return;
            }
            conn->update_rsl(uid,session_id,msg,to_string(ttl_sec));
            redispool->recycle(conn);
        }catch(exception& e) {
            LOG_DEBUG<<e.what();
        }
    }

    ///添加有序列表
    bool ZADD(const string& key,const string& msg,const uint64_t& scope,const uint64_t& ttl_sec) {
        if (key.empty() || scope < 0 || ttl_sec <= 0) {
            return false;
        }
        try{
            bool res = false;
            {
                auto redispool = RedisConnectionPoolBuilder::GetInstance()->build();
                if (!redispool) {
                    return false;
                }
                unique_lock<shared_mutex> lock(mx);
                auto conn = redispool->try_take();
                if (!conn) {
                    //队列已满
                    return false;
                }
                res = conn->zadd(key,msg,to_string(scope),to_string(ttl_sec));
                redispool->recycle(conn);
            }
            return res;
        }catch(...) {
            return false;
        }
    }
    ///获取有序列表范围内的元素，并控制是否删除
    vector<string> ZRANGE(const string& key,const int& start,const int& stop, const bool& is_delete) {
        if (key.empty()) {
            return {};
        }
        try{
            vector<string> res;
            {
                auto redispool = RedisConnectionPoolBuilder::GetInstance()->build();
                if (!redispool) {
                    return {};
                }
                shared_lock<shared_mutex> lock(mx);
                auto conn = redispool->try_take();
                if (!conn) {
                    //队列已满
                    return {};
                }
                res = conn->zrange(key,to_string(start),to_string(stop),is_delete);
                redispool->recycle(conn);
            }
            return res;
        }catch(...) {
            return {};
        }
    }

    ///移除键
    bool RemoveKey(const string& key) {
        if (key.empty()) {
            return false;
        }
        try{
            bool res = false;
            {
                auto redispool = RedisConnectionPoolBuilder::GetInstance()->build();
                if (!redispool) {
                    return false;
                }
                unique_lock<shared_mutex> lock(mx);
                auto conn = redispool->try_take();
                if (!conn) {
                    //队列已满
                    return false;
                }
                res = conn->del(key);
                redispool->recycle(conn);
            }
            return res;
        }catch(...) {
            return false;
        }
    }
    ///缓存键
    bool CacheKey(const string& key,const string& value,const uint64_t& ttl) {
        if (key.empty() || value.empty() || ttl <= 0) {
            return false;
        }
        try{
            bool res = false;
            {
                auto redispool = RedisConnectionPoolBuilder::GetInstance()->build();
                if (!redispool) {
                    return false;
                }
                unique_lock<shared_mutex> lock(mx);
                auto conn = redispool->try_take();
                if (!conn) {
                    //队列已满
                    return false;
                }
                res = conn->add(key,value,ttl);
                redispool->recycle(conn);
            }
            return res;
        }catch(...) {
            return false;
        }
    }

    bool GetValue(const string& key,string& value,const uint64_t& ttl) {
        if (key.empty() or ttl <= 0) {
            return false;
        }
        try{
            {
                shared_lock<shared_mutex> lock(mx);
                auto redispool = RedisConnectionPoolBuilder::GetInstance()->build();
                if (!redispool) {
                    return false;
                }
                auto conn = redispool->try_take();
                if (!conn) {
                    //队列已满
                    return false;
                }
                value = conn->find(key,ttl);
                redispool->recycle(conn);
            }
            if (value.empty() or value == "__CONTINUE__")return false;
            return true;
        }catch(...) {
            return false;
        }
    }

    ///获取剩余过期时间
    bool GetTTL(const string& key,long long& ttl) {
        if (key.empty()) {
            return false;
        }
        try{
            {
                shared_lock<shared_mutex> lock(mx);
                auto redispool = RedisConnectionPoolBuilder::GetInstance()->build();
                if (!redispool) {
                    return false;
                }
                auto conn = redispool->try_take();
                if (!conn) {
                    //队列已满
                    return false;
                }
                ttl = conn->ttl(key);
                redispool->recycle(conn);
            }
            if (ttl == -3)
                return false;
            return true;
        }catch(...) {
            return false;
        }
    }

    ///生成超时时间
    uint64_t GenerateTTL(const uint64_t& min,const uint64_t& max) {
        try{
            unique_lock<shared_mutex> lock(mx);
            auto cache = DBCache::GetInstance();
            if (!cache) {
                return (min & max) + ((min ^ max) >> 1); // 防溢出中值
            }
            return cache->GenerateTTL(min,max);
        }catch(...) {
            return (min & max) + ((min ^ max) >> 1);
        }
    }

    bool FindToken(const string& key) {
        if (key.empty()) {
            return false;
        }
        try{
            bool res = false;
            {
                shared_lock<shared_mutex> lock(mx);
                auto redispool = RedisConnectionPoolBuilder::GetInstance()->build();
                if (!redispool) {
                    return false;
                }
                auto conn = redispool->try_take();
                if (!conn) {
                    //队列已满
                    return false;
                }
                res = conn->get_and_remove(key);
                redispool->recycle(conn);
            }
            return res;
        }catch(...) {
            return false;
        }
    }

    bool BfExistByUID(const string& database,const string& table,const string& uid) {
        try{
            //uid不为空，缓存查询
            auto cache = DBCache::GetInstance();
            if (!cache) {
                return false;
            }
            string primary_key = cache->GeneratePrimaryKey(database,table,uid);
            if (!cache->bm_exists(primary_key)) {
                //布隆过滤器表示不存在，直接返回
                return false;
            }
            return true;
        }catch (...) {
            return false;
        }
    }
    bool BfExistByAC(const string& database,const string& table,const string& key) {
        try{
            //uid不为空，缓存查询
            auto cache = DBCache::GetInstance();
            if (!cache) {
                return false;
            }
            string primary_key = cache->GeneratePrimaryKey(database,table,key);
            if (!cache->bm_exists(primary_key)) {
                //布隆过滤器表示不存在，直接返回
                return false;
            }
            return true;
        }catch (...) {
            return false;
        }
    }

    bool FindWithBloom(const string& key,const uint64_t& ttl,string& buffer) {
        try{
            shared_lock<shared_mutex> lock(mx);
            auto redispool = RedisConnectionPoolBuilder::GetInstance()->build();
            if (!redispool) {
                return false;
            }
            auto conn = redispool->try_take();
            if (!conn) {
                //队列已满
                return false;
            }
            buffer = conn->bloom_find(BLOOMKEY,key,ttl);
            redispool->recycle(conn);
            if (buffer.empty()) {
                return false;
            }else if (buffer == "__CONTINUE__") {
                buffer.clear();
                return false;
            }
            return true;
        }catch (...) {
            return false;
        }
    }

    bool CacheExist(const string& key) {
        try{
            shared_lock<shared_mutex> lock(mx);
            auto redispool = RedisConnectionPoolBuilder::GetInstance()->build();
            if (!redispool) {
                return false;
            }
            auto conn = redispool->try_take();
            if (!conn) {
                //队列已满
                return false;
            }
            bool res = conn->exist(key);
            redispool->recycle(conn);
            return res;
        }catch (...) {
            return false;
        }
    }
};
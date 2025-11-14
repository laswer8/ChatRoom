//
// Created by laswer on 2025/11/13.
//
#ifndef DB_CACHE_H
#define DB_CACHE_H

#include "HeadFile.h"
#include "new_connectpool.hpp"
#include "randomid.hpp"

class DBCache {
private:
    mutex m;
    condition_variable cd;
    DBCache() {
        //实例化连接池对象，使用默认参数
        RedisConnectionPoolBuilder::GetInstance()->build();
        MysqlConnectionPoolBuilder::GetInstance()->build();
    }
public:
    DBCache(const DBCache&) = delete;
    DBCache& operator=(const DBCache&) = delete;
    DBCache(DBCache&&) = delete;
    DBCache& operator=(DBCache&&) = delete;
    ~DBCache() {}

    static shared_ptr<DBCache> GetInstance() {
        static shared_ptr<DBCache> instance = shared_ptr<DBCache>(new DBCache());
        return instance;
    }



};

#endif
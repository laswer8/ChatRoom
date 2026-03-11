#pragma once

#include "../HeadFile.h"
#include "../new_connectpool.hpp"

class HistoryHandler {
public:
    static bool save(const string& uid, const string& session_id,const uint64_t& size_kb,const string& data) {
        if (uid.empty() || session_id.empty())return false;
        {
            auto mysql_pool = MysqlConnectionPoolBuilder::GetInstance()->build();
            if (!mysql_pool) {
                //数据库连接池发生错误，拒绝业务执行
                return false;
            }
            auto conn = mysql_pool->try_take();
            if (!conn) {
                //队列已满
                return false;
            }
            string sql = R"(INSERT INTO history(uid, session_id, size_kb, data) VALUES (?, ?, ?, ?))";
            conn->excuteDML_param(sql,stoull(uid),stoull(session_id),size_kb,data);
            mysql_pool->recycle(conn);
        }
        return true;
    }

    static string get(const string& uid, const string& session_id) {
        if (uid.empty() || session_id.empty())return {};
        shared_ptr<::MySQLResult> res = nullptr;
        {
            auto mysql_pool = MysqlConnectionPoolBuilder::GetInstance()->build();
            if (!mysql_pool) {
                //数据库连接池发生错误，拒绝业务执行
                return {};
            }
            auto conn = mysql_pool->try_take();
            if (!conn) {
                //队列已满
                return {};
            }
            string sql = R"(select data from history WHERE uid = ? AND session_id = ?)";
            res = conn->excuteDML_param(sql,stoull(uid));
            mysql_pool->recycle(conn);
        }
        if (!res || !res->FieldsNum || !res->res){
            //结果为空,用户不存在
            return {};
        }
        string history = res->str_vec->at(0).at(0);
        return history;
    }
};
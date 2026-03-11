#pragma once

#include "../HeadFile.h"
#include "../new_connectpool.hpp"

class OfflineHandler {
    static shared_mutex mx;
public:
    vector<string> GetUserOffline(const string& uid,const string& fid) {
        if (uid.empty() || fid.empty()) return{};
        try {
            shared_ptr<MySQLResult> res = nullptr;
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

            try{
                if (!conn->beginTransaction()) {

                }
                string sql = R"(SELECT data from offlinemsg WHERE uid = ? and fid = ? ORDER BY msg_no ASC)";
                conn->beginTransaction();
                res = conn->excuteDML_param(sql,stoull(uid),stoull(fid));
                sql = R"(DELETE FROM offlinemsg WHERE uid = ? and fid = ?)";
                conn->excuteDML_param(sql,stoull(uid),stoull(fid));
                conn->commit();
            }catch (exception &e) {
                conn->rollback();
                LOG_INFO<<e.what();
                mysql_pool->recycle(conn);
                return {};
            }
            mysql_pool->recycle(conn);
            if (!res || !res->res) {
                return {};
            }
            vector<string> rows;
            rows.reserve(res->res);
            for (auto& vec:*(res->str_vec)) {
                rows.emplace_back(vec[0]);
            }
            return rows;

        }catch (exception &e) {
            LOG_INFO<<e.what();
            return {};
        }
    }

    static bool AppendOffline(const string& uid, const string& fid,const int64_t& msg_no,const json& data) {
        if (uid.empty() || fid.empty() || data.empty()) return false;
        try{
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
            string sql = R"(INSERT INTO offlinemsg(uid,fid,msg_no,data) VALUES (?, ?, ?, ?))";
            conn->excuteDML_param(sql,stoull(uid),stoull(fid),msg_no,data.dump());
            mysql_pool->recycle(conn);
        }catch (exception& e) {
            LOG_DEBUG<<"DBHandler::getGroupList(): "<<e.what();
            return false;
        }
        return true;
    }


};
shared_mutex OfflineHandler::mx;
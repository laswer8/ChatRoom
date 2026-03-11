#pragma once

#include "../HeadFile.h"
#include "../new_connectpool.hpp"

class RequestHandler {
    static shared_mutex mx;
public:
    static json GetFriendRequestList(const string& uid) {
        if (uid.empty())return {};
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
            string sql = R"(SELECT req_id, fid , tid, status, send_at, message, data
FROM freq
WHERE tid = ?
UNION all
SELECT req_id, fid , tid, status, send_at, message, data
FROM freq
WHERE fid = ?
ORDER BY send_at DESC)";
            res = conn->excuteDML_param(sql,stoull(uid),stoull(uid));
            mysql_pool->recycle(conn);
        }
        if (!res || !res->FieldsNum || !res->res){
            //结果为空,用户不存在
            return {};
        }
        json freqlist = json::array();
        json freq;
        for (auto i = 0; i < res->res; ++i) {
            freq["req_id"] = res->str_vec->at(i).at(0);
            freq["fid"] = res->str_vec->at(i).at(1);
            freq["tid"] = res->str_vec->at(i).at(2);
            freq["status"] = res->str_vec->at(i).at(3);
            freq["send_at"] = res->str_vec->at(i).at(4);
            freq["message"] = res->str_vec->at(i).at(5);
            freq["data"] = res->str_vec->at(i).at(6);
            freqlist.emplace_back(freq);
        }
        return freqlist;
    }

    static json GetGroupRequestList(const string& uid) {
        if (uid.empty())return {};
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
            string sql = R"(SELECT
    greq.req_id,
    greq.gid,
    chatgroup.gname,
    greq.uid,
    greq.status,
    greq.send_at,
    greq.message,
    greq.data
FROM greq
LEFT JOIN chatgroup ON greq.gid = chatgroup.gid
WHERE greq.uid = ?
UNION ALL
SELECT
    greq.req_id,
    greq.gid,
    chatgroup.gname,
    greq.uid,
    greq.status,
    greq.send_at,
    greq.message,
    greq.data
FROM greq
-- 用户管理的群的申请
JOIN groupuser ON greq.gid = groupuser.gid
    AND groupuser.uid = ?
    AND groupuser.role IN (1, 2)
JOIN chatgroup ON greq.gid = chatgroup.gid
ORDER BY send_at DESC)";
            res = conn->excuteDML_param(sql,stoull(uid),stoull(uid));
            mysql_pool->recycle(conn);
        }
        if (!res || !res->FieldsNum || !res->res){
            //结果为空,用户不存在
            return {};
        }
        json greqlist = json::array();
        json freq;
        for (auto i = 0; i < res->res; ++i) {
            freq["req_id"] = res->str_vec->at(i).at(0);
            freq["gid"] = res->str_vec->at(i).at(1);
            freq["gname"] = res->str_vec->at(i).at(2);
            freq["uid"] = res->str_vec->at(i).at(3);
            freq["status"] = res->str_vec->at(i).at(4);
            freq["send_at"] = res->str_vec->at(i).at(5);
            freq["message"] = res->str_vec->at(i).at(6);
            freq["data"] = res->str_vec->at(i).at(7);
            greqlist.emplace_back(freq);
        }
        return greqlist;
    }

    static pair<bool,string> CheckFriendRequestValid(const string& fid,const string& tid) {
        if (fid.empty() || tid.empty())return {false,"param empty"};
        shared_ptr<::MySQLResult> res = nullptr;
        {
            auto mysql_pool = MysqlConnectionPoolBuilder::GetInstance()->build();
            if (!mysql_pool) {
                //数据库连接池发生错误，拒绝业务执行
                return {false,"Mysql Connect Pool Error"};
            }
            auto conn = mysql_pool->try_take();
            if (!conn) {
                //队列已满
                return {false,"Mysql Coonnection Take Failed"};
            }
            string sql = R"(SELECT
        EXISTS(
        SELECT 1 FROM freq WHERE fid = ? AND tid = ? AND status = 0
        UNION ALL
        SELECT 1 FROM freq WHERE fid = ? AND tid = ? AND status = 0
        LIMIT 1
        ) AS has_request,
        EXISTS(
        SELECT 1 FROM friend WHERE uid = ? AND fid = ?
        UNION ALL
        SELECT 1 FROM friend WHERE uid = ? AND fid = ?
        LIMIT 1
        ) AS is_friend,
        (SELECT COUNT(1) FROM friend WHERE uid = ?) >= 100 AS self_limit,
        (SELECT COUNT(1) FROM friend WHERE uid = ?) >= 100 AS friend_limit)";
            res = conn->excuteDML_param(sql,
                stoull(fid),stoull(tid),
                stoull(tid),stoull(fid),
                stoull(fid),stoull(tid),
                stoull(tid),stoull(fid),
                stoull(fid),stoull(tid));
            mysql_pool->recycle(conn);
        }
        if (!res || !res->FieldsNum || !res->res){
            //查询失败
            return {false,"Query Failed"};
        }
        json j;
        for (auto i = 0; i < res->FieldsNum; ++i) {
            j[res->FieldsName->at(i)] =  stoi(res->str_vec->at(0).at(i));
        }
        if (j["has_request"].get<int>()) {
            return {false,"Request Already Exist"};
        }
        if (j["is_friend"].get<int>()) {
            return {false,"Already friends"};
        }
        if (j["self_limit"].get<int>()) {
            return {false,"Reached friend limit"};
        }
        if (j["friend_limit"].get<int>()) {
            return {false,"The other party has reached the friend limit"};
        }
        return {true,"Success"};
    }

    static bool AppendFriendRequest(const string& req_id, const string& fid,const string& tid,const string& msg, const json& data) {
        if (req_id.empty() || fid.empty() || tid.empty() || data.empty())return false;
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
            string sql = R"(INSERT INTO freq(req_id, fid, tid, status, message, data) VALUES (?, ?, ?, 0, ?, ?))";
            conn->excuteDML_param(sql,stoull(req_id),stoull(fid),stoull(tid),msg,data.dump());
            mysql_pool->recycle(conn);
        }
        return true;
    }

    static bool AcquireFriendRequest(const string& req_id, const string& fid,const string& tid) {
        if (req_id.empty() || fid.empty() || tid.empty())return false;
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
            try{
                conn->beginTransaction();
                string sql = R"(UPDATE freq SET status = 1 WHERE req_id = ?)";
                conn->excuteDML_param(sql,stoull(req_id));
                sql = R"(INSERT INTO friend (uid, fid, data) VALUES (?, ?, ''), (?, ?, ''))";
                conn->excuteDML_param(sql,stoull(fid),stoull(tid),stoull(tid),stoull(fid));
                conn->commit();
            }catch (exception &e) {
                conn->rollback();
                LOG_INFO<<e.what();
                mysql_pool->recycle(conn);
                return false;
            }
            mysql_pool->recycle(conn);
        }
        return true;
    }

    static bool RejectFriendRequest(const string& req_id) {
        if (req_id.empty())return false;
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
            string sql = R"(UPDATE freq SET status = 2 WHERE req_id = ?)";
            conn->excuteDML_param(sql,stoull(req_id));
            mysql_pool->recycle(conn);
        }
        return true;
    }

    //请求是否存在、请求状态是否已处理、是否已是好友、双方是否达到好友上限（100）
    static pair<bool,string> CheckFriendRequestProcessable(const string& req_id, const string& fid,const string& tid) {
        if (req_id.empty() || fid.empty() || tid.empty())return {false,"param empty"};
        shared_ptr<::MySQLResult> res = nullptr;
        {
            auto mysql_pool = MysqlConnectionPoolBuilder::GetInstance()->build();
            if (!mysql_pool) {
                //数据库连接池发生错误，拒绝业务执行
                return {false,"Mysql Connect Pool Error"};
            }
            auto conn = mysql_pool->try_take();
            if (!conn) {
                //队列已满
                return {false,"Mysql Coonnection Take Failed"};
            }
            string sql = R"(SELECT
    EXISTS(
        SELECT 1 FROM freq WHERE req_id = ? AND status = 0
    ) AS has_request,
    EXISTS(
        SELECT 1 FROM friend WHERE uid = ? AND fid = ?
        UNION ALL
        SELECT 1 FROM friend WHERE uid = ? AND fid = ?
        LIMIT 1
    ) AS is_friend,
    (SELECT COUNT(1) FROM friend WHERE uid = ?) >= 100 AS self_limit,
    (SELECT COUNT(1) FROM friend WHERE uid = ?) >= 100 AS friend_limit)";
            res = conn->excuteDML_param(sql,
                stoull(req_id),
                stoull(fid),stoull(tid),
                stoull(tid),stoull(fid),
                stoull(fid),stoull(tid));
            mysql_pool->recycle(conn);
        }
        if (!res || !res->FieldsNum || !res->res){
            //查询失败
            return {false,"Query Failed"};
        }
        json j;
        for (auto i = 0; i < res->FieldsNum; ++i) {
            j[res->FieldsName->at(i)] =  stoi(res->str_vec->at(0).at(i));
        }
        if (j["is_friend"].get<int>()) {
            return {false,"Already friends"};
        }
        if (j["self_limit"].get<int>()) {
            return {false,"Reached friend limit"};
        }
        if (j["friend_limit"].get<int>()) {
            return {false,"The other party has reached the friend limit"};
        }
        if (j["has_request"].get<int>() == 0) {
            return {false,"Request Not Exist Or Already Processed"};
        }
        return {true,"Success"};
    }


    static pair<bool,string> CheckGroupRequestValid(const string& gid,const string& uid) {
        if (gid.empty() || uid.empty())return {false,"param empty"};
        shared_ptr<::MySQLResult> res = nullptr;
        {
            auto mysql_pool = MysqlConnectionPoolBuilder::GetInstance()->build();
            if (!mysql_pool) {
                //数据库连接池发生错误，拒绝业务执行
                return {false,"Mysql Connect Pool Error"};
            }
            auto conn = mysql_pool->try_take();
            if (!conn) {
                //队列已满
                return {false,"Mysql Coonnection Take Failed"};
            }
            string sql = R"(SELECT
    EXISTS(
        SELECT 1 FROM greq WHERE gid = ? AND uid = ? AND status = 0
    ) AS has_request,
    EXISTS(
        SELECT 1 FROM groupuser WHERE gid = ? AND uid = ?
    ) AS is_joined,
    (SELECT COUNT(1) FROM groupuser WHERE gid = ?) >= 100 AS is_full;)";
            res = conn->excuteDML_param(sql,
                stoull(gid),stoull(uid),
                stoull(gid),stoull(uid),
                stoull(gid));
            mysql_pool->recycle(conn);
        }
        if (!res || !res->FieldsNum || !res->res){
            //查询失败
            return {false,"Query Failed"};
        }
        json j;
        for (auto i = 0; i < res->FieldsNum; ++i) {
            j[res->FieldsName->at(i)] =  stoi(res->str_vec->at(0).at(i));
        }
        if (j["has_request"].get<int>()) {
            return {false,"Request Already Exist"};
        }
        if (j["is_joined"].get<int>()) {
            return {false,"Already Join Group"};
        }
        if (j["is_full"].get<int>()) {
            return {false,"The group is full"};
        }
        return {true,"Success"};
    }

    static pair<bool,string> CheckGroupRequestProcessable(const string& req_id, const string& gid,const string& uid) {
        if (req_id.empty() || gid.empty() || uid.empty())return {false,"param empty"};
        shared_ptr<::MySQLResult> res = nullptr;
        {
            auto mysql_pool = MysqlConnectionPoolBuilder::GetInstance()->build();
            if (!mysql_pool) {
                //数据库连接池发生错误，拒绝业务执行
                return {false,"Mysql Connect Pool Error"};
            }
            auto conn = mysql_pool->try_take();
            if (!conn) {
                //队列已满
                return {false,"Mysql Coonnection Take Failed"};
            }
            string sql = R"(SELECT
    EXISTS(
        SELECT 1 FROM greq WHERE req_id = ? AND status = 0
    ) AS has_request,
    EXISTS(
        SELECT 1 FROM groupuser WHERE gid = ? AND uid = ?
    ) AS is_joined,
    (SELECT COUNT(1) FROM groupuser WHERE gid = ?) >= 100 AS is_full;)";
            res = conn->excuteDML_param(sql,
                stoull(req_id),
                stoull(gid),stoull(uid),
                stoull(gid));
            mysql_pool->recycle(conn);
        }
        if (!res || !res->FieldsNum || !res->res){
            //查询失败
            return {false,"Query Failed"};
        }
        json j;
        for (auto i = 0; i < res->FieldsNum; ++i) {
            j[res->FieldsName->at(i)] =  stoi(res->str_vec->at(0).at(i));
        }

        if (j["is_joined"].get<int>()) {
            return {false,"Already Join Group"};
        }
        if (j["is_full"].get<int>()) {
            return {false,"The group is full"};
        }
        if (j["has_request"].get<int>() == 0) {
            return {false,"Request Not Exist Or Has Process"};
        }
        return {true,"Success"};
    }

    static bool AppendGroupRequest(const string& req_id, const string& gid,const string& uid,const string& msg, const json& data) {
        if (req_id.empty() || gid.empty() || uid.empty() || data.empty())return false;
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
            string sql = R"(INSERT INTO greq(req_id,gid,uid,message,data) VALUES (?, ?, ?, ?, ?))";
            conn->excuteDML_param(sql,stoull(req_id),stoull(gid),stoull(uid),msg,data.dump());
            mysql_pool->recycle(conn);
        }
        return true;
    }

    static bool AcquireGroupRequest(const string& req_id, const string& gid,const string& uid,const string& username) {
        if (req_id.empty() || gid.empty() || uid.empty() || username.empty())return false;
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
            try{
                conn->beginTransaction();
                string sql = R"(UPDATE greq SET status = 1 WHERE req_id = ?)";
                conn->excuteDML_param(sql,stoull(req_id));
                sql = R"(INSERT INTO groupuser (gid, uid, username,data) VALUES (?, ?, ?, ''))";
                conn->excuteDML_param(sql,stoull(gid),stoull(uid),stoull(username));
                conn->commit();
            }catch (exception& e) {
                conn->rollback();
                LOG_INFO<<e.what();
                mysql_pool->recycle(conn);
                return false;
            }
            mysql_pool->recycle(conn);
        }
        return true;
    }

    static bool RejectGroupRequest(const string& req_id) {
        if (req_id.empty())return false;
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
            string sql = R"(UPDATE greq SET status = 2 WHERE req_id = ?)";
            conn->excuteDML_param(sql,stoull(req_id));
            mysql_pool->recycle(conn);
        }
        return true;
    }
};
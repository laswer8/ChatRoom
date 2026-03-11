#pragma once

#include "../HeadFile.h"
#include "../new_connectpool.hpp"

class ChatGroupHandler {
    static shared_mutex mx;
public:
    static json GetGroupInfo(const string& gid) {
        if (gid.empty())return {};
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
            string sql = R"(SELECT chatgroup.gid, chatgroup.gname, chatgroup.ownerid, gu1.username, chatgroup.created_at, chatgroup.data,
COUNT(gu2.uid) AS member_count,MAX(gu2.last_active) AS last_active
FROM chatgroup
LEFT JOIN groupuser gu1 ON chatgroup.gid = gu1.gid AND chatgroup.ownerid = gu1.uid
LEFT JOIN groupuser gu2 ON chatgroup.gid = gu2.gid
WHERE chatgroup.gid = ?)";
            res = conn->excuteDML_param(sql,stoull(gid));
            mysql_pool->recycle(conn);
        }
        if (!res || !res->FieldsNum || !res->res){
            //结果为空
            return {};
        }
        json j;
        for (auto i = 0; i < res->FieldsNum; ++i) {
            j[res->FieldsName->at(i)] =  res->str_vec->at(0).at(i);
        }
        return j;
    }

    static json getGroupList(const string& uid) {
        if (uid.empty())return {};
        shared_ptr<::MySQLResult> res = nullptr;
        try{
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
            string sql = R"(SELECT chatgroup.gid,chatgroup.gname FROM groupuser INNER JOIN chatgroup ON chatgroup.gid = groupuser.gid WHERE groupuser.uid = ?)";
            res = conn->excuteDML_param(sql,stoull(uid));
            mysql_pool->recycle(conn);

        }catch (exception& e) {
            LOG_DEBUG<<"DBHandler::getGroupList(): "<<e.what();
            return {};
        }
        if (!res || !res->FieldsNum || !res->res){
            //结果为空,用户不存在
            return {};
        }
        json glist = json::array();
        json tmp;
        for (auto i = 0; i < res->res; ++i) {
            tmp["gid"] = res->str_vec->at(i).at(0);
            tmp["gname"] = res->str_vec->at(i).at(1);
            glist.emplace_back(tmp);
        }
        return glist;
    }

    static json getGroupMemberList(const string& gid) {
        if (gid.empty())return {};
        shared_ptr<::MySQLResult> res = nullptr;
        try{
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
            string sql = R"( SELECT uid, username, role, joined_at, last_active FROM groupuser WHERE gid = ?)";
            res = conn->excuteDML_param(sql,stoull(gid));
            mysql_pool->recycle(conn);
        }catch (exception& e) {
            LOG_DEBUG<<"DBHandler::getGroupMemberList(): "<<e.what();
            return {};
        }
        if (!res || !res->FieldsNum || !res->res){
            //结果为空,用户不存在
            return {};
        }
        json memberlist = json::array();
        json tmp;
        for (auto i = 0; i < res->res; ++i) {
            tmp["uid"] = res->str_vec->at(i).at(0);
            tmp["username"] = res->str_vec->at(i).at(1);
            tmp["role"] = res->str_vec->at(i).at(2);
            tmp["joined_at"] = res->str_vec->at(i).at(3);
            tmp["last_active"] = res->str_vec->at(i).at(4);
            memberlist.emplace_back(tmp);
        }
        return memberlist;
    }

    static vector<string> getGroupMemberIDList(const string& gid) {
        if (gid.empty())return {};
        shared_ptr<::MySQLResult> res = nullptr;
        try{
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
            string sql = R"( SELECT uid FROM groupuser WHERE gid = ?)";
            res = conn->excuteDML_param(sql,stoull(gid));
            mysql_pool->recycle(conn);
        }catch (exception& e) {
            LOG_DEBUG<<"DBHandler::getGroupMemberList(): "<<e.what();
            return {};
        }
        if (!res || !res->FieldsNum || !res->res){
            //结果为空,用户不存在
            return {};
        }
        vector<string> memberlist;
        for (auto i = 0; i < res->res; ++i) {
            memberlist.emplace_back(res->str_vec->at(i).at(0));
        }
        return memberlist;
    }

    static json ConditionSearch(string& cond, const string& type) {
        if (cond.empty() || type.empty()) return {};
        shared_ptr<::MySQLResult> res = nullptr;
        try{
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
            string sql;
            if (type == "id") {
                sql = R"(select gid, gname from chatgroup WHERE gid = ?)";
                res = conn->excuteDML_param(sql,stoull(cond));
            }
            else if (type == "name") {
                cond += "%";
                sql = R"(select gid, gname from chatgroup where gname LIKE ?)";
                res = conn->excuteDML_param(sql,cond);
            }else {
                return {};
            }
            mysql_pool->recycle(conn);
        }catch (exception& e) {
            LOG_DEBUG<<"DBHandler::ConditionSearch(): "<<e.what();
            return {};
        }
        if (!res || !res->FieldsNum || !res->res){
            //结果为空,用户不存在
            return {};
        }
        json result = json::array();
        json tmp;
        for (auto i = 0; i < res->res; ++i) {
            tmp["gid"] = res->str_vec->at(i).at(0);
            tmp["gname"] = res->str_vec->at(i).at(1);
            result.emplace_back(tmp);
        }
        return result;
    }



    static int getUserRole(const string& gid, const string& uid) {
        if (gid.empty() || uid.empty()) return -1;
        shared_ptr<::MySQLResult> res = nullptr;
        try{
            auto mysql_pool = MysqlConnectionPoolBuilder::GetInstance()->build();
            if (!mysql_pool) {
                //数据库连接池发生错误，拒绝业务执行
                return -1;
            }
            auto conn = mysql_pool->try_take();
            if (!conn) {
                //队列已满
                return -1;
            }
            string sql = R"(select role from groupuser WHERE gid = ? AND uid = ?)";
            res = conn->excuteDML_param(sql,stoull(gid),stoull(uid));
            mysql_pool->recycle(conn);
        }catch (exception& e) {
            LOG_DEBUG<<"DBHandler::getUserRole(): "<<e.what();
            return -1;
        }
        if (!res || !res->FieldsNum || !res->res){
            //结果为空,用户不存在
            return -1;
        }
        return stoi(res->str_vec->at(0).at(0));
    }

    static bool QuitGroup(const string& gid, const string& uid) {
        if (gid.empty() || uid.empty())return false;
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
                //利用SQL优化器的索引合并，简化操作
                string sql = R"(DELETE FROM groupuser WHERE gid = ? AND uid = ? AND role IN (0, 1))";
                conn->excuteDML_param(sql,stoull(gid),stoull(uid));
                sql = R"(DELETE FROM offlinemsg WHERE uid = ? AND fid = ?)";
                conn->excuteDML_param(sql,stoull(uid),stoull(gid));
                sql = R"(DELETE FROM history WHERE uid = ? AND session_id = ?)";
                conn->excuteDML_param(sql,stoull(uid),stoull(gid));
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



    static bool CreateGroup(const string& gid, const string& gname, const string& ownerid,const string& username, const string& data) {
        if (gid.empty() || gname.empty() || ownerid.empty() || data.empty())return false;
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
                //利用SQL优化器的索引合并，简化操作
                string sql = R"(INSERT INTO chatgroup(gid, gname,ownerid,data) VALUES (?, ?, ?, ?))";
                conn->excuteDML_param(sql,stoull(gid),gname,stoull(ownerid),data);
                sql = R"(INSERT INTO groupuser(gid, uid, username, role, data) VALUES (?, ?, ?, 2, ''))";
                conn->excuteDML_param(sql,stoull(gid),stoull(ownerid),username);
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

    static bool SetMemberRole(const string& gid, const string& uid, const int& role) {
        if (gid.empty() || uid.empty() || role < 0 || role > 1)return false;
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
                string sql = R"(UPDATE groupuser SET role = ? WHERE gid = ? AND uid = ? AND role IN (0, 1))";
                conn->excuteDML_param(sql,role,stoull(gid),stoull(uid));
            }catch (exception &e) {
                LOG_INFO<<e.what();
                mysql_pool->recycle(conn);
                return false;
            }
            mysql_pool->recycle(conn);
        }
        return true;
    }

    static bool DestoryGroup(const string& gid) {
        if (gid.empty())return false;
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
                //利用SQL优化器的索引合并，简化操作
                string sql = R"(DELETE FROM chatgroup WHERE gid = ?)";
                conn->excuteDML_param(sql,stoull(gid));
                sql = R"(DELETE FROM groupuser WHERE gid = ?)";
                conn->excuteDML_param(sql,stoull(gid));
                sql = R"(DELETE FROM offlinemsg WHERE fid = ?)";
                conn->excuteDML_param(sql,stoull(gid));
                sql = R"(DELETE FROM history WHERE session_id = ?)";
                conn->excuteDML_param(sql,stoull(gid));
                sql = R"(DELETE FROM greq WHERE gid = ?)";
                conn->excuteDML_param(sql,stoull(gid));
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

    static vector<string> GetGroupManagers(const string& gid) {
        if (gid.empty())return {};
        shared_ptr<::MySQLResult> res = nullptr;
        try{
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
            string sql = R"(select uid FROM groupuser WHERE gid = ? AND role IN (1, 2))";
            res = conn->excuteDML_param(sql,stoull(gid));
            mysql_pool->recycle(conn);

        }catch (exception& e) {
            LOG_DEBUG<<"DBHandler::GetGroupManagers(): "<<e.what();
            return {};
        }
        if (!res || !res->FieldsNum || !res->res){
            //结果为空,用户不存在
            return {};
        }
        vector<string> managers;
        for (auto i = 0; i < res->res; ++i) {
            managers.emplace_back(res->str_vec->at(i).at(0));
        }
        return managers;
    }
};


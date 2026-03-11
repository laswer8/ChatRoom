#pragma once

#include "../HeadFile.h"
#include "../new_connectpool.hpp"

class FriendHandler {
    static shared_mutex mx;
public:
    static json GetFriendList(const string& uid) {
        //select friend.fid, user.username from friend LEFT JOIN user ON friend.fid = user.uid WHERE friend.uid = ?
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
            string sql = R"(select friend.fid, user.username from friend LEFT JOIN user ON friend.fid = user.uid WHERE friend.uid = ?)";
            res = conn->excuteDML_param(sql,stoull(uid));
            mysql_pool->recycle(conn);
        }
        if (!res || !res->FieldsNum || !res->res){
            //结果为空,用户不存在
            return {};
        }
        json flist = json::array();
        json tmp;
        for (auto i = 0; i < res->res; ++i) {
            tmp["uid"] = res->str_vec->at(i).at(0);
            tmp["name"] = res->str_vec->at(i).at(1);
            flist.emplace_back(tmp);
        }
        return flist;
    }

    ///@return -1: 错误、0：不是、1：是
    static int is_Friend(const string& uid,const string& fid) {
        if (uid.empty() || fid.empty())return -1;
        shared_ptr<::MySQLResult> res = nullptr;
        {
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
            string sql = R"(SELECT
        EXISTS(
        SELECT 1 FROM friend WHERE uid = ? AND fid = ?
        UNION ALL
        SELECT 1 FROM friend WHERE uid = ? AND fid = ?
        LIMIT 1
        ) AS is_friend)";
            res = conn->excuteDML_param(sql,
                stoull(uid),stoull(fid),
                stoull(fid),stoull(uid));
            mysql_pool->recycle(conn);
        }
        if (!res || !res->FieldsNum || !res->res){
            //查询失败
            return -1;
        }
        json j;
        for (auto i = 0; i < res->FieldsNum; ++i) {
            j[res->FieldsName->at(i)] =  stoi(res->str_vec->at(0).at(i));
        }
        if (j["is_friend"].get<int>()) {
            return 1;
        }
        return 0;
    }

    static bool DelFriend(const string& uid, const string& fid) {
        if (uid.empty() || fid.empty())return false;
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
                string sql = R"(DELETE FROM friend WHERE uid = ? AND fid = ? OR uid = ? AND fid = ?)";
                conn->excuteDML_param(sql,stoull(uid),stoull(fid),stoull(fid),stoull(uid));
                sql = R"(DELETE FROM offlinemsg WHERE uid = ? AND fid = ? OR uid = ? AND fid = ?)";
                conn->excuteDML_param(sql,stoull(uid),stoull(fid),stoull(fid),stoull(uid));
                sql = R"(DELETE FROM history WHERE uid = ? AND session_id = ? OR uid = ? AND session_id = ?)";
                conn->excuteDML_param(sql,stoull(uid),stoull(fid),stoull(fid),stoull(uid));
                sql = R"(DELETE FROM offlinemsg WHERE uid = ? AND fid = ? OR uid = ? AND fid = ?)";
                conn->excuteDML_param(sql,stoull(uid),stoull(fid),stoull(fid),stoull(uid));
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



};
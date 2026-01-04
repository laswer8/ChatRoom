#ifndef HANDLER_USER_HPP
#define HANDLER_USER_HPP

#include "../HeadFile.h"
#include "../tables/user.hpp"
#include "../tables/userinfo.hpp"
#include "../dbcache.hpp"
#include "../new_connectpool.hpp"

class UserHandler {
    static shared_mutex mx;
public:

    ///获取用户详细信息
    static pair<shared_ptr<User>,shared_ptr<UserInfo>> getUserInfoByID(const string& uid) {
        if (uid.empty())return make_pair(nullptr,nullptr);
        shared_ptr<::MySQLResult> res = nullptr;
        {
            auto mysql_pool = MysqlConnectionPoolBuilder::GetInstance()->build();
            if (!mysql_pool) {
                //数据库连接池发生错误，拒绝业务执行
                return make_pair(nullptr,nullptr);
            }
            auto conn = mysql_pool->try_take();
            if (!conn) {
                //队列已满
                return make_pair(nullptr,nullptr);
            }
            string sql = R"(SELECT user.username, userinfo.phone, userinfo.email, userinfo.last_login FROM user INNER JOIN userinfo ON user.uid = userinfo.uid WHERE user.uid = ?)";
            res = conn->excuteDML_param(sql,uid);
            mysql_pool->recycle(conn);
        }
        if (!res || !res->FieldsNum || !res->res){
            //结果为空,用户不存在
            return make_pair(nullptr,nullptr);
        }
        json j;
        for (auto i = 0; i < res->FieldsNum; ++i) {
            j[res->FieldsName->at(i)] =  res->str_vec->at(0).at(i);
        }
        //获取结果
        return make_pair(make_shared<User>(0,j["username"].get<string>()),
            make_shared<UserInfo>(0,0,j["last_login"].get<uint64_t>(),
                j["phone"].get<string>(),j["email"].get<string>()));
    }

    static json getFriendList(const string& uid) {
        if (uid.empty())return {};
        try{
            //缓存查询
            auto db = DBCache::GetInstance();
            if (!db) {
                return {};
            }
            //好友列表缓存最长7天
            auto ttl = db->GenerateTTL(3600000*24*5,3600000*24*7);
            //查询sql
            string sql = R"(SELECT fid, username FROM user INNER JOIN friend ON user.uid = friend.fid WHERE friend.uid = ?)";
            json res = db->CacheFind("ChatRoom","friend",uid,ttl,":",sql,uid);
            if (res.empty()) {
                return {};
            }
            return res;
        }catch (exception& e) {
            LOG_DEBUG<<"DBHandler::getFriendList(): "<<e.what();
            return {};
        }
    }
    static json getGroupList(const string& uid) {
        if (uid.empty())return {};
        try{
            //缓存查询
            auto db = DBCache::GetInstance();
            if (!db) {
                return {};
            }
            //好友列表缓存最长7天
            auto ttl = db->GenerateTTL(3600000*24*5,3600000*24*7);
            //查询sql
            string sql = R"(SELECT groupuser.gid,chatgroup.gname FROM chatgroup INNER JOIN groupuser ON chatgroup.gid = groupuser.gid WHERE groupuser.uid = ?)";
            json res = db->CacheFind("ChatRoom","chatgroup",uid,ttl,":",sql,uid);
            if (res.empty()) {
                return {};
            }
            return res;
        }catch (exception& e) {
            LOG_DEBUG<<"DBHandler::getFriendList(): "<<e.what();
            return {};
        }
    }

    ///获取用户信息通过UID，避免因为redis缓存导致的信息泄露，只进行数据库查询，使用布隆过滤器拦截,查询成功返回用户信息，查询为空或者用户不存在返回nullptr，数据库连接错误抛出runtime_error异常
    static shared_ptr<User> getUserByID(const string& uid) {
        if (uid.empty())return nullptr;
        shared_ptr<::MySQLResult> res = nullptr;
        {
            auto mysql_pool = MysqlConnectionPoolBuilder::GetInstance()->build();
            if (!mysql_pool) {
                //数据库连接池发生错误，拒绝业务执行
                return nullptr;
            }
            auto conn = mysql_pool->try_take();
            if (!conn) {
                //队列已满
                return nullptr;
            }
            string sql = R"(select username,account,password from user where uid = ?)";
            res = conn->excuteDML_param(sql,uid);
            mysql_pool->recycle(conn);
        }
        if (!res || !res->FieldsNum || !res->res) {
            //执行失败
            return nullptr;
        }
        json j;
        for (auto i = 0; i < res->FieldsNum; ++i) {
            j[res->FieldsName->at(i)] =  res->str_vec->at(0).at(i);
        }
        //获取结果
        return make_shared<User>(stoull(uid),
            j["username"].get<string>(),
            j["account"].get<string>(),
            j["password"].get<string>());

    }

    ///获取用户信息通过账号,只能通过数据库查询,查询成功返回用户信息，查询为空返回nullptr，数据库连接错误抛出runtime_error异常
    static shared_ptr<User> getUserByAccount(string& account) {
        if (account.empty()) return nullptr;
        shared_ptr<::MySQLResult> res = nullptr;
        {
            auto mysql_pool = MysqlConnectionPoolBuilder::GetInstance()->build();
            if (!mysql_pool) {
                //数据库连接池发生错误，拒绝业务执行
                return nullptr;
            }
            auto conn = mysql_pool->try_take();
            if (!conn) {
                //队列已满
                return nullptr;
            }
            //服务端存储哈希后的密码，因此无法使用密码进行条件查询
            string sql = R"(select uid,username,password from user where account = ?)";
            res = conn->excuteDML_param(sql,account);
            mysql_pool->recycle(conn);
        }
        if (!res || !res->FieldsNum || !res->res) {
            //执行失败
            return nullptr;
        }
        json j;
        for (auto i = 0; i < res->FieldsNum; ++i) {
            j[res->FieldsName->at(i)] =  res->str_vec->at(0).at(i);
        }
        //获取结果
        return make_shared<User>(stoull(j["uid"].get<string>()),
            j["username"].get<string>(),
            account,
            j["password"].get<string>());
    }


};
shared_mutex UserHandler::mx;
#endif
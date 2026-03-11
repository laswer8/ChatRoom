#ifndef HANDLER_USER_HPP
#define HANDLER_USER_HPP

#include "../HeadFile.h"
#include "../tables/user.hpp"
#include "../tables/userinfo.hpp"
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
            string sql = R"(SELECT user.username,user.account, userinfo.phone, userinfo.email, userinfo.created_at FROM user INNER JOIN userinfo ON user.uid = userinfo.uid WHERE user.uid = ?)";
            res = conn->excuteDML_param(sql,stoull(uid));
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
        return make_pair(make_shared<User>(0,j["username"].get<string>(),j["account"].get<string>()),
            make_shared<UserInfo>(0,j["created_at"].get<uint64_t>(),0,
                j["phone"].get<string>(),j["email"].get<string>()));
    }

    static int userGroupNum(const string& uid) {
        if (uid.empty()) return -1;
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
            string sql = R"(select COUNT(1) from groupuser WHERE uid = ?)";
            res = conn->excuteDML_param(sql,stoull(uid));
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

    static void UpdateUserLastLogin(const string& uid) {
        if (uid.empty())return;
        try{
            auto mysql_pool = MysqlConnectionPoolBuilder::GetInstance()->build();
            if (!mysql_pool) {
                //数据库连接池发生错误，拒绝业务执行
                return;
            }
            auto conn = mysql_pool->try_take();
            if (!conn) {
                //队列已满
                return;
            }
            string sql = R"(update userinfo set last_login = NOW() where uid = ?)";
            conn->excuteDML_param(sql,stoull(uid));
            mysql_pool->recycle(conn);
        }catch (exception& e) {
            LOG_INFO<<"DBHandler::UpdateUserLastLogin(): "<<e.what();
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
            res = conn->excuteDML_param(sql,stoull(uid));
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
                sql = R"(SELECT uid, username FROM user WHERE uid = ?)";
                res = conn->excuteDML_param(sql,stoull(cond));
            }
            else if (type == "name") {
                cond += "%";
                sql = R"(SELECT uid, username FROM user WHERE username LIKE ?)";
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
            tmp["uid"] = res->str_vec->at(i).at(0);
            tmp["username"] = res->str_vec->at(i).at(1);
            result.emplace_back(tmp);
        }
        return result;
    }

    ///获取用户信息通过账号,只能通过数据库查询,查询成功返回用户信息，查询为空返回nullptr，数据库连接错误抛出runtime_error异常
    bool appendUser(const uint64_t& uid,const string& username,const string& account,const string& password) {
        if (account.empty()) return false;
        shared_ptr<::MySQLResult> res = nullptr;
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
            //服务端存储哈希后的密码，因此无法使用密码进行条件查询
            //使用触发器保持user表与userinfo表数据一致性
            string sql = R"(insert into user(uid,username,account,password) value(?,?,?,?))";
            res = conn->excuteDML_param(sql,uid,username,account,password);
            mysql_pool->recycle(conn);
        }
        if (!res || !res->res) {
            //执行失败
            return false;
        }
        return true;
    }


};
shared_mutex UserHandler::mx;
#endif
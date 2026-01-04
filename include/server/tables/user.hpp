#pragma once

#include "../HeadFile.h"
//用户聊天历史：好友对单个用户的聊天记录
class User {
private:
    uint64_t uid;
    string username;
    string account;
    string password;
    string data;
public:
    User(const uint64_t& id = 0,const string& name = "",const string& username = "",const string& pwd = "",const string& extra = ""):uid(id),username(name),account(username),password(pwd),data(extra){}
    uint64_t getUID() const { return uid; }
    string getName() const { return username; }
    string getUsername() const { return account; }
    string getPassword() const { return password; }
    string getData() const { return data; }

    void setUID(const uint64_t& id) { this->uid = id; }
    void setName(const string& name) { this->username = name; }
    void setUsername(const string& uname) { this->account = uname; }
    void setPassword(const string& pwd) { this->password = pwd; }
    void setData(const string& extra) { this->data = extra; }
};
//重载json转换
void from_json(const json& j,User& msg)
{
    msg.setUID(j["uid"].get<uint64_t>());
    msg.setName(j["username"].get<string>());
    msg.setUsername(j["account"].get<string>());
    msg.setPassword(j["password"].get<string>());
    msg.setData(j["data"].get<string>());
}

//定义to_json(json& j,const T& value)函数，用于反序列化
//class对象----->json对象
void to_json(json& j,const User& msg)
{
    j["uid"] = msg.getUID();
    j["username"] = msg.getName();
    j["account"] = msg.getUsername();
    j["password"] = msg.getPassword();
    j["data"] = msg.getData();
}

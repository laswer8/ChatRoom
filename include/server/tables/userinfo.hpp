#ifndef TABLE_USER_INFO_HPP
#define TABLE_USER_INFO_HPP

#include "../HeadFile.h"
//用户聊天历史：好友对单个用户的聊天记录
class UserInfo {
private:
    uint64_t uid;
    uint64_t created_at;
    uint64_t last_login;
    string phone;
    string email;
    string data;
public:
    UserInfo(const uint64_t& id = 0,const uint64_t& create_time = 0,const uint64_t& login_time = 0,const string& phone_number = "",const string& e_mail = "" ,const string& extra = ""):uid(id),created_at(create_time),last_login(login_time),phone(phone_number),email(e_mail),data(extra){}
    uint64_t getUID() const { return uid; }
    uint64_t getCreatedAt() const { return this->created_at; }
    uint64_t getLastLogin() const { return this->last_login; }
    string getPhone() const { return this->phone; }
    string getEmail() const { return this->email; }
    string getData() const { return this->data; }

    void setUID(const uint64_t& id) { this->uid = id; }
    void setCreatedAt(const uint64_t& create_time) { this->created_at = create_time; }
    void setLastLogin(const uint64_t& login_time) { this->last_login = login_time; }
    void setPhone(const string& phone_number) { this->phone = phone_number; }
    void setData(const string& extra) { this->data = extra; }
    void setEmail(const string& e_mail) { this->email = e_mail; }
};
//重载json转换
void from_json(const json& j,UserInfo& msg)
{
    msg.setUID(j["uid"].get<uint64_t>());
    msg.setCreatedAt(j["created_at"].get<uint64_t>());
    msg.setLastLogin(j["last_login"].get<uint64_t>());
    msg.setPhone(j["phone"].get<string>());
    msg.setEmail(j["email"].get<string>());
    msg.setData(j["data"].get<string>());
}


void to_json(json& j,const UserInfo& msg)
{
    j["uid"] = msg.getUID();
    j["created_at"] = msg.getCreatedAt();
    j["last_login"] = msg.getLastLogin();
    j["phone"] = msg.getPhone();
    j["email"] = msg.getEmail();
    j["data"] = msg.getData();
}


#endif
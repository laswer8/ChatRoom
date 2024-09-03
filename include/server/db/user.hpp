#ifndef USER_T
#define USER_T


#include "../HeadFile.h"

class User{
private:
    int id;
    string name;
    string password;
    string state;
public:
    User(const int& _id=-1,const string& _name="",const string& _pwd="",const string& _state = "offline"):id(_id),name(_name),password(_pwd),state(_state){}

    void setid(const int& _id){id = _id;}
    void setname(const string& _name){name = _name;}
    void setpassword(const string& _pwd){password = _pwd;}
    void setstate(const string& _state){state = _state;}

    int getid()const{return id;}
    string getname()const{return name;}
    string getpwd()const{return password;}
    string getstate()const{return state;}
};

void from_json(const json& j,User& msg)
{
    msg.setid(j["id"].get<int>());
    msg.setname(j["name"].get<string>());
    msg.setstate(j["state"].get<string>());
}

//定义to_json(json& j,const T& value)函数，用于反序列化
//class对象----->json对象
void to_json(json& j,const User& msg)
{
    j["id"] = msg.getid();
    j["name"] = msg.getname();
    j["state"] = msg.getstate();
}

#endif // !USER_T
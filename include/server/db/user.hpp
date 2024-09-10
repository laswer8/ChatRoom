#ifndef USER_T
#define USER_T


#include "../HeadFile.h"

class User{
private:
    int id;
    string name;
    string username;
    string password;
    string email;
    string phone;
    string state;
public:
    User(const int& _id=-1,const string& _name="",const string& _username="",const string& _pwd="",const string& _email="",const string& _phone="",const string& _state = "offline"):id(_id),name(_name),username(_username),password(_pwd),email(_email),phone(_phone),state(_state){}

    void setid(const int& _id){id = _id;}
    void setname(const string& _name){name = _name;}
    void setusername(const string& _username){username = _username;}
    void setpassword(const string& _pwd){password = _pwd;}
    void setemail(const string& _email){email = _email;}
    void setphone(const string& _phone){phone = _phone;}
    void setstate(const string& _state){state = _state;}

    int getid()const{return id;}
    string getname()const{return name;}
    string getusername()const{return username;}
    string getpwd()const{return password;}
    string getemail()const{return email;}
    string getphone()const{return phone;}
    string getstate()const{return state;}
};

void from_json(const json& j,User& msg)
{
    msg.setid(j["id"].get<int>());
    msg.setname(j["name"].get<string>());
    msg.setstate(j["state"].get<string>());
    msg.setusername(j["username"].get<string>());
    msg.setemail(j["email"].get<string>());
    msg.setphone(j["phone"].get<string>());
}

//定义to_json(json& j,const T& value)函数，用于反序列化
//class对象----->json对象
void to_json(json& j,const User& msg)
{
    j["id"] = msg.getid();
    j["name"] = msg.getname();
    j["username"] = msg.getusername();
    j["phone"] = msg.getphone();
    j["email"] = msg.getemail();
    j["state"] = msg.getstate();
}

#endif // !USER_T
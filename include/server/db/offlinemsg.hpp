#ifndef OFFLINE_MSG
#define OFFLINE_MSG

#include "../HeadFile.h"

class OfflineMsg{
    private:
        string username;
        string jsonMsg;
    public:
        string Getusername()const{return username;}
        string GetJsonMsg()const{return jsonMsg;}
        
        void Setusername(const string& _username){username = _username;}
        void SetJsonMsg(const string& str){jsonMsg = str;}
};
void from_json(const json& j,OfflineMsg& msg)
{
    msg.SetJsonMsg(j["JsonMessage"].get<string>());
}

//定义to_json(json& j,const T& value)函数，用于反序列化
//class对象----->json对象
void to_json(json& j,const OfflineMsg& msg)
{
    j["JsonMessage"] = msg.GetJsonMsg();
}
#endif // !OFFLINE_MSG
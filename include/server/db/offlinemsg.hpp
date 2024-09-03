#ifndef OFFLINE_MSG
#define OFFLINE_MSG

#include "../HeadFile.h"

class OfflineMsg{
    private:
        int id;
        string jsonMsg;
    public:
        int GetId()const{return id;}
        string GetJsonMsg()const{return jsonMsg;}
        
        void SetId(int _id){id = _id;}
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
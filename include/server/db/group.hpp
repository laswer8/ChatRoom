#ifndef GROUP_H
#define GROUP_H

#include"../HeadFile.h"
#include"user.hpp"


class GroupMember:public User{
    private:
        string role;
    public:
        void setrole(string _role){
            role = _role;
        }
        string getrole(){return role;}
};

class Group{
    friend void to_json(json& j,const Group& group);
    friend void from_json(const json& j,Group& group);
    private:
        int id;
        int creatorid;
        string name;
        string desc;
        vector<GroupMember> members;
    public:
        Group(int _id = -1,string _name = "",string _desc = ""):id(_id),creatorid(_id),name(_name),desc(_desc){}
        void setid(int _id){id = _id;}
        void setcreatorid(int _id){creatorid = _id;}
        void setname(string _name){name = _name;}
        void setdesc(string _desc){desc = _desc;}

        int getid()const{return id;}
        int getcreatorid()const{return creatorid;}
        string getname(){return name;}
        string getdesc(){return desc;}
        vector<GroupMember>& getmembers(){return members;}

};


void from_json(const json& j,Group& group)
{
    group.id = j["groupid"].get<int>();
    group.name = j["groupname"].get<string>();
    group.desc = j["groupdesc"].get<string>();
    json t = j["groupmembers"];
    for(auto& s : t){
        GroupMember m;
        m.setid(s["memberid"].get<int>());
        m.setname(s["membername"].get<string>());
        m.setrole(s["memberrole"].get<string>());
        m.setstate(s["memberstate"].get<string>());
        group.members.emplace_back(m);
    }
}

//定义to_json(json& j,const T& value)函数，用于反序列化
//class对象----->json对象
void to_json(json& j,const Group& group)
{
    j["groupid"]=group.id;
    j["groupname"]=group.name;
    j["groupdesc"]=group.desc;
    j["groupmembers"] = json::array();
    auto vec = group.members;
    j["membernum"] = vec.size();
    for(auto member:vec){
        json t;
        t["memberid"] = member.getid();
        t["membername"] = member.getname();
        t["memberrole"]= member.getrole();
        t["memberstate"] = member.getstate();
        j["groupmembers"].push_back(t);
    }
}
/*
{
    "MsgType":22,
    "groupid":26,
    "groupnum":1,
    "groups":
    [
        {
            "groupdesc":"aoao创建",
            "groupid":26,
            "groupmembers":
            [
                {
                    "memberid":10000001,
                    "membername":"aoao",
                    "memberrole":"creator",
                    "memberstate":"offline"
                },
                {
                    "memberid":10000002,
                    "membername":"qoqo",
                    "memberrole":"normal",
                    "memberstate":"offline"
                }
            ],
            "groupname":"QQ",
            "membernum":2
        }
    ],
    "userid":10000001
}
*/

#endif // !GROUP_H
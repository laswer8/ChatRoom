#ifndef TABLE_CHATGROUP_HPP
#define TABLE_CHATGROUP_HPP
#include "../HeadFile.h"
class ChatGroup {
private:
    uint64_t gid;
    string gname;
    uint64_t ownerid;
    uint64_t created_at;
    string data;
public:
    ChatGroup(const uint64_t& group_id = 0,const string& group_name = "",const uint64_t& uid = 0,const uint64_t& create_time = 0,const string& extra = ""):gid(group_id),gname(group_name),ownerid(uid),created_at(create_time),data(extra){}
    uint64_t getGID() const { return gid; }
    string getGName() const { return gname; }
    uint64_t getOwnerID() const { return ownerid; }
    uint64_t getCreatedAt() const { return created_at; }
    string getData() const { return data; }

    void setGID(const uint64_t& group_id) {this->gid = group_id;}
    void setGName(const string& group_name) {this->gname = group_name;}
    void setOwnerID(const uint64_t&  uid) {this->ownerid = uid;}
    void setCreatedAt(const uint64_t&  time) {this->created_at = time;}
    void setData(const string& extra) {this->data = extra;}

};

void from_json(const json& j, ChatGroup& msg) {
    msg.setGID(j["gid"].get<uint64_t>());
    msg.setGName(j["gname"].get<string>());
    msg.setOwnerID(j["ownerid"].get<uint64_t>());
    msg.setCreatedAt(j["created_at"].get<int64_t>());
    msg.setData(j["data"].get<string>());
}

void to_json(json& j,const ChatGroup& msg){
    j["gid"] = msg.getGID();
    j["gname"] = msg.getGName();
    j["ownerid"] = msg.getOwnerID();
    j["created_at"] = msg.getCreatedAt();
    j["data"] = msg.getData();

}

#endif
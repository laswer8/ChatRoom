#ifndef TABLE_FRIEND_HPP
#define TABLE_FRIEND_HPP

#include "../HeadFile.h"

class Friend {
private:
    uint64_t uid;
    uint64_t fid;
    uint64_t created_at;
    string data;
public:
    Friend(const uint64_t& user_id = 0,const uint64_t& friend_id = 0,const uint64_t& created_time = 0,const string& extra = ""):uid(user_id),fid(friend_id),created_at(created_time),data(extra){}
    uint64_t getUID() const { return uid; }
    uint64_t getFID() const { return fid; }
    uint64_t getCreatedAt() const { return created_at; }
    string getData() const { return data; }
    string getPrimaryKey(const string& hex = ":") const {
        stringstream ss;
        ss<<uid<<hex<<fid;
        return ss.str();
    }

    void setUID(const uint64_t& user_id) { this->uid = user_id; }
    void setFID(const uint64_t& friend_id) { this->fid = friend_id; }
    void setCreatedAt(const uint64_t& created_time) { this->created_at = created_time; }
    void setData(const string& extra) { this->data = extra; }
};

void from_json(const json& j, Friend& msg) {
    msg.setUID(j["uid"].get<uint64_t>());
    msg.setFID(j["fid"].get<uint64_t>());
    msg.setCreatedAt(j["CreatedAt"].get<uint64_t>());
    msg.setData(j["data"].get<string>());
}

void to_json(json& j,const Friend& msg){
    j["uid"] = msg.getUID();
    j["fid"] = msg.getUID();
    j["CreatedAt"] = msg.getCreatedAt();
    j["data"] = msg.getData();
}

#endif

#ifndef TABLE_GROUPUSER_HPP
#define TABLE_GROUPUSER_HPP

#include "../HeadFile.h"
class GroupUser {
private:
    uint64_t gid;
    uint64_t uid;
    string username;
    int role;
    uint64_t joined_at;
    uint64_t last_active;
    string data;
public:
    GroupUser(const uint64_t& group_id = 0,const uint64_t& user_id = 0,
        const string& name = "",const int& _role = 0,
        const uint64_t& join_time = 0,const uint64_t& last_time = 0,const string& extra = ""):
    gid(group_id),uid(user_id),username(name),role(_role),joined_at(join_time),last_active(last_time),data(extra) {}

    uint64_t getGID() const { return gid; }
    uint64_t getUID() const { return uid; }
    string getUsername() const { return username; }
    int getRole() const { return role; }
    uint64_t getJoinedAt() const { return joined_at; }
    uint64_t getLastActive() const { return last_active; }
    string getData() const { return data; }
    string getPrimaryKey(const string& hex = ":") const {
        stringstream ss;
        ss<<gid<<hex<<uid;
        return ss.str();
    }

    void setGID(const uint64_t& group_id) { gid = group_id; }
    void setUID(const uint64_t& user_id) { uid = user_id; }
    void setUsername(const string& name){username = name;}
    void setRole(const int& _role) { role = _role; }
    void setJoinedAt(const uint64_t& join_time) { joined_at = join_time; }
    void setLastActive(const uint64_t& last_time) { last_active = last_time; }
    void setData(const string& extra){ data = extra; }
};

void from_json(const json& j, GroupUser& msg) {
    msg.setGID(j["gid"].get<uint64_t>());
    msg.setUID(j["uid"].get<uint64_t>());
    msg.setUsername(j["username"].get<string>());
    msg.setRole(j["role"].get<int>());
    msg.setJoinedAt(j["joined_at"].get<uint64_t>());
    msg.setLastActive(j["last_active"].get<uint64_t>());
    msg.setData(j["data"].get<string>());
}

void to_json(json& j,const GroupUser& msg){
    j["gid"] = msg.getGID();
    j["uid"] = msg.getUID();
    j["username"] = msg.getUsername();
    j["role"] = msg.getRole();
    j["joined_at"] = msg.getJoinedAt();
    j["last_active"] = msg.getLastActive();
    j["data"] = msg.getData();

}

#endif

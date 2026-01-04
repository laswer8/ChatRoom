#ifndef TABLE_GREQ_HPP
#define TABLE_GREQ_HPP

#include "../HeadFile.h"
class GroupReq {
private:
    uint64_t gid;
    uint64_t uid;
    int status;
    uint64_t send_at;
    string message;
    string data;
public:
    GroupReq(const uint64_t& group_id = 0,const uint64_t& user_id = 0,
        const int& _status = 0,const uint64_t& send_time = 0,const string& msg = "",const string& extra = ""):
    gid(group_id),uid(user_id),status(_status),send_at(send_time),message(msg),data(extra) {}

    uint64_t getGID() const { return gid; }
    uint64_t getUID() const { return uid; }
    int getStatus() const { return status; }
    uint64_t getSendAt() const { return send_at; }
    string getMessage() const { return message; }
    string getData() const { return data; }
    string getPrimaryKey(const string& hex = ":") const {
        stringstream ss;
        ss<<gid<<hex<<uid;
        return ss.str();
    }

    void setGID(const uint64_t& group_id) { gid = group_id; }
    void setUID(const uint64_t& user_id) { uid = user_id; }
    void setMessage(const string& msg){message = msg;}
    void setStatus(const int& _status) { status = _status; }
    void setSendAt(const uint64_t& send_time){ send_at = send_time; }
    void setData(const string& extra){ data = extra; }
};

void from_json(const json& j, GroupReq& msg) {
    msg.setGID(j["gid"].get<uint64_t>());
    msg.setUID(j["uid"].get<uint64_t>());
    msg.setStatus(j["status"].get<int>());
    msg.setSendAt(j["send_at"].get<uint64_t>());
    msg.setMessage(j["message"].get<string>());
    msg.setData(j["data"].get<string>());
}

void to_json(json& j,const GroupReq& msg){
    j["gid"] = msg.getGID();
    j["uid"] = msg.getUID();
    j["status"] = msg.getStatus();
    j["send_at"] = msg.getSendAt();
    j["message"] = msg.getMessage();
    j["data"] = msg.getData();

}


#endif

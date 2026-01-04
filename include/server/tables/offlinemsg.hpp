#ifndef TABLE_OFFLINEMSG_HPP
#define TABLE_OFFLINEMSG_HPP

#include "../HeadFile.h"
class OfflineMsg {
private:
    uint64_t uid;
    uint64_t msg_no;
    uint64_t CreatedAt;
    string data;
public:
    OfflineMsg(const uint64_t& user_id = 0, const uint64_t& seq = 0,const uint64_t& create_time = 0,const string& extra = string()):
    uid(user_id),msg_no(seq),CreatedAt(create_time),data(extra){}
    uint64_t getUID() const { return uid; }
    uint64_t getMsgNo() const { return msg_no; }
    uint64_t getCreatedAt() const { return CreatedAt; }
    string getData() const { return data; }
    string getPrimaryKey(const string& hex = ":") const {
        stringstream ss;
        ss<<uid<<hex<<msg_no;
        return ss.str();
    }

    void setUID(const uint64_t& user_id) { this->uid = user_id; }
    void setMsgNo(const uint64_t& seq) { this->msg_no = seq; }
    void setCreatedAt(const uint64_t& created_time) { this->CreatedAt = created_time; }
    void setData(const string& extra) { this->data = extra; }

};

void from_json(const json& j, OfflineMsg& msg) {
    msg.setUID(j["uid"].get<uint64_t>());
    msg.setMsgNo(j["msg_no"].get<uint64_t>());
    msg.setCreatedAt(j["CreatedAt"].get<uint64_t>());
    msg.setData(j["data"].get<string>());
}

void to_json(json& j,const OfflineMsg& msg){
    j["uid"] = msg.getUID();
    j["msg_no"] = msg.getMsgNo();
    j["CreatedAt"] = msg.getCreatedAt();
    j["data"] = msg.getData();

}

#endif

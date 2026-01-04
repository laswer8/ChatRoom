#ifndef TABLE_FREQ_HPP
#define TABLE_FREQ_HPP

#include "../HeadFile.h"

class Freq {
private:
    uint64_t req_id;
    uint64_t fid;
    uint64_t tid;
    int64_t status;
    uint64_t send_at;
    string message;
    string data;
public:
    Freq(const uint64_t& id = 0,const uint64_t& from_id = 0,const uint64_t& to_id = 0, const int64_t& req_status = 0,const uint64_t& send_time = 0,const string& msg = "", const string& extra = ""):req_id(id),fid(from_id),tid(to_id),status(req_status),send_at(send_time),message(msg),data(extra){}
    uint64_t getReqID() const {return this->req_id;}
    uint64_t getFromID() const {return this->fid;}
    uint64_t getToID() const {return this->tid;}
    int64_t getStatus() const {return this->status;}
    uint64_t getSendAt() const {return this->send_at;}
    string getMessage() const {return this->message;}
    string getData() const {return this->data;}

    void setReqID(const uint64_t& id) { this->req_id = id; }
    void setFromID(const uint64_t& f) { this->fid = f; }
    void setToID(const uint64_t& t) { this->tid = t; }
    void setStatus(const int64_t& s) { this->status = s; }
    void setSendAt(const uint64_t& t) { this->send_at = t; }
    void setMessage(const string& msg) { this->message = msg; }
    void setData(const string& extra) { this->data = extra; }
};

void from_json(const json& j, Freq& msg) {
    msg.setReqID(j["req_id"].get<uint64_t>());
    msg.setFromID(j["fid"].get<uint64_t>());
    msg.setToID(j["tid"].get<uint64_t>());
    msg.setStatus(j["status"].get<int64_t>());
    msg.setSendAt(j["send_at"].get<uint64_t>());
    msg.setMessage(j["message"].get<string>());
    msg.setData(j["data"].get<string>());
}

void to_json(json& j,const Freq& msg){
    j["req_id"] = msg.getReqID();
    j["fid"] = msg.getFromID();
    j["tid"] = msg.getToID();
    j["status"] = msg.getStatus();
    j["send_at"] = msg.getSendAt();
    j["message"] = msg.getMessage();
    j["data"] = msg.getData();
}

#endif
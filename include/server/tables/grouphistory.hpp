#ifndef TABLE_GROUPHISTORY_HPP
#define TABLE_GROUPHISTORY_HPP

#include "../HeadFile.h"
class GroupHistory {
private:
    const uint64_t gid;   //群组ID
    const uint64_t max_size;  //历史记录最大大小，默认512KB
    uint64_t current_size;  //当前大小
    uint64_t last_update;   //上次更新时间
    string history;     //历史记录，json字符串
public:
    GroupHistory(const uint64_t& id = 0, const uint64_t& size = 512*1024):gid(id),max_size(size),current_size(0),last_update(0),history("[]"){}
    uint64_t getGID(){return this->gid;}
    uint64_t getMaxSize(){return this->max_size;}
    uint64_t getCurrentSize(){return this->current_size;}
    uint64_t getLastUpdate(){return this->last_update;}
    string getHistory(){return history;}
    json getJsonHistory() {
        if (history.empty()) {
            return json::array();
        }
        try {
            return json::parse(history);
        } catch (const exception& e) {
            LOG_ERROR << "GroupHistory::getJsonHistory(): parse history to json error: " << e.what();
            return json::array();
        }
    }

    void UpdateCurrentSize() {
        this->current_size = history.size();
    }
    void UpdateLastUpdate() {
        this->last_update = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    bool setHistory(string&& new_history) {
        if (!new_history.empty() && new_history != "[]") {
            try {
                json test = json::parse(new_history);
                if (!test.is_array()) {
                    LOG_ERROR << "GroupHistory::setHistory(): history is not a JSON array";
                    return false;
                }
            } catch (const exception& e) {
                LOG_ERROR << "GroupHistory::setHistory(): invalid JSON: " << e.what();
                return false;
            }
        }
        if (new_history.size() > max_size) {
            LOG_ERROR << "GroupHistory::setHistory(): new history size exceeds max_size";
            return false;
        }
        this->history = std::move(new_history);
        UpdateCurrentSize();
        UpdateLastUpdate();
        return true;
    }

    bool setJsonHistory(json&& json_data) {
        if (!json_data.is_array())return false;
        try {
            string new_history = json_data.dump();
            return setHistory(std::move(new_history));
        }catch (exception& e) {
            LOG_INFO<<"GroupHistory::setHistory(): parse json error: "<<e.what();
            return false;
        }
    }

    bool addRecord(string&& record) {
        json j = getJsonHistory();
        uint64_t size = record.size() + current_size + (j.empty()? 0 : 1);
        //删除头元素，直到能够容纳大小
        while (size > max_size && !j.empty()) {
            string s = j.at(0);
            j.erase(j.begin());
            size -= (s.size() + 2 + (j.size()>1?1:0));
        }
        j.emplace_back(::move(record));
        record = "";
        return setJsonHistory(::move(j));
    }
};

#endif
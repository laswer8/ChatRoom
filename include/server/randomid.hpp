#ifndef  __ID_GENERATE_H__
#define  __ID_GENERATE_H__
 
#include"HeadFile.h"

//数据中心ID
const uint16_t datacenterId = 1;
//机器中心ID
const uint16_t workerId = 2;

class Snowflake {
public:
    static shared_ptr<Snowflake> GetInstance(){
        if(generate == nullptr){
            lock_guard<mutex> guard(m);
            if(generate == nullptr){
                generate = shared_ptr<Snowflake>(new Snowflake());
            }
        }
        return generate;
    }

    uint64_t generateUniqueId() {
        uint64_t timestamp = currentTimestamp();

        if (timestamp < lastTimestamp_) {
            throw std::runtime_error("Clock moved backwards");
        }

		std::unique_lock<std::mutex> lock(mutex_);
		//时间戳原理:获取当前时间戳如果等于上次时间戳(同一毫秒内),咋序列号加一。否则序列号设置为0，从0开始
        if (timestamp == lastTimestamp_) {
            sequence_ = (sequence_ + 1) & sequenceMask_;
            if (sequence_ == 0) {
                timestamp = waitNextMillis(lastTimestamp_);
            }
        } else {
            sequence_ = 0;
        }

        lastTimestamp_ = timestamp;

        return ((timestamp - twepoch_) << timestampLeftShift_) |
               ((datacenterId_ & maxDatacenterId_) << datacenterIdShift_) |
               ((workerId_ & maxWorkerId_) << workerIdShift_) |
               (sequence_ & sequenceMask_);
    }

private:
    Snowflake()
        : datacenterId_(datacenterId), workerId_(workerId), sequence_(0), lastTimestamp_(0) {
        if (datacenterId_ > maxDatacenterId_ || workerId_ > maxWorkerId_) {
            throw std::invalid_argument("Invalid datacenter or worker ID");
        }
    }

    uint64_t currentTimestamp() const {
        return std::time(nullptr) * 1000;
    }

    uint64_t waitNextMillis(uint64_t lastTimestamp) {
        uint64_t timestamp = currentTimestamp();
        while (timestamp <= lastTimestamp) {
            timestamp = currentTimestamp();
        }
        return timestamp;
    }

    const uint64_t twepoch_ = 1723824000000ULL;  // 2024-08-17 00:00:00 UTC
    const uint64_t unixEpoch_ = 0ULL;  // Unix时间戳起点：1970-01-01 00:00:00 UTC
    const uint64_t workerIdBits_ = 5;
    const uint64_t datacenterIdBits_ = 5;
    const uint64_t maxWorkerId_ = (1ULL << workerIdBits_) - 1;
    const uint64_t maxDatacenterId_ = (1ULL << datacenterIdBits_) - 1;
    const uint64_t sequenceBits_ = 12;
    const uint64_t workerIdShift_ = sequenceBits_;
    const uint64_t datacenterIdShift_ = sequenceBits_ + workerIdBits_;
    const uint64_t timestampLeftShift_ = sequenceBits_ + workerIdBits_ + datacenterIdBits_;
    const uint64_t sequenceMask_ = (1ULL << sequenceBits_) - 1;

    uint16_t datacenterId_;
    uint16_t workerId_;
    uint64_t sequence_;
    uint64_t lastTimestamp_;
    std::mutex mutex_;

    static mutex m;
    static shared_ptr<Snowflake> generate;
};
mutex Snowflake::m;
shared_ptr<Snowflake> Snowflake::generate = nullptr;


#endif
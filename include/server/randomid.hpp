#ifndef  __ID_GENERATE_H__
#define  __ID_GENERATE_H__
 
#include"HeadFile.h"

//数据中心ID
const uint16_t datacenterId = 1;
//机器中心ID
const uint16_t workerId = 2;
//时钟回拨最大容忍时间
const uint64_t maxClockBackward = 10;

//CAS雪花算法
class Snowflake {
public:
    static shared_ptr<Snowflake> GetInstance(){
        static shared_ptr<Snowflake> generate = shared_ptr<Snowflake>(new Snowflake());
        return generate;
    }

    //无锁CAS生成ID
    uint64_t generateUniqueId() {
        uint64_t currentTimestamp_ = currentTimestamp();
        uint64_t lastTimestamp = lastTimestamp_.load(std::memory_order_acquire);

        // 识别时钟回拨问题
        if (currentTimestamp_ < lastTimestamp) {
            int64_t timeDiff = static_cast<int64_t>(lastTimestamp) - static_cast<int64_t>(currentTimestamp_);

            // 如果回拨在容忍范围内，等待时钟追上
            if (timeDiff <= maxClockBackwardTolerance_) {
                LOG_WARN<< "Clock moved backwards by " << timeDiff
                         << "ms, waiting for clock to catch up...";
                handleClockBackward(timeDiff);
                currentTimestamp_ = currentTimestamp(); // 重新获取时间戳
            } else {
                // 回拨超过容忍范围，抛出异常
                throw std::runtime_error("Clock moved backwards by " +
                                       std::to_string(timeDiff) +
                                       "ms, exceeds maximum tolerance of " +
                                       std::to_string(maxClockBackwardTolerance_) + "ms");
            }
            //throw std::runtime_error("Clock moved backwards");
        }

        uint64_t sequence = 0;

        if (currentTimestamp_ == lastTimestamp) {
            // 同一毫秒内，使用CAS递增序列号
            sequence = sequence_.fetch_add(1, std::memory_order_acq_rel);

            // 检查序列号是否溢出
            if ((sequence & sequenceMask_) == sequenceMask_) {
                // 序列号用尽，等待下一毫秒
                currentTimestamp_ = waitNextMillis(lastTimestamp,10);
                sequence = 0;
                lastTimestamp_.store(currentTimestamp_, std::memory_order_release);
                sequence_.store(0, std::memory_order_release);
            }
        } else {
            // 新的时间戳，重置序列号
            sequence = 0;

            // 使用CAS更新最后时间戳
            uint64_t expected = lastTimestamp;
            if (lastTimestamp_.compare_exchange_strong(expected, currentTimestamp_,
                                                      std::memory_order_release,
                                                      std::memory_order_acquire)) {
                // CAS成功，重置序列号
                sequence_.store(0, std::memory_order_release);
            } else {
                // CAS失败，说明在此期间有其他线程更新了时间戳
                if (expected == currentTimestamp_) {
                    // 已经有其他线程更新到相同时间戳，需要递增序列号
                    sequence = sequence_.fetch_add(1, std::memory_order_acq_rel);
                    if ((sequence & sequenceMask_) == sequenceMask_) {
                        currentTimestamp_ = waitNextMillis(currentTimestamp_);
                        sequence = 0;
                        lastTimestamp_.store(currentTimestamp_, std::memory_order_release);
                        sequence_.store(0, std::memory_order_release);
                    }
                } else if (expected > currentTimestamp_) {
                    // 时钟回拨频率太高，可能一直回退，报错
                    throw std::runtime_error("Clock moved backwards during CAS operation");
                } else {
                    // 其他线程已经更新到更新的时间戳，重新尝试生成ID
                    return generateUniqueId();
                }
            }
        }

        return ((currentTimestamp_ - twepoch_) << timestampLeftShift_) |
               ((datacenterId_ & maxDatacenterId_) << datacenterIdShift_) |
               ((workerId_ & maxWorkerId_) << workerIdShift_) |
               (sequence & sequenceMask_);

    }
    // 获取内部状态（用于测试和监控）
    struct State {
        uint64_t sequence;
        uint64_t lastTimestamp;
        uint64_t maxClockBackwardTolerance;
    };

    State getState() const {
        return {
            sequence_.load(std::memory_order_acquire),
            lastTimestamp_.load(std::memory_order_acquire),
            maxClockBackwardTolerance_
        };
    }
    // 设置时钟回拨容忍时间（毫秒）
    void setMaxClockBackwardTolerance(const uint64_t& toleranceMs) {
        maxClockBackwardTolerance_ = toleranceMs;
    }

    // 获取当前容忍时间
    uint64_t getMaxClockBackwardTolerance() const {
        return maxClockBackwardTolerance_;
    }

private:
    Snowflake(const uint16_t& datacenter_id = datacenterId,const uint16_t& worker_id = workerId,const uint64_t& maxClockBackwardTolerance = maxClockBackward)
        : datacenterId_(datacenter_id),
          workerId_(worker_id),
          sequence_(0),
          maxClockBackwardTolerance_(maxClockBackwardTolerance){
        if (datacenterId_ > maxDatacenterId_ || workerId_ > maxWorkerId_) {
            throw std::invalid_argument("Invalid datacenter or worker ID");
        }
        // 初始化最后时间戳
        lastTimestamp_.store(currentTimestamp(), std::memory_order_release);
    }

    Snowflake(const Snowflake&) = delete;
    Snowflake& operator=(const Snowflake&) = delete;

    uint64_t currentTimestamp() const {
        auto now = std::chrono::system_clock::now();
        auto duration = now.time_since_epoch();
        return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
    }

    uint64_t waitNextMillis(uint64_t lastTimestamp,int maxRetry = 10) {
        uint64_t timestamp = currentTimestamp();
        int retryCount = 0;

        while (timestamp <= lastTimestamp) {
            if (++retryCount > maxRetry) {
                throw std::runtime_error("Wait for next millisecond timeout");
            }

            // 指数退避策略
            if (retryCount < 5) {
                std::this_thread::yield();
            } else {
                std::this_thread::sleep_for(std::chrono::microseconds(100 * (1 << (retryCount - 5))));
            }
            timestamp = currentTimestamp();
        }
        return timestamp;
    }

    // 处理时钟回拨 - 返回是否成功
    bool handleClockBackward(int64_t timeDiff) {
        LOG_WARN << "Handling clock backward: " << timeDiff << "ms";

        // 等待时钟追回差异 + 额外1ms缓冲
        uint64_t waitTime = static_cast<uint64_t>(timeDiff) + 1;
        const uint64_t maxWaitTime = maxClockBackwardTolerance_ * 2;  // 最大等待时间

        auto start = std::chrono::steady_clock::now();
        uint64_t totalWaited = 0;

        while (totalWaited < waitTime) {
            // 检查是否超过最大等待时间
            if (totalWaited > maxWaitTime) {
                LOG_WARN<< "Clock backward handling timeout after " << totalWaited << "ms";
                return false;
            }

            // 检查当前时间是否已经追回
            uint64_t nowTimestamp = currentTimestamp();
            uint64_t currentLastTimestamp = lastTimestamp_.load(std::memory_order_acquire);

            if (nowTimestamp >= currentLastTimestamp) {
                LOG_WARN << "Clock caught up after " << totalWaited << "ms";
                return true;
            }

            // 动态睡眠
            uint64_t remaining = waitTime - totalWaited;
            if (remaining > 10) {
                std::this_thread::sleep_for(std::chrono::milliseconds(std::min(remaining / 2, 10UL)));
            } else {
                std::this_thread::yield();
            }

            // 更新等待时间
            auto current = std::chrono::steady_clock::now();
            totalWaited = std::chrono::duration_cast<std::chrono::milliseconds>(current - start).count();
        }

        LOG_WARN<< "Clock backward handling completed in " << totalWaited << "ms";
        return true;
    }

    uint16_t datacenterId_;
    uint16_t workerId_;
    uint64_t maxClockBackwardTolerance_;
    const uint64_t twepoch_ = 1723824000000ULL;  // 2024-08-17 00:00:00 UTC
    const uint64_t workerIdBits_ = 5;
    const uint64_t datacenterIdBits_ = 5;
    const uint64_t maxWorkerId_ = (1ULL << workerIdBits_) - 1;
    const uint64_t maxDatacenterId_ = (1ULL << datacenterIdBits_) - 1;
    const uint64_t sequenceBits_ = 12;
    const uint64_t workerIdShift_ = sequenceBits_;
    const uint64_t datacenterIdShift_ = sequenceBits_ + workerIdBits_;
    const uint64_t timestampLeftShift_ = sequenceBits_ + workerIdBits_ + datacenterIdBits_;
    const uint64_t sequenceMask_ = (1ULL << sequenceBits_) - 1;
    std::atomic<uint64_t> sequence_;
    std::atomic<uint64_t> lastTimestamp_;
};


#endif
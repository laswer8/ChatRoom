#ifndef SERVER_THREADPOOL_HPP_
#define SERVER_THREADPOOL_HPP_

#include "HeadFile.h"

///线程池拒绝策略，当任务队列已满，且达到最大线程数时触发拒绝策略
///
///@param AbortPolicy 异常策略，默认，直接抛出异常拒绝任务的执行
///@param DiscardPolicy 丢弃策略，将拒绝的任务直接丢弃，不提示调用者
///@param CallerRunsPolicy 调用者调用策略，将拒绝的任务交给调用者线程执行
enum ThreadRejectedPolicy {
    AbortPolicy = 0,
    DiscardPolicy,
    CallerRunsPolicy
};

class ThreadWorker {
public:
    std::atomic<bool> stop{false};
    const uint32_t pool_index;
    std::atomic<uint64_t> last_active_time_{0};
    std::thread worker_;
    ThreadWorker(const uint32_t& _index, const uint64_t& time):pool_index(_index),last_active_time_(time){}
    ~ThreadWorker() {
        if (worker_.joinable()) {
            worker_.join();
        }
    }
    // 禁止拷贝
    ThreadWorker(const ThreadWorker&) = delete;
    ThreadWorker& operator=(const ThreadWorker&) = delete;

    void shutdown() {
        stop.store(true, std::memory_order_release);
    }
};

class ThreadPool {
private:
    using TaskType = shared_ptr<function<void()>>;
    ThreadRejectedPolicy handler;   //拒绝策略
    atomic<bool> running{false};  //运行标志位
    uint32_t maxPoolSize;           //池最大线程数
    uint32_t corePoolSize;          //池核心线程数，总数量不能小于
    uint64_t keepAliveTime_ms;      //空闲线程存活时间
    uint64_t Timeout_ms;            //检查超时时间
    atomic<uint32_t> active_size{0};    //活跃线程数
    uint32_t TaskMaxNum;            //最大任务数
    atomic<uint32_t> current_tasks{0}; //当前任务数
    deque<TaskType> WorkQueue;      //任务队列
    deque<uint32_t> unused_index_deque; //空闲下标序列，标记可用位
    vector<shared_ptr<ThreadWorker>> pool;//线程池，固定槽数，控制槽来实现动态大小
    thread check_thread;            //后台检查现场，检查超时时间
    shared_mutex list_mutex;        //线程池共享锁
    shared_mutex mu;                //任务队列共享锁
    condition_variable_any not_empty_cv_;//非空条件变量
    condition_variable_any not_full_cv_;//非满条件变量

    //线程执行函数
    //过期自动销毁
    void thread_work(const shared_ptr<ThreadWorker>& thread_obj) {
        // thread_obj->status_.store(WAITING,memory_order_release);
        thread_obj->last_active_time_.store(getCurrentTimestamp(),memory_order_release);
        while (this->running.load(memory_order_acquire)) {
            try {
                if (!thread_obj or thread_obj->stop.load(memory_order_acquire)) {
                    break;
                }
                auto task = this->take();
                if (task) {
                    thread_obj->last_active_time_.store(getCurrentTimestamp(),memory_order_release);
                    (*task)();
                }
            }catch (...) {
                continue;
            }
            this_thread::yield();
        }

        uint32_t index = thread_obj->pool_index;
        unique_lock<shared_mutex> lock(list_mutex);
        if (pool[index]) {
            //表示当前线程已结束
            unused_index_deque.push_back(index);
        }
    }

    static uint64_t getCurrentTimestamp() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    TaskType take() {
        unique_lock<shared_mutex> lock(mu);
        not_empty_cv_.wait_for(lock,std::chrono::milliseconds(Timeout_ms),[&]() {
            //取消运行或者队列不为空
            return !running.load(memory_order_relaxed) || !this->WorkQueue.empty();
        });
        if (!running.load(memory_order_relaxed) || this->WorkQueue.empty())
            return nullptr;
        auto task = ::move(this->WorkQueue.front());
        this->WorkQueue.pop_front();
        this->current_tasks.fetch_sub(1,memory_order_relaxed);
        not_full_cv_.notify_one();
        return task;
    }

    bool append(TaskType&& task) {
        unique_lock<shared_mutex> lock(mu);
        not_full_cv_.wait_for(lock,std::chrono::milliseconds(Timeout_ms),[&]() {
            //取消运行或者队列不为空
            return !running.load(memory_order_relaxed) || current_tasks.load(memory_order_acquire) < this->TaskMaxNum;
        });
        if (!running.load(memory_order_relaxed) || current_tasks.load(memory_order_acquire) >= this->TaskMaxNum)
            return false;
        this->WorkQueue.push_back(::move(task));
        this->current_tasks.fetch_add(1,memory_order_relaxed);
        not_empty_cv_.notify_one();
        return true;
    }

    bool createWorker(const TaskType& task = nullptr) {
        if (active_size.load(std::memory_order_acquire) >= maxPoolSize || unused_index_deque.empty()) {
            return false;
        }
        uint32_t index = maxPoolSize;
        {
            unique_lock<shared_mutex> lock(list_mutex);
            if (unused_index_deque.empty())return false;
            index = unused_index_deque.front();
            unused_index_deque.pop_front();
        }
        if (index >= maxPoolSize)
            return false;
        auto worker = make_shared<ThreadWorker>(index,getCurrentTimestamp());
        auto worker_ptr = worker.get();
        try {
            // 将worker添加到线程池
            // 即使该位置有未释放的线程对象，其也会在主线程析构
            pool[index] = worker;
            active_size.fetch_add(1, std::memory_order_acq_rel);
            // 启动线程
            worker_ptr->worker_ = thread([&worker,&task,this]() {
                if (task) {
                    try {
                        (*task)();
                    }catch (exception& e) {
                        ostringstream oss;
                        oss << this_thread::get_id();
                        LOG_INFO<<"PID "<<oss.str()<<" -- 创建线程时存在立即执行任务，但执行时错误："<<e.what();
                    }
                }
                this->thread_work(worker);
            });
            return true;
        } catch (...) {
            // 创建失败
            worker->shutdown();
            return false;
        }
    }

    void check_work() {
        const auto interval = chrono::milliseconds(this->Timeout_ms);
        this_thread::sleep_for(interval);
        while (this->running.load(memory_order_acquire)) {
            auto current_time = getCurrentTimestamp();
            for (const auto& it : pool) {
                if (active_size.load(memory_order_acquire) <= corePoolSize) {
                    break;
                }
                if (!it || it->stop.load(memory_order_acquire)) {
                    continue;
                }else if (current_time - it->last_active_time_ > keepAliveTime_ms) {
                    it->shutdown();
                    active_size.fetch_sub(1, memory_order_release);
                }
            }
            this_thread::sleep_for(interval);
        }
    }

public:
    ThreadPool(const uint32_t& max_size,const uint32_t& min_size,const uint64_t& keepalive_ms,const uint64_t& timeout_ms,const uint32_t& task_max,const ThreadRejectedPolicy policy = AbortPolicy):
    handler(policy),maxPoolSize(max_size),keepAliveTime_ms(keepalive_ms),Timeout_ms(timeout_ms),corePoolSize(min_size),TaskMaxNum(task_max){
        this->pool.resize(max_size);
        for (auto i = 0; i < max_size; ++i) {
            pool[i] = nullptr;
            unused_index_deque.push_back(i);
        }
        this->running.store(true,memory_order_release);
        this->check_thread = thread([&]() {
            this->check_work();
        });
    }

    ~ThreadPool() {
        shutdown();
        for (const auto& i : pool) {
            if (i and i->worker_.joinable())
                i->worker_.join();
        }
        this->WorkQueue.clear();
    }

    void shutdown() {
        this->running.store(false,memory_order_release);
        not_empty_cv_.notify_all();
        not_full_cv_.notify_all();
    }

    void shutdown_strong() {
        this->running.store(false,memory_order_release);
        {
            unique_lock<shared_mutex> l(mu);
            this->WorkQueue.clear();
        }
        not_empty_cv_.notify_all();
        not_full_cv_.notify_all();
    }

    bool submit(TaskType task) {
        if ( !this->running.load(memory_order_acquire) || !task)return false;
        if (this->active_size.load(memory_order_acquire) < this->corePoolSize or (this->current_tasks.load(memory_order_acquire) >= this->TaskMaxNum and this->active_size.load(memory_order_acquire) < this->maxPoolSize)) {
            //线程数小于核心线程数，创建新线程
            //任务队列满，线程数大于核心线程数且未达到上限，创建新线程
            if (createWorker(task))return true;
            //触发拒绝策略
            switch (this->handler) {
                case ThreadRejectedPolicy::CallerRunsPolicy:
                    //用户调用线程执行
                    if (task)
                        try {
                            (*task)();
                        }catch (exception& e) {
                            LOG_INFO<<"添加任务失败，于当前线程执行，但出现错误："<<e.what();
                        }
                    return true;
                case ThreadRejectedPolicy::DiscardPolicy:
                    //直接丢弃
                    return true;
                default:
                    //默认ThreadRejectedPolicy::AbortPolicy策略
                    throw runtime_error("加入任务失败，拒绝执行");
            }
        }else if (this->current_tasks.load(memory_order_acquire) < this->TaskMaxNum) {
            //任务队列未满，且当前线程大于核心线程
            return this->append(std::move(task));
        }else {
            //任务队列满，线程池达到上限，触发拒绝策略
            switch (this->handler) {
                case ThreadRejectedPolicy::CallerRunsPolicy:
                    //用户调用线程执行
                    if (task)
                        try {
                            (*task)();
                        }catch (exception& e) {
                            LOG_INFO<<"线程池已满，直接于当前线程执行，但出现错误："<<e.what();
                        }
                    return true;
                case ThreadRejectedPolicy::DiscardPolicy:
                    //直接丢弃
                    return true;
                default:
                    //默认ThreadRejectedPolicy::AbortPolicy策略
                    throw runtime_error("线程池已满，拒绝执行");
            }
        }
    }

};
const size_t CPU_CORES = std::max(static_cast<int>(std::thread::hardware_concurrency()), 4);

class ThreadPoolBuilder:public enable_shared_from_this<ThreadPoolBuilder>{
private:
    ThreadRejectedPolicy handler;   //拒绝策略
    uint32_t maxPoolSize;           //池最大线程数
    uint32_t corePoolSize;          //池核心线程数，总数量不能小于
    uint32_t TaskMaxNum;            //最大任务数
    uint64_t keepAliveTime_ms;      //空闲线程存活时间
    uint64_t Timeout_ms;            //检查超时时间
    explicit ThreadPoolBuilder(const uint32_t& maxsize = CPU_CORES*2, const uint32_t& minsize = CPU_CORES,const uint32_t& max_task_num = 1024,
        const uint64_t& keepalive_ms = 60000,const uint64_t& interval_ms = 1000,const ThreadRejectedPolicy& policy = AbortPolicy):
        handler(policy),maxPoolSize(maxsize),corePoolSize(minsize),TaskMaxNum(max_task_num),keepAliveTime_ms(keepalive_ms),Timeout_ms(interval_ms) {}
public:
    ThreadPoolBuilder(const ThreadPoolBuilder&) = delete;
    ThreadPoolBuilder& operator=(const ThreadPoolBuilder&) = delete;
    ThreadPoolBuilder(ThreadPoolBuilder&&) = delete;
    ThreadPoolBuilder& operator=(ThreadPoolBuilder&&) = delete;

    ~ThreadPoolBuilder() = default;

    static shared_ptr<ThreadPoolBuilder> getInstance() {
        static shared_ptr<ThreadPoolBuilder> instance = shared_ptr<ThreadPoolBuilder>(new ThreadPoolBuilder());
        return instance;
    }

    shared_ptr<ThreadPoolBuilder> setThreadRejectedPolicy(const ThreadRejectedPolicy& policy) {
        this->handler = policy;
        return shared_from_this();
    }

    shared_ptr<ThreadPoolBuilder> setMaxPoolSize(const uint32_t& size) {
        this->maxPoolSize = size;
        return shared_from_this();
    }
    shared_ptr<ThreadPoolBuilder> setCorePoolSize(const uint32_t& size) {
        this->corePoolSize = size;
        return shared_from_this();
    }
    shared_ptr<ThreadPoolBuilder> setTaskMaxNum(const uint32_t& count) {
        this->TaskMaxNum = count;
        return shared_from_this();
    }
    shared_ptr<ThreadPoolBuilder> setKeepAliveTime_ms(const uint64_t& time_ms) {
        this->keepAliveTime_ms = time_ms;
        return shared_from_this();
    }
    shared_ptr<ThreadPoolBuilder> setInterval_ms(const uint64_t& interval_ms) {
        this->Timeout_ms = interval_ms;
        return shared_from_this();
    }

    shared_ptr<ThreadPool> build() {
        static shared_ptr<ThreadPool> instance = make_shared<ThreadPool>(maxPoolSize,corePoolSize,keepAliveTime_ms,Timeout_ms,TaskMaxNum,handler);
        return instance;
    }

};

#endif  // SERVER_THREADPOOL_HPP_
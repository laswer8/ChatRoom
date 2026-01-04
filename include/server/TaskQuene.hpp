#ifndef TASKQUENE_HPP_
#define TASKQUENE_HPP_
#include "HeadFile.h"
#include "threadpool.hpp"
//时间轮转动频率
#define MILLISECONDS 10
#define SECONDS 1000
#define MINUTE 60000
#define HOURS 3600000
#define DAILY 86400000
#define MONTH 2592000000


//五层时间轮算法
class TimeWheelData;

enum TimeWheelType{
    DAILY_WHEEL = 0,
    HOURS_WHEEL,
    MINUTE_WHEEL,
    SECONDS_WHEEL,
    MILLISECONDS_WHEEL
};

enum TaskStatus {
    WAITING = 0,
    ACTIVING,
    DESTROYED,
    INIT
};

///定时任务属性
///@param expireTime 任务定时时间
///@param cb    定时任务，应提前使用bind()绑定参数
///@param status 当前任务状态（空闲、执行中、不可用）
class TimeWheelData{
public:
    uint64_t expireTime;   //设定过期时间
    function<void()> cb; //定时任务的回调函数
    atomic<uint64_t> currentIndex{0};   //任务所在的槽
    atomic<TaskStatus> status{INIT};
    TimeWheelData():expireTime(0){}
    TimeWheelData(const function<void()>& cb_,const uint64_t& expir,const TaskStatus& statu = INIT):
            expireTime(expir),cb(cb_),status(statu){}
    TimeWheelData(const TimeWheelData& right) {
        this->cb = right.cb;
        this->expireTime = right.expireTime;
        this->status.store(right.status.load(memory_order_acquire),memory_order_release);
    }
    TimeWheelData& operator=(const TimeWheelData& right) {
        this->cb = right.cb;
        this->expireTime = right.expireTime;
        this->status.store(right.status.load(memory_order_acquire),memory_order_release);
        return *this;
    }
    ~TimeWheelData() = default;
};

//定义以一个时间轮类型和定时器链表类型，时间轮是一个数组，数组里面是一个个链表
typedef shared_ptr<vector<shared_ptr<TimeWheelData>>> slot_content_t; //时间轮槽
typedef vector<slot_content_t> slot_t;  //时间轮

//时间轮属性
class TimeWheelCommonality {
protected:
    slot_t slots_;							//时间轮
    vector<atomic<uint64_t>> slot_count_; //每个槽对应的任务数量
    atomic<uint64_t> currentSlotIndex_{0};//当前时间轮指针
    const uint32_t slotNumber_;				//时间轮槽数量
    const uint32_t check_time_ms;           //刷新间隔
    const uint32_t maxTaskNumber_;          //槽最大任务数量
    atomic<uint32_t> currentTaskNumber_{0};      //当前任务数
    TimeWheelType wheelType_;				//时间轮类型（ms、sec、min、hour、day）
    thread cleanThread_;			        //清除非活动定时任务线程
    atomic<bool> running{false};		//停止线程标志位
    shared_mutex mu;

public:
	//构造方法
    TimeWheelCommonality(const uint32_t& slotNumber, const TimeWheelType wheelType, const uint32_t& checktime = 10,const uint32_t& task_num = 32) :
    slotNumber_(slotNumber),check_time_ms(checktime),maxTaskNumber_(task_num), wheelType_(wheelType) {
        slots_.resize(slotNumber);
        for (uint32_t i = 0; i < slotNumber; i++) {
            slots_[i] = make_shared<vector<shared_ptr<TimeWheelData>>>(task_num,nullptr);
            slot_count_[i].store(0,memory_order_release);
        }
        this->running.store(true, memory_order_release);
        CleanUpTheNoneActiveSlotsThread();
    }

	//虚析构方法，用于删除线程
    virtual ~TimeWheelCommonality(){
        this->running.store(false,memory_order_release);
        if (this->cleanThread_.joinable())
            this->cleanThread_.join();
    }

    bool remarkCancelTask(shared_ptr<TimeWheelData>& task) {
        TaskStatus except = WAITING;
        if (task->status.compare_exchange_strong(except,DESTROYED,memory_order_acquire)) {
            return true;
        }
        return false;
    }

	//获取时间轮
    slot_t& GetSlots() { return slots_; }
	//获取时间轮指针
    uint64_t GetCurrentSlotIndex() { return currentSlotIndex_.load(memory_order_acquire); }

    uint32_t getSlotTaskNum(const uint64_t index) {
        return this->slot_count_[index].load(memory_order_acquire);
    }

	//清除非活动定时器线程的方法
    void CleanUpTheNoneActiveSlotsThread() {
        this->cleanThread_ = thread(
            [&]() -> void{
                auto interval = chrono::milliseconds(this->check_time_ms);
                this_thread::sleep_for(interval);
                while(this->running.load(memory_order_acquire)){
                    DeleteNotActiveTimers();
                    this_thread::sleep_for(chrono::milliseconds(check_time_ms));
                }
            }
        );
    }

	//判断是否为空
    bool IsEmpty() {
        return !this->currentTaskNumber_.load(memory_order_acq_rel);
    }

    shared_ptr<vector<shared_ptr<TimeWheelData>>> getCurrentSlot(const uint64_t index) {
        return this->slots_[index];
    }

    uint32_t getSlotMaxTasks() {
        return this->maxTaskNumber_;
    }

	//旋转时间轮
    bool RotateTimeWheel() {
        currentSlotIndex_.store((currentSlotIndex_.load(memory_order_acquire)+1)%slotNumber_,memory_order_release);
        return currentSlotIndex_.load(memory_order_acquire) == 0; //是否转完一圈
    }

	//是否已经转过一周
    bool WhetherTimeWheelAdvance() {
        return currentSlotIndex_.load(memory_order_acquire) == 0;
    }

	//触发器，触发单次定时任务
    void TriggerTask() {
        /*获取当前槽*/
        auto index = currentSlotIndex_.load(memory_order_acquire);
        auto& currentSlot = slots_[index];
        if (!currentSlot || !slot_count_[index].load(memory_order_acquire)) {
            return;
        }
        //遍历链表，触发定时任务
        TaskStatus except = WAITING;
        for (auto i = 0; i < maxTaskNumber_; i++){
            //触发定时器
            auto task = currentSlot->at(i);
            if (!task) {continue;}
            except = WAITING;
            if (task->status.compare_exchange_strong(except,ACTIVING,memory_order_acq_rel)) {
                try {
                    ThreadPoolBuilder::getInstance()->build()->submit(make_shared<function<void()>>(task->cb));
                    //timer->cb();
                }catch(exception& e) {
                    LOG_ERROR << "TimeWheelCommonality::TriggerTask(): execute callback error: " << e.what();
                }
                //无论成功失败，不允许重试，直接销毁
                task->status.store(DESTROYED,memory_order_release);
            }
        }
    }

	//删掉非活动任务
    void DeleteNotActiveTimers() {
        {
            //获取被取消的任务
            //遍历每个槽
            for (auto i = 0; i < this->slotNumber_ and this->currentTaskNumber_.load(memory_order_acquire); ++i) {
                auto& data = this->slots_.at(i);
                if (!data || !slot_count_[i].load(memory_order_acquire))
                    continue;
                for (auto index = 0; index < maxTaskNumber_ and slot_count_[i].load(memory_order_acquire); ++index) {
                    auto timedata = data->at(index);
                    if (timedata and timedata->status.load(memory_order_acquire) == DESTROYED) {
                        (*data)[index] = nullptr;
                        this->currentTaskNumber_.fetch_sub(1,memory_order_acq_rel);
                        slot_count_[i].fetch_sub(1,memory_order_acq_rel);
                    }
                }
            }
        }
    }

	//虚函数：将任务加入时间轮
    virtual bool AddTimer(shared_ptr<TimeWheelData>&& timer) = 0;
};

//日轮
class TimeWheelDaily : public TimeWheelCommonality{
public:
    explicit  TimeWheelDaily(const uint32_t& checktime = 10,const uint32_t& task_num = 32) : TimeWheelCommonality(30, TimeWheelType::DAILY_WHEEL,checktime,task_num){}

    bool AddTimer(shared_ptr<TimeWheelData>&& timer) override{
        if (!timer || timer->status.load(memory_order_acquire) != INIT) {
            return false;
        }
    	//计算当前超时时间
        uint64_t offset = timer->expireTime;
        if (offset == 0) {
            //立即任务，直接执行
            try {
                ThreadPoolBuilder::getInstance()->build()->submit(make_shared<function<void()>>(timer->cb));
                //timer->cb();
            }
            catch (const exception& e) {
                return false;
            }
            return true;
        }
        unique_lock<shared_mutex> lock(mu);
    	//得出插入的槽的索引
        uint64_t insertIndex = (currentSlotIndex_.load(memory_order_acquire) + (offset / DAILY)) % slotNumber_;
        auto& slot = slots_[insertIndex];
        if (!slot) {
            return false;
        }
        TaskStatus except = INIT;
        auto it = find_if(slot->begin(), slot->end(), [&](const shared_ptr<TimeWheelData>& p)->bool {return p == nullptr ;});
        if (it == slot->end()) {
            return false;
        }
        if (timer->status.compare_exchange_strong(except,WAITING,memory_order_acq_rel)) {
            timer->currentIndex.store(insertIndex,memory_order_release);
            *it = ::move(timer);
            timer = nullptr;
            this->currentTaskNumber_.fetch_add(1,memory_order_acq_rel);
            slot_count_[insertIndex].fetch_add(1,memory_order_acq_rel);
            return true;
        }
        return false;
    }
};
//时轮
class TimeWheelHours : public TimeWheelCommonality{
public:
    explicit TimeWheelHours(const uint32_t& checktime = 10,const uint32_t& task_num = 32) : TimeWheelCommonality(24, TimeWheelType::HOURS_WHEEL,checktime,task_num){}

    bool AddTimer(shared_ptr<TimeWheelData>&& timer) override{
        if (!timer || timer->status.load(memory_order_acquire) != INIT) {
            return false;
        }
        //计算当前超时时间
        uint64_t offset = timer->expireTime;
        if (offset == 0) {
            //立即任务，直接执行
            try {
                ThreadPoolBuilder::getInstance()->build()->submit(make_shared<function<void()>>(timer->cb));
                //timer->cb();
            }
            catch (const exception& e) {
                return false;
            }
            return true;
        }
        unique_lock<shared_mutex> lock(mu);
        //得出插入的槽的索引
        uint64_t insertIndex = (currentSlotIndex_.load(memory_order_acquire) + (offset / HOURS)) % slotNumber_;
        auto& slot = slots_[insertIndex];
        if (!slot) {
            return false;
        }
        TaskStatus except = INIT;
        auto it = find_if(slot->begin(), slot->end(), [&](const shared_ptr<TimeWheelData>& p)->bool {return p == nullptr ;});
        if (it == slot->end()) {
            return false;
        }
        if (timer->status.compare_exchange_strong(except,WAITING,memory_order_acq_rel)) {
            timer->currentIndex.store(insertIndex,memory_order_release);
            *it = ::move(timer);
            timer = nullptr;
            this->currentTaskNumber_.fetch_add(1,memory_order_acq_rel);
            slot_count_[insertIndex].fetch_add(1,memory_order_acq_rel);
            return true;
        }
        return false;
    }
};
//分钟轮
class TimeWheelMinute : public TimeWheelCommonality{
public:
    explicit TimeWheelMinute(const uint32_t& checktime = 10,const uint32_t& task_num = 32) : TimeWheelCommonality(60, TimeWheelType::MINUTE_WHEEL,checktime,task_num){}

    bool AddTimer(shared_ptr<TimeWheelData>&& timer) override{
        if (!timer || timer->status.load(memory_order_acquire) != INIT) {
            return false;
        }
        //计算当前超时时间
        uint64_t offset = timer->expireTime;
        if (offset == 0) {
            //立即任务，直接执行
            try {
                ThreadPoolBuilder::getInstance()->build()->submit(make_shared<function<void()>>(timer->cb));
                //timer->cb();
            }
            catch (const exception& e) {
                return false;
            }
            return true;
        }
        unique_lock<shared_mutex> lock(mu);
        //得出插入的槽的索引
        uint64_t insertIndex = (currentSlotIndex_.load(memory_order_acquire) + (offset / MINUTE)) % slotNumber_;
        auto& slot = slots_[insertIndex];
        if (!slot) {
            return false;
        }
        TaskStatus except = INIT;

        auto it = find_if(slot->begin(), slot->end(), [&](const shared_ptr<TimeWheelData>& p)->bool {return p == nullptr ;});
        if (it == slot->end()) {
            return false;
        }
        if (timer->status.compare_exchange_strong(except,WAITING,memory_order_acq_rel)) {
            timer->currentIndex.store(insertIndex,memory_order_release);
            *it = ::move(timer);
            timer = nullptr;
            this->currentTaskNumber_.fetch_add(1,memory_order_acq_rel);
            slot_count_[insertIndex].fetch_add(1,memory_order_acq_rel);
            return true;
        }
        return false;
    }
};
//秒轮
class TimeWheelSeconds : public TimeWheelCommonality{
public:
    explicit TimeWheelSeconds(const uint32_t& checktime = 10,const uint32_t& task_num = 32) : TimeWheelCommonality(60, TimeWheelType::SECONDS_WHEEL,checktime,task_num){}

    bool AddTimer(shared_ptr<TimeWheelData>&& timer) override{
        if (!timer || timer->status.load(memory_order_acquire) != INIT) {
            return false;
        }
        //计算当前超时时间
        uint64_t offset = timer->expireTime;
        if (offset == 0) {
            //立即任务，直接执行
            try {
                ThreadPoolBuilder::getInstance()->build()->submit(make_shared<function<void()>>(timer->cb));
                //timer->cb();
            }
            catch (const exception& e) {
                return false;
            }
            return true;
        }
        unique_lock<shared_mutex> lock(mu);
        //得出插入的槽的索引
        uint64_t insertIndex = (currentSlotIndex_.load(memory_order_acquire) + (offset / SECONDS)) % slotNumber_;
        auto& slot = slots_[insertIndex];
        if (!slot) {
            return false;
        }
        TaskStatus except = INIT;
        auto it = find_if(slot->begin(), slot->end(), [&](const shared_ptr<TimeWheelData>& p)->bool {return p == nullptr ;});
        if (it == slot->end()) {
            return false;
        }
        if (timer->status.compare_exchange_strong(except,WAITING,memory_order_acq_rel)) {
            timer->currentIndex.store(insertIndex,memory_order_release);
            *it = ::move(timer);
            timer = nullptr;
            this->currentTaskNumber_.fetch_add(1,memory_order_acq_rel);
            slot_count_[insertIndex].fetch_add(1,memory_order_acq_rel);
            return true;
        }
        return false;
    }
};
//毫秒轮
class TimeWheelMilliSeconds : public TimeWheelCommonality{
public:
    explicit TimeWheelMilliSeconds(const uint32_t& checktime = 10,const uint32_t& task_num = 32) : TimeWheelCommonality(100, TimeWheelType::MILLISECONDS_WHEEL,checktime,task_num){}

    bool AddTimer(shared_ptr<TimeWheelData>&& timer) override{
        if (!timer || timer->status.load(memory_order_acquire) != INIT) {
            return false;
        }
        //计算当前超时时间
        uint64_t offset = timer->expireTime;
        if (offset == 0) {
            //立即任务，直接执行
            try {
                ThreadPoolBuilder::getInstance()->build()->submit(make_shared<function<void()>>(timer->cb));
                //timer->cb();
            }
            catch (const exception& e) {
                return false;
            }
            return true;
        }
        unique_lock<shared_mutex> lock(mu);
        //得出插入的槽的索引
        uint64_t insertIndex = (currentSlotIndex_.load(memory_order_acquire) + (offset / MILLISECONDS)) % slotNumber_;
        auto& slot = slots_[insertIndex];
        if (!slot) {
            return false;
        }
        TaskStatus except = INIT;

        auto it = find_if(slot->begin(), slot->end(), [&](const shared_ptr<TimeWheelData>& p)->bool {return p == nullptr ;});
        if (it == slot->end()) {
            return false;
        }
        if (timer->status.compare_exchange_strong(except,WAITING,memory_order_acq_rel)) {
            timer->currentIndex.store(insertIndex,memory_order_release);
            *it = ::move(timer);
            timer = nullptr;
            this->currentTaskNumber_.fetch_add(1,memory_order_acq_rel);
            slot_count_[insertIndex].fetch_add(1,memory_order_acq_rel);
            return true;
        }
        return false;
    }
};

class TimeWheel5 {
private:
    atomic<bool> running_{false};
    thread rotate_thread;
    shared_ptr<TimeWheelDaily> dailyTimeWheel_;        // 天级时间轮
    shared_ptr<TimeWheelHours> hoursTimeWheel_;       // 小时级时间轮
    shared_ptr<TimeWheelMinute> minuteTimeWheel_;     // 分钟级时间轮
    shared_ptr<TimeWheelSeconds> secondsTimeWheel_;   // 秒级时间轮
    shared_ptr<TimeWheelMilliSeconds> millisecondsTimeWheel_;  // 毫秒级时间轮

    // 检查所有时间轮是否为空
    bool SlotsEmpty_() {
        return dailyTimeWheel_->IsEmpty() &&
               hoursTimeWheel_->IsEmpty() &&
               minuteTimeWheel_->IsEmpty() &&
               secondsTimeWheel_->IsEmpty() &&
               millisecondsTimeWheel_->IsEmpty();
    }

    // 时间轮迁移函数
    template<class TimeWheel>
    void TickTimeWheel_(shared_ptr<TimeWheel>& timeWheel,const int64_t& timeUnit){
        auto current_index = timeWheel->GetCurrentSlotIndex();
        auto size = timeWheel->getSlotTaskNum(current_index);
        if(!size){
            return;
        }
        auto curr_slot = timeWheel->getCurrentSlot(current_index);
        auto max_size = timeWheel->getSlotMaxTasks();
        TaskStatus except = WAITING;
        for (auto i = 0; i < max_size;++i) {
            auto task = curr_slot->at(i);
            //剩余时间不为0，降级
            uint64_t remainingTime = task->expireTime%timeUnit;
            if (task->expireTime > 0) {
                MigrateTimeWheel_(timeWheel, task, remainingTime);
            }else {
                //为0立即执行
                except = WAITING;
                if (task->status.compare_exchange_strong(except,TaskStatus::ACTIVING,memory_order_acq_rel)) {
                    try {
                        ThreadPoolBuilder::getInstance()->build()->submit(make_shared<function<void()>>(task->cb));
                        //task->cb();
                    }catch (exception& e) {
                        LOG_ERROR<<"TimeWheel5::TickTimeWheel_(): execute callback error: "<<e.what();
                    }
                    task->status.store(DESTROYED,memory_order_release);
                }
            }
        }
    }

    // 迁移辅助函数
    template<class FromTimeWheel>
    bool MigrateTimeWheel_(shared_ptr<FromTimeWheel>& fromTimeWheel, shared_ptr<TimeWheelData>& timer, uint64_t& remainingTime){
        if (!timer)return false;
        //将timer复制到newTimer里
        shared_ptr<TimeWheelData> newTimer = make_shared<TimeWheelData>(*timer);
        //将剩余时间赋值给newTimer的expireTime内
        newTimer->expireTime = remainingTime;
        //先取消原来定时器中的任务，false代表任务已执行完或者任务正在执行，此时不应该继续迁移
        //已经保存副本，无需担心生命周期问题
        if (fromTimeWheel->remarkCancelTask(timer)) {
            //插入新时间轮
            InsertTimeWheel5(newTimer);
            return true;
        }
        return false;
    }

    explicit TimeWheel5(const uint32_t& check_time = 10, const uint32_t& max_task = 32){
        dailyTimeWheel_ = make_shared<TimeWheelDaily>(check_time,max_task);
        hoursTimeWheel_ = make_shared<TimeWheelHours>(check_time,max_task);
        minuteTimeWheel_ = make_shared<TimeWheelMinute>(check_time,max_task);
        secondsTimeWheel_ = make_shared<TimeWheelSeconds>(check_time,max_task);
        millisecondsTimeWheel_ = make_shared<TimeWheelMilliSeconds>(check_time,max_task);
        running_.store(true,memory_order_release);
        rotate_thread = thread([this]() {
            this->RotateTimeWheel5();
        });
    }
public:
    static shared_ptr<TimeWheel5> getInstance() {
        static shared_ptr<TimeWheel5> instance = shared_ptr<TimeWheel5>(new TimeWheel5());
        return instance;
    }

    // 插入和删除定时器
    // 插入任务到合适的时间轮
    bool InsertTimeWheel5(shared_ptr<TimeWheelData> timer) {

        uint64_t remainingTime = timer->expireTime;
        if (remainingTime < MILLISECONDS) {
            LOG_INFO << "定时器已到期，立即执行回调函数";
            //错误提供给调用者处理
            timer->cb();
            return true;
        }
        else if(remainingTime < SECONDS) {
            return millisecondsTimeWheel_->AddTimer(::move(timer));
        }
        else if(remainingTime < MINUTE) {
            return secondsTimeWheel_->AddTimer(::move(timer));
        }
        else if(remainingTime < HOURS) {
            return minuteTimeWheel_->AddTimer(::move(timer));
        }
        else if(remainingTime < DAILY) {
            return hoursTimeWheel_->AddTimer(::move(timer));
        }
        else if(remainingTime < MONTH) {
            return dailyTimeWheel_->AddTimer(::move(timer));
        }
        else{
            LOG_INFO << "超过30天，将被插入到天级时间轮，30天到期";
            timer->expireTime = MONTH;
            return dailyTimeWheel_->AddTimer(::move(timer));
        }
    }

    void DeleteTimeWheel5(shared_ptr<TimeWheelData> timer){
        uint64_t remainingTime = timer->expireTime;
        if (remainingTime < MILLISECONDS) {
            LOG_INFO << "定时器过期时间无效";
        }
        else if(remainingTime < SECONDS) {
            millisecondsTimeWheel_->remarkCancelTask(timer);
        }
        else if(remainingTime < MINUTE) {
            secondsTimeWheel_->remarkCancelTask(timer);
        }
        else if(remainingTime < HOURS) {
            minuteTimeWheel_->remarkCancelTask(timer);
        }
        else if(remainingTime < DAILY) {
            hoursTimeWheel_->remarkCancelTask(timer);
        }
        else{
            dailyTimeWheel_->remarkCancelTask(timer);
        }
    }

    // 驱动时间轮运转
    void RotateTimeWheel5(){
        while(running_.load(memory_order_acquire)) {
            /*
                判断每一级时间轮的是否转过一圈就要用上TimeWheelCommonality的RotateTimeWheel方法。
                当RotateTimeWheel方法为true的时候，就代表下级时间轮准了一圈，这时候就要上一级时间轮转一格
                即使没有任务依旧转动，保证时间片的旋转
            */
            bool millisecondsAdvance = millisecondsTimeWheel_->RotateTimeWheel();
            millisecondsTimeWheel_->TriggerTask();
            if(millisecondsAdvance) {
                bool secondsAdvance = secondsTimeWheel_->RotateTimeWheel();
                TickTimeWheel_<TimeWheelSeconds>(secondsTimeWheel_, (int64_t)SECONDS);

                if(secondsAdvance) {
                    bool minuteAdvance = minuteTimeWheel_->RotateTimeWheel();
                    TickTimeWheel_<TimeWheelMinute>(minuteTimeWheel_, (int64_t)MINUTE);

                    if(minuteAdvance) {
                        bool hoursAdvance = hoursTimeWheel_->RotateTimeWheel();
                        TickTimeWheel_<TimeWheelHours>(hoursTimeWheel_, (int64_t)HOURS);

                        if(hoursAdvance) {
                            bool dailyAdvance = dailyTimeWheel_->RotateTimeWheel();
                            TickTimeWheel_<TimeWheelDaily>(dailyTimeWheel_, (int64_t)DAILY);
                        }
                    }
                }
            }
            usleep(MILLISECONDS);  // 最小ms单位转动一次
        }
    }
};

#endif
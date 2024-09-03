#ifndef MSG_LIST_H
#define MSG_LIST_H

#include "../HeadFile.h"
#include "../connection.hpp"
#include<thread>
#include<mutex>

//使用redis发布订阅模式制作的消息队列
//因为订阅模式下redis-cli是处于阻塞状态的，所以需要使用两个redis连接，一个订阅subscribe，一个发布publish
class MsgList{
private:
    shared_ptr<RedisCache> redis;
    //负责发布消息
    shared_ptr<RedisConnection> publish;
    //负责订阅消息：
    /*
        "message"
        "标题"
        "消息"
    */
    shared_ptr<RedisConnection> subscribe;

    //收到订阅消息的回调函数:订阅标题 消息
    function<void(string,string)> notify_handler;

    static mutex lock;
    static shared_ptr<MsgList> msglist;

    
    
    public:
    MsgList(){
        redis = RedisCache::GetInstance();
        publish = redis->take();
        subscribe = redis->take();
        redis->start();//不会重复调用，只是保证正常工作
        //异步接收订阅消息
        thread t([&](){observer_message();});
        t.detach();
    }
    ~MsgList(){
        redis->recycle(publish);
        redis->recycle(subscribe);
    } 
    //发布消息
    /*
        @param title 订阅标题
        @param message 消息
    */
   bool publishmessage(string title,string message){
        string nosql = "publish "+title + " "+message;
        auto ret = redis->ExecuteNoSQL(publish,nosql.c_str());
        if(ret == 0){
            return true;
        }
        return false;
    }
    //订阅消息
    bool subscribemessage(string title){
        //subscribe命令调用后会进行阻塞，因此不能直接使用rediscommand
        //因为rediscommand分为三部：调用redisAppendCommand将命令写到本地 ，在调用redisBufferWrite将命令写到redis服务器上，最后调用redisGetReply获取结果
        //但是subscribe写到服务器后会阻塞住，也就是说redisGetReply会阻塞当前线程，而该线程也不会以阻塞的方式获取订阅消息
        //订阅消息会在子线程中异步进行读取，subscribemessage只需要提交命令即可
        string nosql = "subscribe "+title;
        if(redisAppendCommand(subscribe->conn,nosql.c_str()) == REDIS_ERR){
            return false;
        }
        //redisBufferWrite会循环发送缓冲区内容，当缓冲区发送完毕时将标志位置为1
        int fine = 0;
        while(!fine){
            if(redisBufferWrite(subscribe->conn,&fine) == REDIS_ERR){
                return false;
            }
        }
        return true;
    }

    //取消订阅
    bool unsubscribe(string title){
        string nosql = "unsubscribe "+title;
        if(redisAppendCommand(subscribe->conn,nosql.c_str()) == REDIS_ERR){
            return false;
        }
        //redisBufferWrite会循环发送缓冲区内容，当缓冲区发送完毕时将标志位置为1
        int fine = 0;
        while(!fine){
            if(redisBufferWrite(subscribe->conn,&fine) == REDIS_ERR){
                return false;
            }
        }
        return true;
    }

    //在子线程中接收订阅消息
    void observer_message(){
        //观察者模式下，接收订阅模式下推送过来的消息，并调用回调函数
        //因为是一个redis连接下且处于订阅模式下的，接收到的redis结果一般都是推送到该频道的消息，不会是其他redis语句
        //接收的消息是订阅模式下的，对应的连接一直处于阻塞状态等待消息读取,需要使用redisGetReply循环读取结果
        //redisGetReply是一个阻塞读取的，一次只会读取一条命令的结果，因此可以很好的处理消息
        //订阅模式的消息有三行，后两行是频道名和消息，我们需要其来调用回调函数
        while(redisGetReply(subscribe->conn,(void**)&subscribe->result) == REDIS_OK){
            if(subscribe->result != nullptr && subscribe->result->element[2] != nullptr && subscribe->result->element[2]->str != nullptr){
                notify_handler(subscribe->result->element[1]->str,subscribe->result->element[2]->str);
            }
            //避免内存泄漏
            freeReplyObject(subscribe->result);
        }
    }

    //注册回调函数
    void registehandler(function<void(string,string)> func){
        notify_handler = func;
    }
};
mutex MsgList::lock;
shared_ptr<MsgList> MsgList::msglist = nullptr;
#endif
/*
    这是一种观察者模式
    传统的观察者模式，观察对象储存观察者列表，当对象发生对应的事件时，通知每一个观察者做出相应的行为
    在redis实现的发布订阅消息队列中，消息队列的每一个频道是被观察的对象，订阅频道的每一个是观察者
    当频道中发布了消息，就会通知每一个订阅者，也就是在子线程中去调用相应的回调函数，回调函数的具体功能就是观察者做出的处理方式
*/
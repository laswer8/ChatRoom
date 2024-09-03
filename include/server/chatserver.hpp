#include"HeadFile.h"
#include"chatservice.hpp"

class ChatRoom{
    private:
    net::TcpServer _server;
    net::EventLoop* _loop;

    void ConnectionCallback(const net::TcpConnectionPtr& conn){
        if(!conn->connected()){
            Service::GetInstance()->CloseException(conn);
            conn->shutdown();
        }
    }

    void MessageCallback(const net::TcpConnectionPtr& conn,net::Buffer* buf,Timestamp time){
        string recvstring = buf->retrieveAllAsString();
        try{
            //解析json文件
            json js = json::parse(recvstring);
            //通过JSON文件中的type字段，根据type决定业务，实现网络与业务的解耦
            //获取相应的业务处理函数
            auto handler = Service::GetInstance()->GetHandler(js["MsgType"].get<int>());
            handler(conn,js,time);
        }catch(exception e){
            LOG_ERROR<<e.what()<<": "+recvstring;
            Service::GetInstance()->CloseException(conn);
        }
    }

    public:
    
    ChatRoom(net::EventLoop* loop,                  //事件循环，即Reactor反应堆
            const net::InetAddress& listenAddr,     //IP+Port
            const string& nameArg                   //服务名
            ):_server(loop,listenAddr,nameArg),_loop(loop){
                //设置用户连接断开回调
                _server.setConnectionCallback(bind(&ChatRoom::ConnectionCallback,this,_1));
                //设置用户读写回调
                _server.setMessageCallback(bind(&ChatRoom::MessageCallback,this,_1,_2,_3));

                _server.setThreadNum(4);//1个I/O线程，3个工作线程

                auto cache = DatabaseCache::GetInstance();
            }

    void start(){
        _server.start();
    }

};
#include "../include/server/HeadFile.h"

//muduo网络库，测试
//1. 组装TcpServer对象
//2. 创建EventLoop事件循环对象的指针
//3. 明确TcpServer的构造函数参数，TcpServer没有默认构造
//4. 注册用户连接和断开以及用户读写事件的回调函数
//5. 设置服务器线程数
//6. 开启事件循环
class ChatRoom{
    private:
    net::TcpServer _server;
    net::EventLoop* _loop;

    void ConnectionCallback(const net::TcpConnectionPtr& conn){
        if(conn->connected())
            cout<<"Connection-"<<conn->peerAddress().toIpPort()<<"-to-"<<conn->localAddress().toIpPort()<<" state: online"<<endl;
        else{
            cout<<"Connection-"<<conn->peerAddress().toIpPort()<<"-to-"<<conn->localAddress().toIpPort()<<" state: offline"<<endl;
            conn->shutdown(); //释放套接字资源
            //_loop->quit();  //退出事件循环，也相当于释放了资源
        }


    }
    void MessageCallback(const net::TcpConnectionPtr& conn,net::Buffer* buf,Timestamp time){
            string recvmsg = buf->retrieveAllAsString();
            cout<<"time: "<<time.toString()<<" message: "<<recvmsg<<endl;
            conn->send(recvmsg);
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
            }

    void start(){
        _server.start();
    }

};

void muduo_test(){
    net::EventLoop loop;
    net::InetAddress addr("127.0.0.1",6666);
    ChatRoom chat(&loop,addr,"ChatServer");
    chat.start();
    loop.loop(); //相当于epoll_wait等待新用户连接 与 等待已连接用户的读写事件
}

int main(){
    muduo_test();
    return 0;
}
#include "../../include/server/HeadFile.h"
#include"../../include/server/chatserver.hpp"

void reset(int n){
    Service::GetInstance()->reset();
    exit(-1);
}

int main(int argc,char** argv){
    if (argc < 3)
    {
        cerr << "command invalid! example: ./ChatServer 127.0.0.1 6000" << endl;
        return -1;
    }
    //处理ctrl+c信号的中断
    signal(SIGINT,reset);
    //处理ctrl+/造成的退出
    signal(SIGQUIT,reset);
    //处理ctrl+z造成的退出
    signal(SIGTSTP,reset);
    net::EventLoop loop;
    net::InetAddress addr(argv[1],atoi(argv[2]));
    ChatRoom server(&loop,addr,"ChatRoomServer");
    server.start();
    loop.loop();
    return 0;
}
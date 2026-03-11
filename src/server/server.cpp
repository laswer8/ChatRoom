#include "../../include/server/HeadFile.h"
#include "../../include/server/chatserver.hpp"
#include "new_connectpool.hpp"

// void reset(int n){
//     WebSocketChatRoom::GetInstance()->stop();
//     exit(-1);
// }

int main(int argc,char** argv){
    if (argc < 3)
    {
        cerr << "command invalid! example: ./ChatServer 127.0.0.1 6000" << endl;
        return -1;
    }

    // net::EventLoop loop;
    // net::InetAddress addr(argv[1],atoi(argv[2]));
    // ChatRoom server(&loop,addr,"ChatRoomServer");
    // server.start();
    // loop.loop();
    // //处理ctrl+c信号的中断
    // signal(SIGINT,reset);
    // //处理ctrl+/造成的退出
    // signal(SIGQUIT,reset);
    // //处理ctrl+z造成的退出
    // signal(SIGTSTP,reset);
    MysqlConnectionPoolBuilder::GetInstance()->build();
    RedisConnectionPoolBuilder::GetInstance()->build();
    auto server = make_shared<WebSocketChatRoom>();
    server->start(argv[1],atoi(argv[2]));
    cout<<"----------over------------"<<endl;
    sleep(20);
    return 0;
}
#include "HeadFile.h"
#include "public.hpp"
#include"userhandler.hpp"
#include "offlinemsghandler.hpp"
#include"friendhandler.hpp"
#include"grouphandler.hpp"
#include "./redis/redismessagelist.hpp"
//业务模块
using MsgHandler = function<void(const net::TcpConnectionPtr&,json&,Timestamp)>;



//单例
class Service{
public:
    //登录 JSON: username + password 检测username对应的pwd 比较pwd是否相同
    void login(const net::TcpConnectionPtr& conn,json& js,Timestamp time){
        string username = js["username"].get<string>();
        string pwd = js["password"].get<string>();
        User user;
        user.setusername(username);
        user.setpassword(pwd);
        if(handler->check(user)){
            if(user.getstate() == "online"){
                json result;
                result["MsgType"] = MsgType::LOGIN_MSG;
                result["ErrNo"] = 1;
                result["message"] = "用户已登录";
                conn->send(result.dump());
            }else{
                if(handler->online(user)){
                    cout<<"online"<<endl;
                    js["id"] = user.getid();
                    string name = user.getname();
                    js["name"] = name;
                    js["email"] = user.getemail();
                    js["phone"] = user.getphone();
                    js["ErrNo"]=0;
                    //登录成功，记录用户连接
                    {
                        lock_guard<mutex> guard(m);
                        Userconnmap.insert({username,{name,conn}});
                    }
                    //用户登录成功后，向redis订阅该用户
                    msglist->subscribemessage(username);
                    //查询该用户是否有离线消息，有的话就带回去
                    OfflineMsg msg;
                    msg.Setusername(username);
                    vector<OfflineMsg> msgs = OffMsghandler->query(msg);
                    js["OffLineMsgNum"] = msgs.size();
                    if(!msgs.empty()){
                        js["OffLineMsgs"] = msgs;
                        OffMsghandler->remove(msg);  
                    }
                    //查询该用户的好友
                    Friend f;
                    f.setusername(username);
                    vector<User> friends = friendhandler->getfriend(f);
                    js["FriendNum"] = friends.size();
                    if(!friends.empty()){
                        js["Friends"]=friends;
                        //通知好友上线
                        json ackjs;
                        ackjs["MsgType"] = MsgType::USER_ONLINE_ACK;
                        ackjs["username"] = username;
                        string ackjsstring = ackjs.dump();
                        for(auto& f:friends){
                            //如果对方不在线不需要发送，登录会获得当前状态
                            if(f.getstate() == "online"){
                                auto it = Userconnmap.find(f.getusername());
                                if(it != Userconnmap.end()){
                                    //如果与好友在同一服务器，直接发送
                                    it->second.second->send(ackjsstring);
                                }else{
                                    //不在同一服务器，发布订阅消息
                                    msglist->publishmessage(f.getusername(),ackjsstring);
                                }
                            }
                        }
                    }
                    //查询该用户的好友申请
                    FriendReq req;
                    req.setusername(username);
                    vector<OfflineMsg> vec = friendhandler->getrequest(req);
                    js["FriendRequestNum"] = vec.size();
                    if(!vec.empty()){
                        js["FriendRequests"]=vec;
                    }
                    //查询该用户的群组消息
                    auto groups = grouphandler->queryGroup(username);
                    js["groupnum"]=groups.size();
                    if(!groups.empty()){
                        js["groups"]=groups;
                    }
                    conn->send(js.dump());
                }else{
                    json result;
                    result["MsgType"] =  MsgType::LOGIN_MSG;
                    result["ErrNo"] = 1;
                    result["message"] = "登录异常,请稍后再试";
                    conn->send(result.dump());
                }
                
            }
            
        }
        else{
            json result;
            result["MsgType"] =  MsgType::TIP_MSG;
            result["ErrNo"] = 1;
            result["message"] = "用户名或密码错误";
            conn->send(result.dump());
        }
    }

    //注册: username + password + name + email + phone
    void regist(const net::TcpConnectionPtr& conn,json& js,Timestamp time){
        string username = js["username"].get<string>();
        string pwd = js["password"].get<string>();
        string name = js["name"].get<string>();
        User user;
        user.setname(name);
        user.setusername(username);
        user.setpassword(pwd);
        user.setemail(js["email"].get<string>());
        user.setphone(js["phone"].get<string>());
        bool ret = handler->insert(user);
        if(ret){
            if(ret){
                lock_guard<mutex> guard(m);
                Userconnmap.insert({username, {name, conn}});
            }
            //用户登录成功后，向redis订阅该用户
            msglist->subscribemessage(username);
            js["ErrNo"] = 0;
            js["userid"] = user.getid();
            conn->send(js.dump());    
        }else{
            json result;
            result["MsgType"] =  MsgType::REGIST_MSG;
            result["ErrNo"] = 1;
            result["message"]="该用户已存在";
            conn->send(result.dump());
        }
        
    }

    //客户端手动退出：username
    void drop(const net::TcpConnectionPtr& conn,json& js,Timestamp time){
        string username = js["username"].get<string>();
        User user;
        user.setusername(username);
        handler->offline(user);
        {
            lock_guard<mutex> guard(m);
            auto it = Userconnmap.find(username);
            Userconnmap.erase(it);
        }
        // 用户下线，在redis中取消订阅
        msglist->unsubscribe(username);
        auto vec = friendhandler->queryfriendid(username);
        if(vec.empty())
            return;
        for(const string& s:vec){
            auto it = Userconnmap.find(s);
            if(it != Userconnmap.end()){
                it->second.second->send(js.dump());
                continue;
            }
            if(handler->isonline(s)){
                msglist->publishmessage(s,js.dump());
            }
            
        }
    }

    //服务端异常，重置用户数据库
    void reset(){
        //将所有online状态的用户状态更新为offline
        handler->ResetState();
    }

    //异常退出
    void CloseException(const net::TcpConnectionPtr& conn){
        User user;
        {
            lock_guard<mutex> guard(m);
            for(auto it = Userconnmap.begin();it != Userconnmap.end();it++){
                if(it->second.second == conn){
                    //在连接映射中删除对应记录
                    user.setusername(it->first);
                    Userconnmap.erase(it);
                    break;
                }
            }
        }
        string username = user.getusername();
        //用户下线，在redis中取消订阅
        msglist->unsubscribe(username);
        //修改其状态
        if (!username.empty())
        {
            handler->offline(user);
            auto vec = friendhandler->queryfriendid(username);
            if (vec.empty())
                return;
            json js;
            js["MsgType"] = MsgType::DROP_MSG;
            js["username"] = username;
            for (const string &s : vec)
            {
                auto it = Userconnmap.find(s);
                if (it != Userconnmap.end())
                {
                    it->second.second->send(js.dump());
                    continue;
                }
                if (handler->isonline(s))
                {
                    msglist->publishmessage(s, js.dump());
                }
            }
            return;
        }
    }

    //获取消息类型id对应的回调函数
    MsgHandler GetHandler(int msgid){
        auto it = Servicemap.find(msgid);
        if(it == Servicemap.end()){
            //返回一个空操作，输出错误
            return [&](const net::TcpConnectionPtr&,json&,Timestamp){
                LOG_INFO<<"MsgType: "<<msgid<<" NOT FOUND";
            };
        }else
            return Servicemap[msgid];
    }

    static shared_ptr<Service> GetInstance(){
        if(service == nullptr){
            lock_guard<mutex> guard(m);
            if(service == nullptr){
                service = shared_ptr<Service>(new Service());
            }
        }
        return service;
    }

    //私聊
    /*
        {
            "MsgType":7,
            "fromusername":xxx,
            "fromname":"xxx"
            "tousername":xxx,
            "message":"xxxx..."
        }
        可以让客户端保存好友id与好友名的映射表，也是为了降低数据库压力"
    */
   void c2cChat(const net::TcpConnectionPtr& conn,json& js,Timestamp time){
        string tousername = js["tousername"].get<string>();
        {
            //需要添加锁，防止查找过程中，Userconnmap发生增删，造成获取的迭代器错误或混乱
            lock_guard<mutex> guard(m);
            auto it = Userconnmap.find(tousername);
            //好友处于同一台服务器且对方在线需要在临界区内操作，因为如果离开临界区，对方连接随时可能被移除
            if(it != Userconnmap.end()){
                it->second.second->send(js.dump());
                return;
            }
        }
        //可能不存在该用户
        User user;
        user.setusername(tousername);
        if(handler->query(user))
            if(user.getstate() == "online"){
                //好友不在线，查询对方是否在线，如果在线表示不在同一台服务器上，需要发布订阅消息
                msglist->publishmessage(tousername,js.dump());
                return;
            }else{
                //好友不在线，需要储存离线消息
                OfflineMsg off;
                off.Setusername(tousername);
                off.SetJsonMsg(js.dump());
                bool ret = OffMsghandler->insert(off);
                if(ret == false){
                    json result;
                    result["MsgType"] =  MsgType::TIP_MSG;
                    result["ErrNo"] = 1;
                    result["message"] = "好友不在线,且消息离线储存异常";
                    conn->send(result.dump());
                }
            }
        
   }

    //申请添加好友 fromname fromusername tousername message
    void AddFriendRequest(const net::TcpConnectionPtr& conn,json& js,Timestamp time){
        string username = js["tousername"].get<string>();
        string fromusername = js["fromusername"].get<string>();
        FriendReq req;
        req.setusername(username);
        req.setfromname(fromusername);
        req.setjsonmsg(js.dump());
        Friend f;
        f.setusername(fromusername);
        f.setfriendname(username);
        if(!friendhandler->check(f)){
            if(friendhandler->requst(req)){
                {
                    //需要添加锁，防止查找过程中，Userconnmap发生增删，造成获取的迭代器错误或混乱
                    lock_guard<mutex> guard(m);
                    auto it = Userconnmap.find(username);
                    //好友在线需要在临界区内操作，因为如果离开临界区，对方连接随时可能被移除
                    if(it != Userconnmap.end()){
                        it->second.second->send(js.dump());
                        return;
                    }
                }
                //好友不在线，查询对方是否在线，如果在线表示不在同一台服务器上，需要发布订阅消息
                User user;
                user.setusername(username);
                if(handler->isonline(username)){
                    msglist->publishmessage(username,js.dump());
                    return;
                }
            }else{
                json js;
                js["MsgType"]= MsgType::TIP_MSG;
                js["ErrNo"] = 1;
                js["message"]="请求已存在";
                conn->send(js.dump());
            }
        }else{
            json js;
            js["MsgType"]= MsgType::TIP_MSG;
            js["ErrNo"] = 1;
            js["message"]="该好友已存在";
            conn->send(js.dump());
        }
    }

    //拒绝好友申请 username fromusername
    void UnAccFriendRequest(const net::TcpConnectionPtr& conn,json& js,Timestamp time){
        string username = js["username"].get<string>();
        string requsername = js["fromusername"].get<string>();
        FriendReq req;
        req.setusername(username);
        req.setfromname(requsername);
        friendhandler->removeREQ(req);
        js["MsgType"]= MsgType::FRIEND_UNACC_MSG;
        js["ErrNo"]=0;
        js["message"]=Userconnmap[username].first+"拒绝了你的好友申请";
        {
            //需要添加锁，防止查找过程中，Userconnmap发生增删，造成获取的迭代器错误或混乱
            lock_guard<mutex> guard(m);
            auto it = Userconnmap.find(requsername);
            //好友在线需要在临界区内操作，因为如果离开临界区，对方连接随时可能被移除
            if(it != Userconnmap.end()){
                it->second.second->send(js.dump());
                return;
            }
        }
        //好友不在线，查询对方是否在线，如果在线表示不在同一台服务器上，需要发布订阅消息
        User user;
        user.setusername(requsername);
        if(handler->isonline(requsername)){
            msglist->publishmessage(requsername,js.dump());
            return;
        }
        //好友不在线，需要储存离线消息
        OfflineMsg off;
        off.Setusername(requsername);
        off.SetJsonMsg(js.dump());
        OffMsghandler->insert(off);
    }

    //同意好友申请 id username email phone fromusername    
    void AccFriendRequest(const net::TcpConnectionPtr& conn,json& js,Timestamp time){
        string username = js["username"].get<string>();
        string requsername = js["fromusername"].get<string>();
        Friend f;
        f.setfriendname(requsername);
        f.setusername(username);
        
        if(friendhandler->check(f)){
            js["MsgType"]=MsgType::TIP_MSG;
            js["ErrNo"]=1;
            js["message"]="你们已经是好友";
            conn->send(js.dump());
            return;
        }
        FriendReq req;
        req.setusername(username);
        req.setfromname(requsername);
        
        auto ret = friendhandler->accept(f);
        if(!ret){
            js["MsgType"]=MsgType::TIP_MSG;
            js["ErrNo"] = 1;
            js["message"]="添加好友失败";
            conn->send(js.dump());
            return;
        }
        friendhandler->removeREQ(req);
        //获取好友信息
        User friendinfo;
        friendinfo.setusername(requsername);
        handler->query(friendinfo);
        js["MsgType"]=MsgType::FRIEND_ACC_TO_ACK;
        js["fromid"] = friendinfo.getid();
        js["fromname"] = friendinfo.getname();
        js["fromemail"] = friendinfo.getemail();
        js["fromphone"] = friendinfo.getphone();
        js["fromstate"] = "online";
        json retjs;
        retjs["MsgType"]= MsgType::FRIEND_ACC_FROM_ACK;
        retjs["fromusername"]=username;
        retjs["fromid"] = js["id"].get<int>();
        retjs["fromname"]=Userconnmap[username].first;
        retjs["fromemail"] = js["email"].get<string>();
        retjs["fromphone"] = js["phone"].get<string>();
        retjs["fromstate"] = "online";
        retjs["message"]="我同意了你的好友请求,快来聊天吧";
        //裁剪发送回同意方的数据包，不传递不需要的信息
        js.erase("id");
        js.erase("email");
        js.erase("phone");
        js.erase("username");
        {
            //需要添加锁，防止查找过程中，Userconnmap发生增删，造成获取的迭代器错误或混乱
            lock_guard<mutex> guard(m);
            auto it = Userconnmap.find(requsername);
            //好友在线需要在临界区内操作，因为如果离开临界区，对方连接随时可能被移除
            if(it != Userconnmap.end()){
                conn->send(js.dump());
                it->second.second->send(retjs.dump());
                return;
            }
        }
        js["fromstate"] = "offline";
        retjs["fromstate"] = "offline";
        conn->send(js.dump());
        //好友不在线，查询对方是否在线，如果在线表示不在同一台服务器上，需要发布订阅消息
        User user;
        user.setusername(requsername);
        if(handler->isonline(requsername)){
            msglist->publishmessage(requsername,retjs.dump());
            return;
        }
        //好友不在线，需要储存离线消息
        OfflineMsg off;
        off.Setusername(requsername);
        off.SetJsonMsg(retjs.dump());
        OffMsghandler->insert(off);
    }

    //删除好友 username + friendusername
    void DelFriend(const net::TcpConnectionPtr& conn,json& js,Timestamp time){
        string username = js["username"].get<string>();
        string friendusername = js["friendusername"].get<string>();
        //删除好友
        Friend f;
        f.setfriendname(friendusername);
        f.setusername(username);
        if(!friendhandler->check(f)){
            js["MsgType"]=MsgType::TIP_MSG;
            js["ErrNo"]=1;
            js["message"]="你们还不是好友";
            conn->send(js.dump());
            return;
        }
        friendhandler->removeFriend(f);
        //删除相关的请求好友记录
        FriendReq req;
        req.setusername(username);
        req.setfromname(friendusername);
        friendhandler->removeREQ(req);
        {
            //需要添加锁，防止查找过程中，Userconnmap发生增删，造成获取的迭代器错误或混乱
            lock_guard<mutex> guard(m);
            auto it = Userconnmap.find(friendusername);
            //好友在线需要在临界区内操作，因为如果离开临界区，对方连接随时可能被移除
            if(it != Userconnmap.end()){
                it->second.second->send(js.dump());
                return;
            }
        }
        //好友不在线，查询对方是否在线，如果在线表示不在同一台服务器上，需要发布订阅消息
        if(handler->isonline(friendusername)){
            msglist->publishmessage(friendusername,js.dump());
            return;
        }
        //好友不在线，不需要储存发送离线消息
    }

    //创建群聊 MsgType groupname desc username
    void CreateGroup(const net::TcpConnectionPtr& conn,json& js,Timestamp time){
        string creatorusername = js["username"].get<string>();
        string groupname = js["groupname"].get<string>();
        string desc = js["desc"].get<string>();
        Group g;
        g.setname(groupname);
        g.setdesc(desc);
        g.setcreatorname(creatorusername);
        auto ret = grouphandler->create(g);
        if(ret){
            js["groupid"]=g.getid();
            conn->send(js.dump());
        }else{
            js["MsgType"]=MsgType::TIP_MSG;
            js["ErrNo"]=1;
            js["message"] = "群聊"+groupname+"创建失败";
            conn->send(js.dump());
        }
    }

    //申请加入群聊 MsgType groupid requsername reqname message
    void RequestAddGroup(const net::TcpConnectionPtr& conn,json& js,Timestamp time){
        int groupid = js["groupid"].get<int>();
        string username = js["requsername"].get<string>();
        string msg = js["message"].get<string>();
        auto ret = grouphandler->request(username,groupid,msg);
        if(ret){
            js["MsgType"] =MsgType::GROUP_REQ_MSG;
            vector<string> rooters = grouphandler->queryGroupCreator(groupid);
            lock_guard<mutex> guard(m);
            int id;
            for(const auto& rooter : rooters){
                auto it = Userconnmap.find(rooter);
                //好友在线需要在临界区内操作，因为如果离开临界区，对方连接随时可能被移除
                if(it != Userconnmap.end()){
                    it->second.second->send(js.dump());
                    return;
                }
                //好友不在线，查询对方是否在线，如果在线表示不在同一台服务器上，需要发布订阅消息
                if(handler->isonline(rooter)){
                    msglist->publishmessage(rooter,js.dump());
                    return;
                }
                //好友不在线，需要储存离线消息
                OfflineMsg off;
                off.Setusername(rooter);
                off.SetJsonMsg(js.dump());
                OffMsghandler->insert(off);
            }
        }else{
            json js;
            js["MsgType"]=MsgType::TIP_MSG;
            js["ErrNo"]=1;
            js["message"]="请求失败";
            conn->send(js.dump());
        }
    }

    //同意加入群聊申请 MsgType groupid requsername
    void RequestAccGroup(const net::TcpConnectionPtr& conn,json& js,Timestamp time){
        int groupid = js["groupid"].get<int>();
        string requsername = js["requsername"].get<string>();
        auto ret = grouphandler->accrequest(groupid,requsername);
        if(ret){
            js["groupinfo"] = grouphandler->groupinfo(groupid);
            conn->send(js.dump());
            {
                //需要添加锁，防止查找过程中，Userconnmap发生增删，造成获取的迭代器错误或混乱
                lock_guard<mutex> guard(m);
                auto it = Userconnmap.find(requsername);
                //好友在线需要在临界区内操作，因为如果离开临界区，对方连接随时可能被移除
                if(it != Userconnmap.end()){
                    it->second.second->send(js.dump());
                    return;
                }
            }
            //好友不在线，查询对方是否在线，如果在线表示不在同一台服务器上，需要发布订阅消息
            if(handler->isonline(requsername)){
                msglist->publishmessage(requsername,js.dump());
                return;
            }
            //好友不在线，需要储存离线消息
            OfflineMsg off;
            off.Setusername(requsername);
            off.SetJsonMsg(js.dump());
            OffMsghandler->insert(off);
        }else{
            js["MsgType"]=MsgType::TIP_MSG;
            js["ErrNo"]=1;
            js["message"]="申请已过期";
            conn->send(js.dump());
        }
    }

    //拒绝用户申请加入群聊 groupid requsername
    void RequestRefuseGroup(const net::TcpConnectionPtr& conn,json& js,Timestamp time){
        int groupid = js["groupid"].get<int>();
         string requsername = js["requsername"].get<string>();
        auto ret = grouphandler->deleterequest(groupid,requsername);
        if(!ret){
            js["MsgType"]=MsgType::TIP_MSG;
            js["ErrNo"]=1;
            js["message"]="该请求不存在";
            conn->send(js.dump());
        }
    }

    //退出群聊 groupid username name
    void quitGroup(const net::TcpConnectionPtr& conn,json& js,Timestamp time){
        int groupid = js["groupid"].get<int>();
        string username = js["username"].get<string>();
        auto ret = grouphandler->quitgroup(username,groupid);
        if(ret){
            // js["MsgType"]=MsgType::GROUP_REMOVE_MSG;
            // js["ErrNo"]=0;
            // js["message"]="退出群聊成功";
            // conn->send(js.dump());
            json retjs;
            retjs["MsgType"] = MsgType::GROUP_REMOVE_ACK;
            retjs["fromusername"] = username;
            retjs["fromname"] = js["name"].get<string>();
            retjs["groupid"] = groupid;
            vector<string> vec = grouphandler->queryGroupMembers(groupid,username);
            lock_guard<mutex> guard(m);
            int id;
            OfflineMsg off;
            for(const string& s:vec){
                auto it = Userconnmap.find(s);
                if(it != Userconnmap.end()){
                    it->second.second->send(retjs.dump());
                    continue;
                }
                //好友不在线，查询对方是否在线，如果在线表示不在同一台服务器上，需要发布订阅消息
                if(handler->isonline(s)){
                    msglist->publishmessage(s,retjs.dump());
                    continue;
                }
            }
            return;
        }
        js["MsgType"]=MsgType::TIP_MSG;
        js["ErrNo"]=1;
        js["message"]="退出群聊失败";
        conn->send(js.dump());
    }

    //切换用户群权限
    void shiftGroupRole(const net::TcpConnectionPtr& conn,json& js,Timestamp time){
        int groupid = js["groupid"].get<int>();
        string username = js["username"].get<string>();
        string role = js["role"].get<string>();
        grouphandler->shiftrole(username,groupid,role);
    }

    //查询群组 key
    void getGroupInfo(const net::TcpConnectionPtr& conn,json& js,Timestamp time){
        string key = js["key"].get<string>();
        auto vec = grouphandler->likequery(key);
        if(vec.empty()){
            js["count"]=0;
            conn->send(js.dump());
            return;
        }
        js["count"]=vec.size();
        js["grouplist"]=vec;
        conn->send(js.dump());
    }

    //群聊 groupid fromusername fromname message
    void GroupChat(const net::TcpConnectionPtr& conn,json& js,Timestamp time){
        int groupid = js["groupid"].get<int>();
        string username = js["fromusername"].get<string>();
        vector<string> vec = grouphandler->queryGroupMembers(groupid,username);
        lock_guard<mutex> guard(m);
        int id;
        OfflineMsg off;
        for(const string& s:vec){
            auto it = Userconnmap.find(s);
            if(it != Userconnmap.end()){
                it->second.second->send(js.dump());
                continue;
            }
            if(handler->isonline(s)){
                msglist->publishmessage(s,js.dump());
            }else{
                off.Setusername(s);
                off.SetJsonMsg(js.dump());
                OffMsghandler->insert(off);
            }
            
        }
    }

    //订阅消息回调
    void subscribecallback(string title,string message){
        try
        {
            // 在当前工作线程，如果接收到订阅的用户消息，就将其发送

            lock_guard<mutex> guard(m);
            auto it = Userconnmap.find(title);
            if (it != Userconnmap.end())
            {
                it->second.second->send(message);
                return;
            }

            OfflineMsg off;
            off.Setusername(title);
            off.SetJsonMsg(message);
            OffMsghandler->insert(off);
        }
        catch (const std::exception &e)
        {
            LOG_INFO << e.what() << "订阅处理异常";
        }
    }
private:
    Service(){
        Servicemap.insert({MsgType::LOGIN_MSG,          bind(   &Service::login,               this,_1,_2,_3)});
        Servicemap.insert({MsgType::REGIST_MSG,         bind(   &Service::regist,              this,_1,_2,_3)});
        Servicemap.insert({MsgType::DROP_MSG,           bind(   &Service::drop,                this,_1,_2,_3)});
        Servicemap.insert({MsgType::ONE_CHAT_MSG,       bind(   &Service::c2cChat,             this,_1,_2,_3)});
        Servicemap.insert({MsgType::FRIEND_REQ_MSG,     bind(   &Service::AddFriendRequest,    this,_1,_2,_3)});
        Servicemap.insert({MsgType::FRIEND_ACC_MSG,     bind(   &Service::AccFriendRequest,    this,_1,_2,_3)});
        Servicemap.insert({MsgType::FRIEND_DEL_MSG,     bind(   &Service::DelFriend,           this,_1,_2,_3)});
        Servicemap.insert({MsgType::FRIEND_UNACC_MSG,   bind(   &Service::UnAccFriendRequest,  this,_1,_2,_3)});
        Servicemap.insert({MsgType::GROUP_CREATE_MSG,   bind(   &Service::CreateGroup,         this,_1,_2,_3)});
        Servicemap.insert({MsgType::GROUP_REQ_MSG,      bind(   &Service::RequestAddGroup,     this,_1,_2,_3)});
        Servicemap.insert({MsgType::GROUP_ACC_MSG,      bind(   &Service::RequestAccGroup,     this,_1,_2,_3)});
        Servicemap.insert({MsgType::GROUP_REFUSE_MSG,   bind(   &Service::RequestRefuseGroup,  this,_1,_2,_3)});
        Servicemap.insert({MsgType::GROUP_CHAT_MSG,     bind(   &Service::GroupChat,           this,_1,_2,_3)});
        Servicemap.insert({MsgType::GROUP_REMOVE_MSG,   bind(   &Service::quitGroup,           this,_1,_2,_3)});
        Servicemap.insert({MsgType::GROUP_INFO_MSG,     bind(   &Service::getGroupInfo,        this,_1,_2,_3)});
        Servicemap.insert({MsgType::GROUP_ROLE_MSG,     bind(   &Service::shiftGroupRole,      this,_1,_2,_3)});
        handler         = make_shared<UserHandler>();
        OffMsghandler   = make_shared<OfflineMsgHandler>();
        friendhandler   = make_shared<FriendHandler>();
        grouphandler    = make_shared<GroupHandler>();
        msglist = make_shared<MsgList>();
        msglist->registehandler(bind(&Service::subscribecallback,this,_1,_2));
    }
    //建立类型id与业务回调函数的映射关系
    shared_ptr<UserHandler> handler;
    shared_ptr<OfflineMsgHandler> OffMsghandler;
    shared_ptr<FriendHandler> friendhandler;
    shared_ptr<GroupHandler> grouphandler;
    unordered_map<int,MsgHandler> Servicemap;   //消息类型与业务函数的映射表
    unordered_map<string,pair<string,net::TcpConnectionPtr>> Userconnmap; //用户账号与用户连接的映射表
    shared_ptr<MsgList> msglist;
    static shared_ptr<Service> service;
    static mutex m;
};
shared_ptr<Service> Service::service = nullptr;
mutex Service::m;
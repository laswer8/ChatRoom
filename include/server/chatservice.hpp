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
    //登录 JSON: id + pwd 检测id对应的pwd 比较pwd是否相同
    void login(const net::TcpConnectionPtr& conn,json& js,Timestamp time){
        int id = js["userid"].get<int>();
        string name = js["username"].get<string>();
        string pwd = js["password"].get<string>();
        User user;
        user.setid(id);
        user.setpassword(pwd);
        if(handler->check(user)){
            if(user.getstate() == "online"){
                json result;
                result["MsgType"] = MsgType::TIP_MSG;
                result["ErrNo"] = 1;
                result["message"] = "用户已登录";
                conn->send(result.dump());
            }else{
                if(handler->online(user)){
                    js["ErrNo"]=0;
                    //登录成功，记录用户连接
                    {
                        lock_guard<mutex> guard(m);
                        Userconnmap.insert({id,{name,conn}});
                    }
                    //用户登录成功后，向redis订阅该用户
                    msglist->subscribemessage(to_string(id));
                    //查询该用户是否有离线消息，有的话就带回去
                    OfflineMsg msg;
                    msg.SetId(id);
                    vector<OfflineMsg> msgs = OffMsghandler->query(msg);
                    js["OffLineMsgNum"] = msgs.size();
                    if(!msgs.empty()){
                        js["OffLineMsgs"] = msgs;
                        OffMsghandler->remove(msg);  
                    }
                    //查询该用户的好友
                    Friend f;
                    f.setmyid(id);
                    vector<User> friends = friendhandler->getfriend(f);
                    js["FriendNum"] = friends.size();
                    if(!friends.empty()){
                        js["Friends"]=friends;
                    }
                    //查询该用户的好友申请
                    FriendReq req;
                    req.setid(id);
                    vector<OfflineMsg> vec = friendhandler->getrequest(req);
                    js["FriendRequestNum"] = vec.size();
                    if(!vec.empty()){
                        js["FriendRequests"]=vec;
                    }
                    //查询该用户的群组消息
                    auto groups = grouphandler->queryGroup(id);
                    js["groupnum"]=groups.size();
                    if(!groups.empty()){
                        js["groups"]=groups;
                    }
                    conn->send(js.dump());
                }else{
                    json result;
                    result["MsgType"] =  MsgType::TIP_MSG;
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

    //注册: name + password
    void regist(const net::TcpConnectionPtr& conn,json& js,Timestamp time){
        string name = js["username"].get<string>();
        string pwd = js["password"].get<string>();

        User user;
        user.setname(name);
        user.setpassword(pwd);
        bool ret = handler->insert(user);
        if(ret){
            ret = handler->online(user);
            if(ret){
                lock_guard<mutex> guard(m);
                Userconnmap.insert({user.getid(), {name, conn}});
            }
            //用户登录成功后，向redis订阅该用户
            msglist->subscribemessage(to_string(user.getid()));
            js["ErrNo"] = 0;
            js["userid"] = user.getid();
            conn->send(js.dump());    
        }else{
            json result;
            result["MsgType"] =  MsgType::TIP_MSG;
            result["ErrNo"] = 1;
            result["message"]="该用户已存在";
            conn->send(result.dump());
        }
        
    }

    //客户端手动退出：id
    void drop(const net::TcpConnectionPtr& conn,json& js,Timestamp time){
        int id = js["id"].get<int>();
        User user;
        user.setid(id);
        if(handler->offline(user)){
            {
                lock_guard<mutex> guard(m);
                auto it = Userconnmap.find(id);
                Userconnmap.erase(it);
            }
            //用户下线，在redis中取消订阅
            msglist->unsubscribe(to_string(id));
        }else{
            json result;
            result["MsgType"] =  MsgType::TIP_MSG;
            result["ErrNo"] = 1;
            result["message"] = "状态异常";
            conn->send(result.dump());
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
                    user.setid(it->first);
                    Userconnmap.erase(it);
                    break;
                }
            }
        }
        //用户下线，在redis中取消订阅
        msglist->unsubscribe(to_string(user.getid()));
        //修改其状态
        if(user.getid() != -1)
            handler->offline(user);
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
            "fromid":xxx,
            "fromname":"xxx"
            "toid":xxx,
            "message":"xxxx..."
        }
        可以让客户端保存好友id与好友名的映射表，也是为了降低数据库压力"
    */
   void c2cChat(const net::TcpConnectionPtr& conn,json& js,Timestamp time){
        int toid = js["toid"].get<int>();
        {
            //需要添加锁，防止查找过程中，Userconnmap发生增删，造成获取的迭代器错误或混乱
            lock_guard<mutex> guard(m);
            auto it = Userconnmap.find(toid);
            //好友处于同一台服务器且对方在线需要在临界区内操作，因为如果离开临界区，对方连接随时可能被移除
            if(it != Userconnmap.end()){
                it->second.second->send(js.dump());
                return;
            }
        }
        //可能不存在该用户
        User user;
        user.setid(toid);
        if(handler->query(user))
            if(user.getstate() == "online"){
                //好友不在线，查询对方是否在线，如果在线表示不在同一台服务器上，需要发布订阅消息
                msglist->publishmessage(to_string(toid),js.dump());
                return;
            }else{
                //好友不在线，需要储存离线消息
                OfflineMsg off;
                off.SetId(toid);
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

    //申请添加好友
    void AddFriendRequest(const net::TcpConnectionPtr& conn,json& js,Timestamp time){
        int id = js["toid"].get<int>();
        int fromid = js["fromid"].get<int>();
        FriendReq req;
        req.setid(id);
        req.setfromid(fromid);
        req.setjsonmsg(js.dump());
        Friend f;
        f.setmyid(fromid);
        f.setfriendid(id);
        if(!friendhandler->check(f)){
            if(friendhandler->requst(req)){
                {
                    //需要添加锁，防止查找过程中，Userconnmap发生增删，造成获取的迭代器错误或混乱
                    lock_guard<mutex> guard(m);
                    auto it = Userconnmap.find(id);
                    //好友在线需要在临界区内操作，因为如果离开临界区，对方连接随时可能被移除
                    if(it != Userconnmap.end()){
                        it->second.second->send(js.dump());
                        return;
                    }
                }
                //好友不在线，查询对方是否在线，如果在线表示不在同一台服务器上，需要发布订阅消息
                User user;
                user.setid(id);
                if(handler->query(user) && user.getstate() == "online"){
                    msglist->publishmessage(to_string(id),js.dump());
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

    //拒绝好友申请
    void UnAccFriendRequest(const net::TcpConnectionPtr& conn,json& js,Timestamp time){
        int userid = js["userid"].get<int>();
        int reqid = js["fromid"].get<int>();
        FriendReq req;
        req.setid(userid);
        req.setfromid(reqid);
        friendhandler->removeREQ(req);
        js["MsgType"]= MsgType::FRIEND_UNACC_MSG;
        js["ErrNo"]=0;
        js["message"]=Userconnmap[userid].first+"拒绝了你的好友申请";
        {
            //需要添加锁，防止查找过程中，Userconnmap发生增删，造成获取的迭代器错误或混乱
            lock_guard<mutex> guard(m);
            auto it = Userconnmap.find(reqid);
            //好友在线需要在临界区内操作，因为如果离开临界区，对方连接随时可能被移除
            if(it != Userconnmap.end()){
                it->second.second->send(js.dump());
                return;
            }
        }
        //好友不在线，查询对方是否在线，如果在线表示不在同一台服务器上，需要发布订阅消息
        User user;
        user.setid(reqid);
        if(handler->query(user) && user.getstate() == "online"){
            msglist->publishmessage(to_string(reqid),js.dump());
            return;
        }
        //好友不在线，需要储存离线消息
        OfflineMsg off;
        off.SetId(reqid);
        off.SetJsonMsg(js.dump());
        OffMsghandler->insert(off);
    }

    //同意好友申请
    void AccFriendRequest(const net::TcpConnectionPtr& conn,json& js,Timestamp time){
        int userid = js["userid"].get<int>();
        int reqid = js["fromid"].get<int>();
        Friend f;
        f.setfriendid(reqid);
        f.setmyid(userid);
        if(friendhandler->check(f)){
            js["MsgType"]=MsgType::TIP_MSG;
            js["ErrNo"]=1;
            js["message"]="你们已经是好友";
            conn->send(js.dump());
            return;
        }
        FriendReq req;
        req.setid(userid);
        req.setfromid(reqid);
        auto ret = friendhandler->accept(f);
        if(!ret){
            js["MsgType"]=MsgType::TIP_MSG;
            js["ErrNo"] = 1;
            js["message"]="添加好友失败";
            conn->send(js.dump());
            return;
        }
        friendhandler->removeREQ(req);
        js["MsgType"]=MsgType::FRIEND_ACC_TO_ACK;
        json retjs;
        retjs["MsgType"]= MsgType::FRIEND_ACC_FROM_ACK;
        retjs["fromid"]=userid;
        retjs["fromname"]=Userconnmap[userid].first;
        retjs["fromstate"] = "online";
        retjs["toid"]=reqid;
        retjs["message"]="我同意了你的好友请求,快来聊天吧";
        {
            //需要添加锁，防止查找过程中，Userconnmap发生增删，造成获取的迭代器错误或混乱
            lock_guard<mutex> guard(m);
            auto it = Userconnmap.find(reqid);
            //好友在线需要在临界区内操作，因为如果离开临界区，对方连接随时可能被移除
            if(it != Userconnmap.end()){
                js["fromstate"] = "online";
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
        user.setid(reqid);
        if(handler->query(user) && user.getstate() == "online"){
            msglist->publishmessage(to_string(reqid),retjs.dump());
            return;
        }
        //好友不在线，需要储存离线消息
        OfflineMsg off;
        off.SetId(reqid);
        off.SetJsonMsg(retjs.dump());
        OffMsghandler->insert(off);
    }

    //删除好友
    void DelFriend(const net::TcpConnectionPtr& conn,json& js,Timestamp time){
        int userid = js["userid"].get<int>();
        int friendid = js["friendid"].get<int>();
        //删除好友
        Friend f;
        f.setfriendid(friendid);
        f.setmyid(userid);
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
        req.setid(userid);
        req.setfromid(friendid);
        friendhandler->removeREQ(req);
        js["MsgType"]=MsgType::FRIEND_DEL_MSG;
        conn->send(js.dump());
        {
            //需要添加锁，防止查找过程中，Userconnmap发生增删，造成获取的迭代器错误或混乱
            lock_guard<mutex> guard(m);
            auto it = Userconnmap.find(friendid);
            //好友在线需要在临界区内操作，因为如果离开临界区，对方连接随时可能被移除
            if(it != Userconnmap.end()){
                it->second.second->send(js.dump());
                return;
            }
        }
        //好友不在线，查询对方是否在线，如果在线表示不在同一台服务器上，需要发布订阅消息
        User user;
        user.setid(friendid);
        if(handler->query(user) && user.getstate() == "online"){
            msglist->publishmessage(to_string(friendid),js.dump());
            return;
        }
        //好友不在线，需要储存离线消息
        OfflineMsg off;
        off.SetId(friendid);
        off.SetJsonMsg(js.dump());
        OffMsghandler->insert(off);
    }

    //创建群聊 MsgType groupname desc userid
    void CreateGroup(const net::TcpConnectionPtr& conn,json& js,Timestamp time){
        int creatorid = js["userid"].get<int>();
        string groupname = js["groupname"].get<string>();
        string desc = js["desc"].get<string>();
        Group g;
        g.setname(groupname);
        g.setdesc(desc);
        g.setcreatorid(creatorid);
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

    //申请加入群聊 MsgType groupid reqid reqname message
    void RequestAddGroup(const net::TcpConnectionPtr& conn,json& js,Timestamp time){
        int groupid = js["groupid"].get<int>();
        int userid = js["reqid"].get<int>();
        string msg = js["message"].get<string>();
        auto ret = grouphandler->request(userid,groupid,msg);
        if(ret){
            js["MsgType"] =MsgType::GROUP_REQ_MSG;
            vector<string> rooters = grouphandler->queryGroupCreator(groupid);
            lock_guard<mutex> guard(m);
            int id;
            for(const auto& rooter : rooters){
                id = atoi(rooter.c_str());
                auto it = Userconnmap.find(id);
                //好友在线需要在临界区内操作，因为如果离开临界区，对方连接随时可能被移除
                if(it != Userconnmap.end()){
                    it->second.second->send(js.dump());
                    return;
                }
                //好友不在线，查询对方是否在线，如果在线表示不在同一台服务器上，需要发布订阅消息
                User user;
                user.setid(id);
                if(handler->query(user) && user.getstate() == "online"){
                    msglist->publishmessage(to_string(id),js.dump());
                    return;
                }
                //好友不在线，需要储存离线消息
                OfflineMsg off;
                off.SetId(id);
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

    //同意加入群聊申请 MsgType groupid reqid
    void RequestAccGroup(const net::TcpConnectionPtr& conn,json& js,Timestamp time){
        int groupid = js["groupid"].get<int>();
        int reqid = js["reqid"].get<int>();
        auto ret = grouphandler->accrequest(groupid,reqid);
        if(ret){
            js["groupinfo"] = grouphandler->groupinfo(groupid);
            conn->send(js.dump());
            {
                //需要添加锁，防止查找过程中，Userconnmap发生增删，造成获取的迭代器错误或混乱
                lock_guard<mutex> guard(m);
                auto it = Userconnmap.find(reqid);
                //好友在线需要在临界区内操作，因为如果离开临界区，对方连接随时可能被移除
                if(it != Userconnmap.end()){
                    it->second.second->send(js.dump());
                    return;
                }
            }
            //好友不在线，查询对方是否在线，如果在线表示不在同一台服务器上，需要发布订阅消息
            User user;
            user.setid(reqid);
            if(handler->query(user) && user.getstate() == "online"){
                msglist->publishmessage(to_string(reqid),js.dump());
                return;
            }
            //好友不在线，需要储存离线消息
            OfflineMsg off;
            off.SetId(reqid);
            off.SetJsonMsg(js.dump());
            OffMsghandler->insert(off);
        }else{
            js["MsgType"]=MsgType::TIP_MSG;
            js["ErrNo"]=1;
            js["message"]="申请已过期";
            conn->send(js.dump());
        }
    }

    //拒绝用户申请加入群聊 groupid reqid
    void RequestRefuseGroup(const net::TcpConnectionPtr& conn,json& js,Timestamp time){
        int groupid = js["groupid"].get<int>();
        int reqid = js["reqid"].get<int>();
        auto ret = grouphandler->deleterequest(groupid,reqid);
        if(!ret){
            js["MsgType"]=MsgType::TIP_MSG;
            js["ErrNo"]=1;
            js["message"]="该请求不存在";
            conn->send(js.dump());
        }
    }

    //退出群聊 groupid userid username
    void quitGroup(const net::TcpConnectionPtr& conn,json& js,Timestamp time){
        int groupid = js["groupid"].get<int>();
        int userid = js["userid"].get<int>();
        auto ret = grouphandler->quitgroup(userid,groupid);
        if(ret){
            js["MsgType"]=MsgType::GROUP_REMOVE_MSG;
            js["ErrNo"]=0;
            js["message"]="退出群聊成功";
            conn->send(js.dump());
            json retjs;
            retjs["MsgType"] = MsgType::GROUP_REMOVE_ACK;
            retjs["fromid"] = userid;
            retjs["fromname"] = js["username"];
            retjs["groupid"] = groupid;
            vector<string> vec = grouphandler->queryGroupMembers(groupid,userid);
            lock_guard<mutex> guard(m);
            int id;
            OfflineMsg off;
            User user;
            for(const string& s:vec){
                id = atoi(s.c_str());
                auto it = Userconnmap.find(id);
                if(it != Userconnmap.end()){
                    it->second.second->send(retjs.dump());
                    continue;
                }
                //好友不在线，查询对方是否在线，如果在线表示不在同一台服务器上，需要发布订阅消息
                user.setid(id);
                if(handler->query(user) && user.getstate() == "online"){
                    msglist->publishmessage(to_string(id),retjs.dump());
                    continue;
                }
            }
            return;
        }
        js["MsgType"]=MsgType::TIP_MSG;
        js["ErrNo"]=1;
        js["message"]="管理员无法退出群聊";
        conn->send(js.dump());
    }

    //切换用户群权限
    void shiftGroupRole(const net::TcpConnectionPtr& conn,json& js,Timestamp time){
        int groupid = js["groupid"].get<int>();
        int userid = js["userid"].get<int>();
        string role = js["role"].get<string>();
        grouphandler->shiftrole(userid,groupid,role);
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

    //群聊 groupid fromid fromname message
    void GroupChat(const net::TcpConnectionPtr& conn,json& js,Timestamp time){
        int groupid = js["groupid"].get<int>();
        int userid = js["fromid"].get<int>();
        vector<string> vec = grouphandler->queryGroupMembers(groupid,userid);
        lock_guard<mutex> guard(m);
        int id;
        OfflineMsg off;
        User user;
        for(const string& s:vec){
            id = atoi(s.c_str());
            auto it = Userconnmap.find(id);
            if(it != Userconnmap.end()){
                it->second.second->send(js.dump());
                continue;
            }
            user.setid(id);
            if(handler->query(user) && user.getstate() == "online"){
                msglist->publishmessage(to_string(id),js.dump());
            }else{
                off.SetId(id);
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
            int id = atoi(title.c_str());

            lock_guard<mutex> guard(m);
            auto it = Userconnmap.find(id);
            if (it != Userconnmap.end())
            {
                it->second.second->send(message);
                return;
            }

            OfflineMsg off;
            off.SetId(id);
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
    unordered_map<int,pair<string,net::TcpConnectionPtr>> Userconnmap; //用户id与用户连接的映射表
    shared_ptr<MsgList> msglist;
    static shared_ptr<Service> service;
    static mutex m;
};
shared_ptr<Service> Service::service = nullptr;
mutex Service::m;
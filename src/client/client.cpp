#include <iostream>
#include <fstream>
#include <string>
#include <algorithm>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <thread>
#include <chrono>
#include <semaphore.h>
#include <fstream>
#include <functional>
#include <utility>
#include "../../include/server/public.hpp"
#include "../../include/server/db/user.hpp"
#include "../../include/server/db/group.hpp"
#include "../../include/server/db/friend.hpp"
#include "../../include/server/db/offlinemsg.hpp"
#include <nlohmann/json.hpp>

using namespace std;
using namespace placeholders; // 占位符
using json = nlohmann::json;

static const uint32_t MAXSIZE = 10240; // 10k
static const string   CLIENTJSON = "clientinfo.json"; //保存了当前用户信息的json文件 
static User currentuser;                  // 当前登录用户
static vector<User> friendlist;           // 当前用户的好友列表
static vector<Group> grouplist;           // 当前用户的群聊列表
static vector<OfflineMsg> offlinemsglist; // 当前用户的离线消息
static vector<OfflineMsg> friendreqlist;  // 当前用户的好友请求
static mutex mlock;                       // 用于保证输出的正确性
static int clifd = -1;                    // 为了在异常退出时能够正确关闭套接字，需要将其定义为全局
static unordered_map<int, string> id2name;
static unordered_map<string, string> user2name;
static bool status;                       //确认用户状态
static sem_t rwcond;
// 获取当前时间，在聊天时需要添加时间信息
string CurrentTime()
{
    auto ret = std::chrono::system_clock::to_time_t(chrono::system_clock::now());
    return ctime(&ret);
}
// 对服务器返回消息的类型进行解析
using RESPONSE = function<void(const json &, string)>;
class handler
{
private:
    static unordered_map<int, RESPONSE> responsemap;
    // 1.    ONE_CHAT_MSG            私聊
    void P_one_chat_msg(const json &j, string time)
    {
        string fromusername = j["fromusername"].get<string>();
        string fromname = j["fromname"].get<string>();
        string message = j["message"].get<string>();
        replace(message.begin(), message.end(), '\x01', ' ');
        user2name[fromusername] = fromname;
        lock_guard<mutex> guard(mlock);
        cout << "-----------------------------------你有一条消息--------------------------------------------" << endl;
        cout << "time: " << time << endl;
        cout << "fromusername: " << fromusername << endl;
        cout << "name: " << fromname << endl;
        cout << "message: " << message << endl;
        cout << "------------------------------------------------------------------------------------------" << endl;
    }
    // 2.    FRIEND_REQ_MSG          好友请求
    void P_friend_req_msg(const json &j, string time)
    {
        string fromusername = j["fromusername"].get<string>();
        string fromname = j["fromname"].get<string>();
        string message = j["message"].get<string>();
        replace(message.begin(), message.end(), '\x01', ' ');
        user2name[fromusername] = fromname;
        lock_guard<mutex> guard(mlock);
        cout << "----------------------------------你有一条好友申请-----------------------------------------" << endl;
        cout << "time: " << time << endl;
        cout << "fromusername: " << fromusername << endl;
        cout << "name: " << fromname << endl;
        cout << "message: " << message << endl;
        cout << "------------------------------------------------------------------------------------------" << endl;
    }
    // 3.1    FRIEND_ACC_FROM_ACK          对方同意好友申请
    void P_friend_acc_from_ack(const json &j, string time)
    {
        int    fromid = j["fromid"].get<int>();
        string fromusername = j["fromusername"].get<string>();
        string fromname = j["fromname"].get<string>();
        string fromemail = j["fromemail"].get<string>();
        string fromphone = j["fromphone"].get<string>();
        string fromstate = j["fromstate"].get<string>();
        string message = j["message"].get<string>();
        replace(message.begin(), message.end(), '\x01', ' ');
        user2name[fromusername] = fromname;
        User u(fromid, fromname,fromusername,"",fromemail,fromphone, fromstate);
        friendlist.emplace_back(u);
        lock_guard<mutex> guard(mlock);
        cout << "-----------------------------------你有一条消息--------------------------------------------" << endl;
        cout << "time: " << time << endl;
        cout << "fromusername: " << fromusername << endl;
        cout << "name: " << fromname << endl;
        cout << "message: " << message << endl;
        cout << "------------------------------------------------------------------------------------------" << endl;
    }
    // 3.2   FRIEND_ACC_TO_ACK          同意对方好友申请
    void P_friend_acc_to_ack(const json &j, string time)
    {
        int    fromid = j["fromid"].get<int>();
        string fromusername = j["fromusername"].get<string>();
        string fromname = j["fromname"].get<string>();
        string fromemail = j["fromemail"].get<string>();
        string fromphone = j["fromphone"].get<string>();
        string fromstate = j["fromstate"].get<string>();
        User u(fromid, fromname,fromusername,"",fromemail,fromphone, fromstate);
        friendlist.emplace_back(u);
    }

    // 4.    FRIEND_DEL_MSG          删除好友
    void P_friend_del_msg(const json &j, string time)
    {
        string username = j["username"].get<string>();
        auto it = find_if(friendlist.begin(), friendlist.end(), [&](User &u)
                          {if(u.getusername() == username)return true; });
        friendlist.erase(it);
    }
    // 5.    FRIEND_UNACC_MSG        拒绝好友申请
    void P_friend_unacc_msg(const json &j, string time)
    {
        string msg = j["message"].get<string>();
        replace(msg.begin(), msg.end(), '\x01', ' ');
        lock_guard<mutex> guard(mlock);
        cout << "------------------------------------------INFO-------------------------------------------" << endl;
        cout << "time: " << time << endl;
        cout << "message: " << msg << endl;
    }

    // 6.    GROUP_CREATE_MSG        创建群聊
    void P_group_create_msg(const json &j, string time)
    {
        int gid = j["groupid"].get<int>();
        string groupname = j["groupname"].get<string>();
        string desc = j["desc"].get<string>();
        replace(groupname.begin(), groupname.end(), '\x01', ' ');
        replace(desc.begin(), desc.end(), '\x01', ' ');
        id2name[gid]=groupname;
        Group g;
        g.setid(gid);
        g.setname(groupname);
        g.setdesc(desc);
        g.setcreatorname(currentuser.getusername());
        GroupMember m;
        m.setid(currentuser.getid());
        m.setname(currentuser.getname());
        m.setusername(currentuser.getusername());
        m.setemail(currentuser.getemail());
        m.setphone(currentuser.getphone());
        m.setrole("creator");
        m.setstate("online");
        g.getmembers().emplace_back(m);
        grouplist.emplace_back(g);
        lock_guard<mutex> guard(mlock);
        cout << "--------------------------------------创建成功--------------------------------------------" << endl;
        cout << "time: " << time << endl;
        cout << "groupid: " << gid << endl;
        cout << "groupname: " << groupname << endl;
        cout << "desc: " << desc << endl;
        cout << "------------------------------------------------------------------------------------------" << endl;
    }
    // 7.    GROUP_REQ_MSG           申请加入群聊
    void P_group_req_msg(const json &j, string time)
    {
        int gid = j["groupid"].get<int>();
        string requsername = j["requsername"].get<string>();
        string reqname = j["reqname"].get<string>();
        string message = j["message"].get<string>();
        replace(message.begin(), message.end(), '\x01', ' ');
        user2name[requsername] = reqname;
        lock_guard<mutex> guard(mlock);
        cout << "----------------------------------你有一条群聊申请-----------------------------------------" << endl;
        cout << "time: " << time << endl;
        cout << "groupid: " << gid << endl;
        cout << "groupname: "<<id2name[gid]<<endl;
        cout << "requsername: " << requsername << endl;
        cout << "reqname: " << reqname << endl;
        cout << "message: " << message << endl;
        cout << "------------------------------------------------------------------------------------------" << endl;
    }
    // 8.    GROUP_ACC_MSG           加入申请同意
    void P_group_acc_msg(const json &j, string time)
    {
        Group g = j["groupinfo"].get<Group>();
        auto it = find_if(grouplist.begin(),grouplist.end(),[&](Group& t){if(g.getid() == t.getid())return true;});
        if(it == grouplist.end()){ 
            grouplist.emplace_back(g);
        }
        else{
            it->getmembers()=g.getmembers();
        }
        id2name[g.getid()] = g.getname();
    }
    // 9.    GROUP_CHAT_MSG          群聊
    void P_group_chat_msg(const json &j, string time)
    {
        string fromusername = j["fromusername"].get<string>();
        string fromname = j["fromname"].get<string>();
        string message = j["message"].get<string>();
        replace(message.begin(), message.end(), '\x01', ' ');
        int groupid = j["groupid"].get<int>();
        string groupname = id2name[groupid];
        lock_guard<mutex> guard(mlock);
        cout << "----------------------------------" << groupname << "-----------------------------------------" << endl;
        cout << "time: " << time << endl;
        cout << "fromusername: " << fromusername << endl;
        cout << "fromname: " << fromname << endl;
        cout << "message: " << message << endl;
        cout << "------------------------------------------------------------------------------------------" << endl;
    }
    // 10.   GROUP_REMOVE_MSG        退出群聊
    void P_group_remove_msg(const json &j, string time)
    {
        
    }
    // 11. GROUP_REMOVE_ACK      退出群聊消息通知
    void P_group_remove_ack(const json &j, string time)
    {
        int groupid = j["groupid"].get<int>();
        string fromusername = j["fromusername"].get<string>();
        string fromname = j["fromname"].get<string>();
        auto it = find_if(grouplist.begin(), grouplist.end(), [&](Group &g)
                          {if(g.getid() == groupid)return true; });
        if (it != grouplist.end())
        {
            string groupname = it->getname();
            replace(groupname.begin(), groupname.end(), '\x01', ' ');
            auto& vec = it->getmembers();
            auto t_it = find_if(vec.begin(), vec.end(), [&](GroupMember &m)
                                {if(m.getusername() == fromusername)return true; });
            if (t_it != vec.end())
            {
                vec.erase(t_it);
            }
            
            lock_guard<mutex> guard(mlock);
            cout << "----------------------------------" << groupname << "-----------------------------------------" << endl;
            cout << "time: " << time << endl;
            cout << "fromusername: " << fromusername << endl;
            cout << "fromname: " << fromname << endl;
            cout << "message: " << fromname << "退出群聊" << endl;
            cout << "------------------------------------------------------------------------------------------" << endl;
        }
    }
    // 12.   GROUP_REFUSE_MSG        拒绝加入申请
    void P_group_refuse_msg(const json &j, string time)
    {
    }
    // 13.   GROUP_ROLE_MSG          切换权限
    void P_group_role_msg(const json &j, string time)
    {
    }
    // 14.   TIP_MSG                 系统提示
    void P_tip_msg_msg(const json &j, string time)
    {
        if (j["ErrNo"].get<int>())
        {
            lock_guard<mutex> guard(mlock);
            cout << "----------------------------------INFO-----------------------------------------" << endl;
            cout << "time: " << time << endl;
            string message = j["message"].get<string>();
            replace(message.begin(), message.end(), '\x01', ' ');
            cout << "message: " << message << endl;
        }
    }
    // 15.   LOGIN_MSG               登录消息
    void P_login_msg(const json &js, string time){
        if(js["ErrNo"].get<int>() == 0){
            //登录成功
            currentuser.setid(js["id"].get<int>());
            currentuser.setname(js["name"].get<string>());
            currentuser.setemail(js["email"].get<string>());
            currentuser.setphone(js["phone"].get<string>());
            currentuser.setstate("online");
            user2name[currentuser.getusername()] = currentuser.getname();
            if (js.contains("Friends"))
            {
                // cout<<"friend有"<<endl;
                friendlist = js["Friends"].get<vector<User>>();
                for(const auto& i : friendlist){
                    user2name[i.getusername()] = i.getname();
                }
            }
            if (js.contains("OffLineMsgs"))
            {
                // cout<<"OffLineMsgs有"<<endl;
                offlinemsglist = js["OffLineMsgs"].get<vector<OfflineMsg>>();
                
            }
            if (js.contains("groups"))
            {
                // cout<<"groups有"<<endl;
                grouplist = js["groups"].get<vector<Group>>();
                for(auto i : grouplist){
                    id2name[i.getid()] = i.getname();
                    for(auto j :i.getmembers()){
                        user2name[j.getusername()] = j.getname();
                    }
                }
            }
            if (js.contains("FriendRequests"))
            {
                // cout<<"FriendRequests有"<<endl;
                friendreqlist = js["FriendRequests"].get<vector<OfflineMsg>>();
                json j;
                for(const auto& i : friendreqlist){
                    j = json::parse(i.GetJsonMsg());
                    user2name[j["fromusername"].get<string>()] = j["fromname"].get<string>();
                }
                
            }
            status = true;
        }else{
            //登录失败
            system("clear");
            string message = js["message"].get<string>();
            replace(message.begin(), message.end(), '\x01', ' ');
            cout << "======================login failed=====================" << endl;
            cout << "message: " << message << endl;
            cout << "=======================================================" << endl;
        }
        //信号量加一
        sem_post(&rwcond);
    }
    // 16.   REGISTER_MSG            注册消息
    void P_register_msg(const json &js, string time){
        if(js["ErrNo"].get<int>() == 0){
            //注册成功
            currentuser.setid(js["userid"].get<int>());
            status = true;
        }else{
            // 注册失败
            system("clear");
            string message = js["message"].get<string>();
            replace(message.begin(), message.end(), '\x01', ' ');
            cout << "======================register failed=====================" << endl;
            cout << "message: " << message << endl;
            cout << "=======================================================" << endl;
        }
        sem_post(&rwcond);
    }
    // 17.   DROP_MSG                注销消息
    void P_drop_msg(const json &js, string time){
        string username = js["username"].get<string>();
        for(auto& f:friendlist){
            if(f.getusername() == username){
                lock_guard<mutex> guard(mlock);
                f.setstate("offline");
                break;
            }
        }
    }
    //  18. USER_ONLINE_ACK         好友上线消息
    void P_user_online_ack(const json &js, string time){
        string username = js["username"].get<string>();
        for(auto& f:friendlist){
            if(f.getusername() == username){
                lock_guard<mutex> guard(mlock);
                f.setstate("online");
                break;
            }
        }
    }
public:
    handler()
    {
        responsemap.insert({MsgType::ONE_CHAT_MSG, bind(&handler::P_one_chat_msg, this, _1, _2)});
        responsemap.insert({MsgType::FRIEND_REQ_MSG, bind(&handler::P_friend_req_msg, this, _1, _2)});
        responsemap.insert({MsgType::FRIEND_ACC_FROM_ACK, bind(&handler::P_friend_acc_from_ack, this, _1, _2)});
        responsemap.insert({MsgType::FRIEND_ACC_TO_ACK, bind(&handler::P_friend_acc_to_ack, this, _1, _2)});
        responsemap.insert({MsgType::FRIEND_DEL_MSG, bind(&handler::P_friend_del_msg, this, _1, _2)});
        responsemap.insert({MsgType::FRIEND_UNACC_MSG, bind(&handler::P_friend_unacc_msg, this, _1, _2)});
        responsemap.insert({MsgType::GROUP_CREATE_MSG, bind(&handler::P_group_create_msg, this, _1, _2)});
        responsemap.insert({MsgType::GROUP_REQ_MSG, bind(&handler::P_group_req_msg, this, _1, _2)});
        responsemap.insert({MsgType::GROUP_ACC_MSG, bind(&handler::P_group_acc_msg, this, _1, _2)});
        responsemap.insert({MsgType::GROUP_CHAT_MSG, bind(&handler::P_group_chat_msg, this, _1, _2)});
        responsemap.insert({MsgType::GROUP_REMOVE_MSG, bind(&handler::P_group_remove_msg, this, _1, _2)});
        responsemap.insert({MsgType::GROUP_REMOVE_ACK, bind(&handler::P_group_remove_ack, this, _1, _2)});
        responsemap.insert({MsgType::GROUP_REFUSE_MSG, bind(&handler::P_group_refuse_msg, this, _1, _2)});
        responsemap.insert({MsgType::GROUP_ROLE_MSG, bind(&handler::P_group_role_msg, this, _1, _2)});
        responsemap.insert({MsgType::TIP_MSG, bind(&handler::P_tip_msg_msg, this, _1, _2)});
        responsemap.insert({MsgType::DROP_MSG, bind(&handler::P_drop_msg, this, _1, _2)});
        responsemap.insert({MsgType::LOGIN_MSG, bind(&handler::P_login_msg, this, _1, _2)});
        responsemap.insert({MsgType::REGIST_MSG, bind(&handler::P_register_msg, this, _1, _2)});
        responsemap.insert({MsgType::USER_ONLINE_ACK, bind(&handler::P_user_online_ack, this, _1, _2)});
    }

    void reload(string jsonmsg, string time)
    {
        try
        {
            json js = json::parse(jsonmsg);
            auto func = responsemap.find(js["MsgType"].get<int>());
            if (func != responsemap.end())
            {
                responsemap[js["MsgType"].get<int>()](js, time);
            }
        }
        catch (const std::exception &e)
        {
            std::cerr << e.what() <<": "<<jsonmsg<<endl;
            close(clifd);
            exit(-1);
        }
    }
};
unordered_map<int, RESPONSE> handler::responsemap;

// 显示当前用户信息
void showCurrentUserInfo()
{
    cout << "=====================login user========================" << endl;
    cout << "userid: " << currentuser.getid() << endl;
    cout << "name: " << currentuser.getname() << endl;
    cout << "username: " << currentuser.getusername() << endl;
    cout << "email: " << currentuser.getemail() << endl;
    cout << "phone: " << currentuser.getphone() << endl;
    cout << "state: " << currentuser.getstate() << endl;
    cout << "=====================friend list=======================" << endl;
    cout << "count: " << friendlist.size() << endl;
    if (!friendlist.empty())
    {
        for (auto &i : friendlist)
        {
            cout << "friendid: " << i.getid() << "\tname: " << i.getname()<< "\tusername: " << i.getusername() << "\temail: " << i.getemail()<< "\tphone: " << i.getphone()<< "\tstate: " << i.getstate() << endl;
        }
    }
    cout << "===================friend request list==================" << endl;
    cout << "count: " << friendreqlist.size() << endl;
    if (!friendreqlist.empty())
    {
        json j;
        for (auto &i : friendreqlist)
        {
            j = json::parse(i.GetJsonMsg());
            cout << "request username: " << j["fromusername"].get<string>() << "\trequest name: " << j["fromname"].get<string>() << "\tmessage: " << j["message"].get<string>() << endl;
        }
    }
    cout << "=====================Group list=======================" << endl;
    cout << "count: " << grouplist.size() << endl;
    if (!grouplist.empty())
    {
        string gname, desc;
        for (auto &i : grouplist)
        {
            gname = i.getname();
            desc = i.getdesc();
            replace(gname.begin(), gname.end(), '\x01', ' ');
            replace(desc.begin(), desc.end(), '\x01', ' ');
            cout << "{\n\tgroupid: " << i.getid() << endl
                 << "\tgroupname: " << gname << endl
                 << "\tdesc: " << desc << endl;
            auto &members = i.getmembers();
            cout << "\tmembercount: " << members.size() << endl;
            cout << "\tmembers: [" << endl;
            for (auto &m : members)
            {
                cout << "\t\t{\n\t\t\tid: " << m.getid() << endl
                     << "\t\t\tname: " << m.getname() << endl
                     << "\t\t\tusername: " << m.getusername() << endl
                     << "\t\t\temail: " << m.getemail() << endl
                     << "\t\t\tphone: " << m.getphone() << endl
                     << "\t\t\trole: " << m.getrole() << endl
                     << "\t\t}" << endl;
            }
            cout << "\t]" << endl
                 << "}" << endl;
        }
    }
    cout << "=====================OfflineMessage list=======================" << endl;
    cout << "count: " << offlinemsglist.size() << endl;
    if (!offlinemsglist.empty())
    {
        handler h;
        for (auto &s : offlinemsglist)
        {
            h.reload(s.GetJsonMsg(), CurrentTime());
        }
    }
}

// 接收服务器的线程
void recvTaskHandler(int clientfd)
{
    try
    {
        char c[MAXSIZE];
        string msg;
        handler h;
        while (true)
        {
            memset(c, 0, MAXSIZE);
            recv(clientfd, c, MAXSIZE, 0);
            msg = c;
            h.reload(msg, CurrentTime());
        }
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << ": 接收服务器线程错误"<<endl;
    }
}

// 合法读取字符串
void input(string &msg)
{
    msg.clear();
    string s;
    getline(cin,s,'\n');
    for(auto& c:s){
        if(c == '\n')
            break;
        if(c == ' ')
            msg +='\x01';
        else if(c == '\\')
            msg += "\\\\";
        else if(c == '\"')
            msg += "\\\"";
        else
            msg += c;
    }
}
// 格式化输入
bool check(string str)
{
    if(str.size() > 20){
        return false;
    }
    auto it = str.find(' ');
    if (it == string::npos)
    {
        return false;
    }
    return true;
}

bool safeid(string& id){
    id.clear();
    getline(cin,id,'\n');
    for(char& c:id){
        if(!isdigit(c)){
            return false;
        }
    }
    return true;
}
string getrole(int gid, int uid)
{
    auto it = find_if(grouplist.begin(), grouplist.end(), [&](Group &g)
                   {if((g.getid()) == gid)return true; });
    if (it == grouplist.end())
    {
        return string();
    }
    auto members = it->getmembers();
    auto m_it = find_if(members.begin(), members.end(), [&](GroupMember &m)
                     { if((m.getid()) == uid)return true; });
    if (m_it == members.end())
    {
        return string();
    }
    return m_it->getrole();
}

// 功能类
class Menu
{
private:
    // help : 显示所有可执行的命令,不需要向服务端通信
    void help(int clientfd = -1)
    {
        lock_guard<mutex> guard(mlock);
        cout << "======================Command Help==========================" << endl;
        for (const auto &s : commandmap)
        {
            cout << "command: " << s.first << "\tinfo: " << s.second << endl;
        }
    }
    // chat: 向服务端发送聊天消息
    void chat(int clientfd)
    {
        string tousername;
        string message;
        cout << "请输入对方账号: ";
        input(tousername);
        cout << "请输入聊天消息：" << endl;
        input(message);
        json js;
        js["MsgType"] = MsgType::ONE_CHAT_MSG;
        js["fromusername"] = currentuser.getusername();
        js["fromname"] = currentuser.getname();
        js["tousername"] = tousername;
        js["message"] = message;
        string jsonmsg = js.dump();
        int ret = send(clientfd, jsonmsg.c_str(), jsonmsg.size(), 0);
        if (ret < jsonmsg.size())
        {
            cerr << "聊天消息发送失败: " << ret << endl;
            return;
        }
    }
    // fadd
    void fadd(int clientfd)
    {
        string tousername;
        string message;
        cout << "请输入对方账号: ";
        input(tousername);
        cout << "请输入验证消息：" << endl;
        input(message);
        json js;
        js["MsgType"] = MsgType::FRIEND_REQ_MSG;
        js["fromusername"] = currentuser.getusername();
        js["fromname"] = currentuser.getname();
        js["tousername"] = tousername;
        js["message"] = message;
        string jsonmsg = js.dump();
        int ret = send(clientfd, jsonmsg.c_str(), jsonmsg.size(), 0);
        if (ret < jsonmsg.size())
        {
            cerr << "请求好友发送失败: " << ret << endl;
            return;
        }
    }
    // faccept
    void faccept(int clientfd)
    {
        string fromusername;
        cout << "请输入对方账号: ";
        input(fromusername);
        json js;
        js["MsgType"] = MsgType::FRIEND_ACC_MSG;
        js["fromusername"] = fromusername;
        js["id"] = currentuser.getid();
        js["email"] = currentuser.getemail();
        js["phone"] = currentuser.getphone();
        js["username"] = currentuser.getusername();
        string jsonmsg = js.dump();
        cout<<jsonmsg<<endl;
        int ret = send(clientfd, jsonmsg.c_str(), jsonmsg.size(), 0);
        if (ret < jsonmsg.size())
        {
            cerr << "同意好友发送失败: " << ret << endl;
            return;
        }
    }
    // frefuse
    void frefuse(int clientfd)
    {
        string fromusername;
        cout << "请输入对方账号: ";
        input(fromusername);
        json js;
        js["MsgType"] = MsgType::FRIEND_UNACC_MSG;
        js["fromusername"] = fromusername;
        js["username"] = currentuser.getusername();
        string jsonmsg = js.dump();
        int ret = send(clientfd, jsonmsg.c_str(), jsonmsg.size(), 0);
        if (ret < jsonmsg.size())
        {
            cerr << "拒绝好友发送失败: " << ret << endl;
            return;
        }
    }
    // fdelete
    void fdelete(int clientfd)
    {
        string friendusername;
        cout << "请输入对方账号: ";
        input(friendusername);
        {
            lock_guard<mutex> guard(mlock);
            auto it = find_if(friendlist.begin(), friendlist.end(), [&](User &u)
                          {if(u.getusername() == friendusername)return true; });
            if(it != friendlist.end())
                friendlist.erase(it);
            else{
                cout<<"此好友不存在"<<endl;
                return;
            }
        }
        json js;
        js["MsgType"] = MsgType::FRIEND_DEL_MSG;
        js["friendusername"] = friendusername;
        js["username"] = currentuser.getusername();
        string jsonmsg = js.dump();
        int ret = send(clientfd, jsonmsg.c_str(), jsonmsg.size(), 0);
        if (ret < jsonmsg.size())
        {
            cerr << "删除好友发送失败: " << ret << endl;
            return;
        }
    }
    // fview
    void fview(int clientfd = -1)
    {
        lock_guard<mutex> guard(mlock);
        cout << "=====================friend list=======================" << endl;
        cout << "count: " << friendlist.size() << endl;
        if (!friendlist.empty())
        {
            for (auto &i : friendlist)
            {
                cout << "friendid: " << i.getid() << "\tname: " << i.getname()<< "\tusername: " << i.getusername() << "\temail: " << i.getemail()<< "\tphone: " << i.getphone()<< "\tstate: " << i.getstate() << endl;
            }
        }
    }
    // gcreate
    void gcreate(int clientfd)
    {
        string groupname;
        string desc;
        cout << "请输入组名：";
        input(groupname);
        cout << "请输入群介绍：" << endl;
        input(desc);
        cout << groupname << "  " << desc << endl;
        json js;
        js["MsgType"] = MsgType::GROUP_CREATE_MSG;
        js["username"] = currentuser.getusername();
        js["groupname"] = groupname;
        js["desc"] = desc;
        string jsonmsg = js.dump();
        int ret = send(clientfd, jsonmsg.c_str(), jsonmsg.size(), 0);
        if (ret < jsonmsg.size())
        {
            cerr << "创建群聊发送失败: " << ret << endl;
            return;
        }
    }
    // gadd
    void gadd(int clientfd)
    {
        int groupid;
        string message;
        cout << "请输入要加入的群id: ";
        string id;
        if(!safeid(id)){
            system("clear");
            cout<<"错误输入"<<endl;
            return;
        }
        groupid = atoi(id.c_str());
        cout << "请输入验证消息: " << endl;
        input(message);
        json js;
        js["MsgType"] = MsgType::GROUP_REQ_MSG;
        js["requsername"] = currentuser.getusername();
        js["reqname"] = currentuser.getname();
        js["message"] = message;
        js["groupid"] = groupid;
        string jsonmsg = js.dump();
        int ret = send(clientfd, jsonmsg.c_str(), jsonmsg.size(), 0);
        if (ret < jsonmsg.size())
        {
            cerr << "请求加入群聊发送失败: " << ret << endl;
            return;
        }
    }
    // grefuse
    void grefuse(int clientfd)
    {
        int groupid;
        string requsername;
        cout << "请输入要受理的群id: ";
        string id;
        if(!safeid(id)){
            system("clear");
            cout<<"错误输入"<<endl;
            return;
        }
        groupid = atoi(id.c_str());
        string role = getrole(groupid, currentuser.getid());
        if (role.empty() || role == "normal")
        {
            cerr << "非法操作" << endl;
            return;
        }
        cout << "请输入申请者的账号: ";
        input(requsername);
        json js;
        js["MsgType"] = MsgType::GROUP_REFUSE_MSG;
        js["requsername"] = requsername;
        js["groupid"] = groupid;
        string jsonmsg = js.dump();
        int ret = send(clientfd, jsonmsg.c_str(), jsonmsg.size(), 0);
        if (ret < jsonmsg.size())
        {
            cerr << "拒绝申请发送失败: " << ret << endl;
            return;
        }
    }
    // gchat
    void gchat(int clientfd)
    {
        int groupid;
        string message;
        cout << "请输入组id: ";
        string id;
        if(!safeid(id)){
            system("clear");
            cout<<"错误输入"<<endl;
            return;
        }
        groupid = atoi(id.c_str());
        
        auto it = find_if(grouplist.begin(), grouplist.end(), [&](Group &g)
                       {if((g.getid())==groupid)return true; });
        if (it == grouplist.end())
        {
            cerr << "未加入群聊" << groupid << endl;
            return;
        }
        cout << "消息: " << endl;
        input(message);
        json js;
        js["MsgType"] = MsgType::GROUP_CHAT_MSG;
        js["fromusername"] = currentuser.getusername();
        js["fromname"] = currentuser.getname();
        js["groupid"] = groupid;
        js["message"] = message;
        string jsonmsg = js.dump();
        int ret = send(clientfd, jsonmsg.c_str(), jsonmsg.size(), 0);
        if (ret < jsonmsg.size())
        {
            cerr << "群聊消息发送失败: " << ret << endl;
            return;
        }
    }
    // gaccept
    void gaccept(int clientfd)
    {
        int groupid;
        string requsername;
        cout << "请输入要受理的组id: ";
        string id;
        if(!safeid(id)){
            system("clear");
            cout<<"错误输入"<<endl;
            return;
        }
        groupid = atoi(id.c_str());
        string role = getrole(groupid, currentuser.getid());
        if (role.empty() || role == "normal")
        {
            cerr << "非法操作" << endl;
            return;
        }
        cout << "请输入申请者的账号: ";
        input(requsername);
        json js;
        js["MsgType"] = MsgType::GROUP_ACC_MSG;
        js["requsername"] = requsername;
        js["groupid"] = groupid;
        string jsonmsg = js.dump();
        int ret = send(clientfd, jsonmsg.c_str(), jsonmsg.size(), 0);
        if (ret < jsonmsg.size())
        {
            cerr << "同意申请发送失败: " << ret << endl;
            return;
        }
    }
    // gquit
    void gquit(int clientfd)
    {
        int groupid;
        string username = currentuser.getusername();
        cout << "请输入组id: ";
        string id;
        if(!safeid(id)){
            system("clear");
            cout<<"错误输入"<<endl;
            return;
        }
        groupid = atoi(id.c_str());
        auto it = find_if(grouplist.begin(), grouplist.end(), [&](Group &g)
                       {if((g.getid())==groupid)return true; });
        if (it == grouplist.end())
        {
            cerr << "未加入群聊" << groupid << endl;
            return;
        }
        auto &members = it->getmembers();
        auto it2 = find_if(members.begin(), members.end(), [&](GroupMember &member)
                           {if(member.getusername() == username)return true; });
        if (it2->getrole() != "normal")
        {
            cout << it2->getusername() << it2->getname() << ": " << it2->getrole() << endl;
            cout << "管理员无法退出群聊" << endl;
            return;
        }
        grouplist.erase(it);

        json js;
        js["MsgType"] = MsgType::GROUP_REMOVE_MSG;
        js["name"] = currentuser.getname();
        js["username"] = username;
        js["groupid"] = groupid;
        string jsonmsg = js.dump();
        int ret = send(clientfd, jsonmsg.c_str(), jsonmsg.size(), 0);
        if (ret < jsonmsg.size())
        {
            cerr << "退出消息发送失败: " << ret << endl;
            return;
        }
    }
    // exit
    void exit(int clientfd)
    {
        close(clientfd);
        exit(-1);
    }
    
    void drop(int clientfd){
        json js;
        js["MsgType"] = MsgType::DROP_MSG;
        js["username"] = currentuser.getusername();
        string jsonmsg = js.dump();
        send(clientfd, jsonmsg.c_str(), jsonmsg.size(), 0);
        status = false;
        system("clear");
    }
    using FUNCTION_TYPE = function<void(int)>;
    // 命令信息映射表
    unordered_map<string, string> commandmap = {
        {"help", "显示当前所有支持的命令"},
        {"chat", "一对一聊天"},
        {"fadd", "添加好友"},
        {"faccept", "同意好友申请"},
        {"frefuse", "拒绝好友申请"},
        {"fdelete", "删除好友"},
        {"fview", "查看好友列表"},
        {"gcreate", "创建群聊"},
        {"gadd", "加入群聊申请"},
        {"grefuse", "拒绝加入申请"},
        {"gchat", "群聊"},
        {"gaccept", "同意群聊申请"},
        {"gquit", "退出群聊"},
        {"exit", "退出程序"},
        {"drop", "退出登录"}
        };
    // 命令功能映射表
    unordered_map<string, FUNCTION_TYPE> funcmap = {
        {"help",bind(&Menu::help,this,_1)},
        {"chat",bind(&Menu::chat,this,_1)},
        {"fadd",bind(&Menu::fadd,this,_1)},
        {"faccept",bind(&Menu::faccept,this,_1)},
        {"frefuse",bind(&Menu::frefuse,this,_1)},
        {"fdelete",bind(&Menu::fdelete,this,_1)},
        {"fview",bind(&Menu::fview,this,_1)},
        {"gcreate",bind(&Menu::gcreate,this,_1)},
        {"gadd",bind(&Menu::gadd,this,_1)},
        {"grefuse",bind(&Menu::grefuse,this,_1)},
        {"gchat",bind(&Menu::gchat,this,_1)},
        {"gaccept",bind(&Menu::gaccept,this,_1)},
        {"gquit",bind(&Menu::gquit,this,_1)},
        {"exit",bind(&Menu::exit,this,_1)},
        {"drop",bind(&Menu::drop,this,_1)}
    };

public:
    Menu() {}

    // 聊天页面程序
    void ChatMenu()
    {
        string cmd;
        while(status){
            cout<<"按下任意键继续";
            getchar();
            system("clear");
            showCurrentUserInfo();
            {
                lock_guard<mutex> guard(mlock);
                cout << "============================mian menu=============================" << endl;
                for (const auto &it : commandmap)
                {
                    cout << "command: " << it.first << "\t\tinfo: " << it.second << endl;
                }
                cout << "---------------------------以下是新消息----------------------------" << endl;
            }
            cin>>cmd;
            cin.ignore();
            auto it = funcmap.find(cmd);
            if(it == funcmap.end()){
                cout<<"未知命令"<<endl;
                continue;
            }
            it->second(clifd);
        }
        
        
    }

};


/*
    带空格：
        groupname message desc
    不带空格
        username friendname password state role
*/


// 处理客户端异常退出
void quit(int n)
{
    close(clifd);
    exit(-1);
}

int main(int argc, char **argv)
{
    if (argc < 3)
    {
        cerr << "command invalid! example: ./ChatClient 127.0.0.1 6000" << endl;
        return -1;
    }
    clifd = socket(AF_INET, SOCK_STREAM, 0);
    if (clifd == -1)
    {
        cerr << "Client Socket Created fail" << endl;
        return -1;
    }
    // 处理ctrl+c信号的中断
    signal(SIGINT, quit);
    // 处理ctrl+/造成的退出
    signal(SIGQUIT, quit);
    // 处理ctrl+z造成的退出
    signal(SIGTSTP, quit);
    sockaddr_in server_addr = {0};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(argv[1]);
    server_addr.sin_port = htons(atoi(argv[2]));
    if (connect(clifd, (sockaddr *)&server_addr, sizeof(server_addr)) == -1)
    {
        close(clifd);
        cerr << "Connect Server Failed" << endl;
        return -1;
    }
    //初始化信号量为0，用于实现轻量的同步，用于控制读写线程的正确进行
    sem_init(&rwcond,0,0);
    // 启动服务器接收信息线程
    thread task(recvTaskHandler, clifd);
    task.detach();
    while (true)
    {
        status = false;
        cout << "==================" << endl;
        cout << "1. login" << endl;
        cout << "2. regist" << endl;
        cout << "3. quit" << endl;
        cout << "==================" << endl;
        cout << ":(please input a number) ";
        int choice = 0;
        cin >> choice;
        cin.ignore();
        switch (choice)
        {
        case 1:
        {
            /*登录*/
            string password;
            string username;
            system("clear");
            // 使用getline获取字符串，避免读取到空格而结束
            cout << "请输入账号: ";
            input(username);

            cout << "请输入密码: ";
            input(password);

            if (check(username) || check(password))
            {
                system("clear");
                cout << "违法输入" << endl;
                continue;
            }
            json js;
            js["MsgType"] = MsgType::LOGIN_MSG;
            js["username"] = username;
            js["password"] = password;
            string jsonmsg = js.dump();
            cout<<jsonmsg<<endl;
            int ret = send(clifd, jsonmsg.c_str(), jsonmsg.size(), 0);
            if (ret == -1)
            {
                cerr << "send error" << endl;
                continue;
            }
            currentuser.setusername(username);
            currentuser.setpassword(password);
            //控制读写线程
            sem_wait(&rwcond);
            if(status){
                //登录成功
                Menu m;
                m.ChatMenu();
            }
            continue;    
        }
        case 2:
        {
            /*注册*/
            string name;
            string username;
            string password;
            string phone;
            string email;
            system("clear");
            cout << "请输入用户名: ";
            input(name);
            cout << "请输入账号: ";
            input(username);
            cout << "请输入密码: ";
            input(password);
            cout << "请输入邮箱: ";
            input(email);
            cout << "请绑定手机号: ";
            input(phone);
            if (phone.size() > 11 || check(email) ||check(name)||check(username) || check(password))
            {
                system("clear");
                cout << "违法输入" << endl;
                continue;
            }
            json js;
            js["MsgType"] = MsgType::REGIST_MSG;
            js["name"] = name;
            js["username"] = username;
            js["password"] = password;
            js["email"] = email;
            js["phone"] = phone;
            string jsonmsg = js.dump();
            int ret = send(clifd, jsonmsg.c_str(), jsonmsg.size(), 0);
            if (ret == -1)
            {
                cerr << "send error" << endl;
                continue;
            }
            currentuser.setname(name);
            currentuser.setusername(username);
            currentuser.setpassword(password);
            currentuser.setemail(email);
            currentuser.setphone(phone);
            currentuser.setstate("online");
            sem_wait(&rwcond);
            if(status){
                //注册成功
                Menu m;
                m.ChatMenu();
            }
            continue;
        }
        case 3:
            /*退出*/
            close(clifd);
            return 0;
        default:
            return 0;
        }

        break;
    }
    return 0;
}
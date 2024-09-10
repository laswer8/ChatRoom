#ifndef FRIEND_H
#define FRIEND_H

#include"./db/friend.hpp"
#include "./db/user.hpp"
#include"connection.hpp"
#include"./HeadFile.h"

class FriendHandler{
public:

    //Friend表：检测是否已有req->id与req->friendid,有返回true
    bool check(Friend& f){
        string username = f.getusername();
        string friendname = f.getfriendname();
        auto cache = DatabaseCache::GetInstance();
        string primarykey = cache->GeneratePrimaryKey("Chat","Friend",username+":"+friendname);
        auto res = cache->cachefind("Friend",primarykey);
        if(res == nullptr || res->str.empty()){
            string sql = "select true from Friend where username in('"+username+"','"+friendname+"') and friendname in('"+friendname+"','"+username+"')";
            auto ret = cache->MySQLquery("Chat",sql);
            if(ret == nullptr || ret->res ==0){
                return false;
            }
            string value = "{\"username\":\""+username+"\",\"friendname\":\""+friendname+"\"}";
            string time = cache->RandomNum(3600,36000);
            cache->cacheadd("Friend",primarykey,value,time);
        }
        //此处不能使用布隆判断是否存在，因为布隆不会删除以及删除的键
        return true;
    }

    //Friend表：删除req->id与req->friendid对应的记录
    bool removeFriend(Friend& f){
        //friend表中，两个用户之间的好友关系是双向的，因此删除redis缓存也要一次删除两个
        string username = f.getusername();
        string friendname = f.getfriendname();
        auto cache = DatabaseCache::GetInstance();
        string primarykey[2] = {username+":"+friendname,friendname+":"+username};
        string key1 = cache->GeneratePrimaryKey("Chat","Friend",primarykey[0]);
        string key2 = cache->GeneratePrimaryKey("Chat","Friend",primarykey[1]);
        cache->cacheremove(key1);
        cache->cacheremove(key2);
        string sql = "delete from Friend where username in('"+username+"','"+friendname+"') and friendname in('"+friendname + "','"+username+"')";
        auto ret = cache->MySQLquery("Chat",sql);
        if(ret == nullptr)
        {   
            return false;
        }
        cache->cacheremove(key1);
        cache->cacheremove(key2);
        return true;
    }

    //请求添加好友
    bool requst(FriendReq& req){
        string username = req.getusername();
        string fromname = req.getfromname();
        string jsonmsg = req.getjsonmsg();
        auto cache = DatabaseCache::GetInstance();
        //不需要检测是否存在申请，因为存在申请会导致插入失败
        //将添加好友的消息添加到FriendREQ表中，有req->id自身的id,req->friendid请求添加的id,req->jsonmsg json消息
        string sql = "insert into FriendREQ values('"+username+"','"+fromname+"','"+jsonmsg+"')";
        auto ret = cache->MySQLquery("Chat",sql);
        if(ret == nullptr)
        {
            return false;
        }
        //请求一类一般不能添加布隆过滤器中，因为布隆过滤器不能删除键，一旦删除表中请求就会造成数据不一致
        string primarykey = cache->GeneratePrimaryKey("Chat","FriendREQ",username+":"+fromname);
        cache->cacheadd("FriendREQ",primarykey,jsonmsg,cache->RandomNum(3600,36000));
        return true;
    }

    //Friend表：获取好友列表
    vector<User> getfriend(Friend& f){
        //查询用户id的好友列表，好友可能为0
        string username = f.getusername();
        auto cache = DatabaseCache::GetInstance();
        string sql = "select User.id ,User.name,User.username,User.email,User.phone,User.state FROM `Friend` LEFT JOIN  `User` ON Friend.friendname = User.username where Friend.username = '"+username+"'";
        auto ret = cache->MySQLquery("Chat",sql);
        if(ret == nullptr){
            return vector<User>();
        }
        vector<User> users;
        User user;
        int count = ret->res * ret->FieldsNum;
        const vector<string>& vec = ret->str_vec;
        for(int i = 0;i<count;i += ret->FieldsNum){
            user.setid(atoi(vec[i].c_str()));
            user.setname(vec[i+1]);
            user.setusername(vec[i+2]);
            user.setemail(vec[i+3]);
            user.setphone(vec[i+4]);
            user.setstate(vec[i+5]);
            users.emplace_back(user);
        }
        return users;
    }

    //获取好友申请消息
    vector<OfflineMsg> getrequest(FriendReq& req){
        auto cache = DatabaseCache::GetInstance();
        string username = req.getusername();
        string sql = "select message from FriendREQ where username = '"+username+"'";
        auto ret = cache->MySQLquery("Chat",sql);
        if(ret == nullptr || ret->res == 0){
            return vector<OfflineMsg>();
        }
        vector<OfflineMsg> res;
        OfflineMsg m;
        for(const auto& s:ret->str_vec){
            m.SetJsonMsg(s);
            res.emplace_back(m);
        }
        return res;
    }

    //Friend表：同意好友申请
    bool accept(Friend& req){
        //在Friend表插入req->id与req->friendid 、req->friendid与req->id
        string username = req.getusername();
        string friendname = req.getfriendname();
        auto cache = DatabaseCache::GetInstance();
        //检测是否存在申请
        string sql = "select username from FriendREQ where username = '"+username+"' and fromname = '"+friendname+"'";
        auto ret = cache->MySQLquery("Chat",sql);
        if(ret == nullptr || ret->res == 0){
            //LOG_INFO<<"申请1错误   res: "<<ret->res;
            return false;
        }
        //LOG_INFO<<ret->res<<" "<<ret->str_vec.size();
        sql = "insert into Friend values('"+username+"','"+friendname+"'),('"+friendname+"','"+username+"')";
        string key1 =cache->GeneratePrimaryKey("Chat","Friend",username+":"+friendname);
        string key2 = cache->GeneratePrimaryKey("Chat","Friend",friendname+":"+username);
        ret = cache->MySQLquery("Chat",sql);
        if(ret == nullptr)
        {
            return false;
        }
        cache->bm_add(key1);
        cache->bm_add(key2);
        string value1 = "{\"username\":\""+username+"\",\"friendname\":\""+friendname+"\"}";
        string value2 = "{\"username\":\""+friendname+"\",\"friendname\":\""+username+"\"}";
        string time = cache->RandomNum(3600,36000);
        cache->cacheadd("Friend",key1,value1,time);
        cache->cacheadd("Friend",key2,value2,time);
        return true;
    }

    //删除请求
    bool removeREQ(FriendReq& req){
        //1. 根据req->id与req->friendid确定主键req->id:req->friendid
        //2. 删除FriendREQ表中对应的请求
        string username = req.getusername();
        string friendname = req.getfromname();
        auto cache = DatabaseCache::GetInstance();
        string primarykey =cache->GeneratePrimaryKey("Chat","FriendREQ",username+":"+friendname);
        cache->cacheremove(primarykey);
        string sql = "delete from FriendREQ where username = '"+username+"' and fromname = '"+friendname+"'";
        auto ret = cache->MySQLquery("Chat",sql);
        if(ret == nullptr){
            return false;
        }
        return true;
    }

    vector<string> queryfriendid(string username){
        auto cache = DatabaseCache::GetInstance();
        string sql = "select username from Friend where friendname = '"+username+"'";
        auto ret = cache->MySQLquery("Chat",sql);
        if(ret == nullptr || ret->res <= 0){
            return vector<string>();
        }
        return ret->str_vec;
    }

};

#endif // !FRIEND_H
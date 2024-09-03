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
        string userid = to_string(f.getmyid());
        string friendid = to_string(f.getfriendid());
        auto cache = DatabaseCache::GetInstance();
        string primarykey = cache->GeneratePrimaryKey("Chat","Friend",userid+":"+friendid);
        auto res = cache->cachefind("Friend",primarykey);
        if(res == nullptr || res->str.empty()){
            string sql = "select true from Friend where userid in("+userid+","+friendid+") and friendid in("+friendid+","+userid+")";
            auto ret = cache->MySQLquery("Chat",sql);
            if(ret == nullptr || ret->res ==0){
                return false;
            }
        }
        //此处不能使用布隆判断是否存在，因为布隆不会删除以及删除的键
        return true;
    }

    //Friend表：删除req->id与req->friendid对应的记录
    bool removeFriend(Friend& f){
        //friend表中，两个用户之间的好友关系是双向的，因此删除redis缓存也要一次删除两个
        string userid = to_string(f.getmyid());
        string friendid = to_string(f.getfriendid());
        auto cache = DatabaseCache::GetInstance();
        string primarykey[2] = {userid+":"+friendid,friendid+":"+userid};
        string key1 = cache->GeneratePrimaryKey("Chat","Friend",primarykey[0]);
        string key2 = cache->GeneratePrimaryKey("Chat","Friend",primarykey[1]);
        cache->cacheremove(key1);
        cache->cacheremove(key2);
        string sql = "delete from Friend where userid in("+userid+","+friendid+") and friendid in("+friendid + ","+userid+")";
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
        string id = to_string(req.getid());
        string fromid = to_string(req.getfromid());
        string jsonmsg = req.getjsonmsg();
        auto cache = DatabaseCache::GetInstance();
        //不需要检测是否存在申请，因为存在申请会导致插入失败
        //将添加好友的消息添加到FriendREQ表中，有req->id自身的id,req->friendid请求添加的id,req->jsonmsg json消息
        string sql = "insert into FriendREQ values("+id+","+fromid+",'"+jsonmsg+"')";
        auto ret = cache->MySQLquery("Chat",sql);
        if(ret == nullptr)
        {
            return false;
        }
        //请求一类一般不能添加布隆过滤器中，因为布隆过滤器不能删除键，一旦删除表中请求就会造成数据不一致
        string primarykey = cache->GeneratePrimaryKey("Chat","FriendREQ",id+":"+fromid);
        cache->cacheadd("FriendREQ",primarykey,jsonmsg,cache->RandomNum(3600,36000));
        return true;
    }

    //Friend表：获取好友列表
    vector<User> getfriend(Friend& f){
        //查询用户id的好友列表，好友可能为0
        string id = to_string(f.getmyid());
        auto cache = DatabaseCache::GetInstance();
        string sql = "select Friend.friendid ,User.name,User.state FROM `Friend` LEFT JOIN  `User` ON Friend.friendid = User.id where Friend.userid = "+id;
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
            user.setstate(vec[i+2]);
            users.emplace_back(user);
        }
        return users;
    }

    //获取好友申请消息
    vector<OfflineMsg> getrequest(FriendReq& req){
        auto cache = DatabaseCache::GetInstance();
        string id = to_string(req.getid());
        string sql = "select message from FriendREQ where id = "+id;
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
        string userid = to_string(req.getmyid());
        string friendid = to_string(req.getfriendid());
        auto cache = DatabaseCache::GetInstance();
        //检测是否存在申请
        string sql = "select id from FriendREQ where id = "+userid+" and fromid = "+friendid;
        auto ret = cache->MySQLquery("Chat",sql);
        if(ret == nullptr || ret->res == 0){
            //LOG_INFO<<"申请1错误   res: "<<ret->res;
            return false;
        }
        //LOG_INFO<<ret->res<<" "<<ret->str_vec.size();
        sql = "insert into Friend values("+userid+","+friendid+"),("+friendid+","+userid+")";
        string key1 =cache->GeneratePrimaryKey("Chat","Friend",userid+":"+friendid);
        string key2 = cache->GeneratePrimaryKey("Chat","Friend",friendid+":"+userid);
        ret = cache->MySQLquery("Chat",sql);
        if(ret == nullptr)
        {
            return false;
        }
        cache->bm_add(key1);
        cache->bm_add(key2);
        string value1 = "{\"userid\":"+userid+",\"friendid\":"+friendid+"}";
        string value2 = "{\"userid\":"+friendid+",\"friendid\":"+userid+"}";
        string time = cache->RandomNum(3600,36000);
        cache->cacheadd("Friend",key1,value1,time);
        cache->cacheadd("Friend",key2,value2,time);
        return true;
    }

    //删除请求
    bool removeREQ(FriendReq& req){
        //1. 根据req->id与req->friendid确定主键req->id:req->friendid
        //2. 删除FriendREQ表中对应的请求
        string id = to_string(req.getid());
        string friendid = to_string(req.getfromid());
        auto cache = DatabaseCache::GetInstance();
        string primarykey =cache->GeneratePrimaryKey("Chat","FriendREQ",id+":"+friendid);
        cache->cacheremove(primarykey);
        string sql = "delete from FriendREQ where id = "+id+" and fromid = "+friendid;
        auto ret = cache->MySQLquery("Chat",sql);
        if(ret == nullptr){
            return false;
        }
        return true;
    }



};

#endif // !FRIEND_H
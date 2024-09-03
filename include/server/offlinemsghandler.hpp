#ifndef OFFLINE_MSG_H
#define OFFLINE_MSG_H

#include "HeadFile.h"
#include "./db/offlinemsg.hpp"
#include "connection.hpp"

class OfflineMsgHandler{
public:
    //添加用户离线消息，1.修改表，2.添加布隆缓冲器 3.添加redis缓存
    bool insert(OfflineMsg& msg){
        string uid = to_string(msg.GetId());
        string jsmsg = msg.GetJsonMsg();
        string sql = "insert into OfflineMessage values("+uid+",\'"+jsmsg+"\')";
        auto cache = DatabaseCache::GetInstance();
        auto ret = cache->MySQLquery("Chat",sql);
        if(ret == nullptr){
            LOG_INFO<<"insert 执行失败: "<<sql;
            return false;
        }
        string primarykey = cache->GeneratePrimaryKey("Chat","OfflineMessage",uid);
        //更新布隆缓存器
        cache->bm_add(primarykey);
        string time = cache->RandomNum(3600,36000);
        //添加对应缓存键
        cache->cacheadd("OfflineMessage",primarykey,jsmsg,time);
        return true;
    }

    //删除用户离线消息
    bool remove(OfflineMsg& msg){
        auto cache = DatabaseCache::GetInstance();
        string id = to_string(msg.GetId());
        string primarykey = cache->GeneratePrimaryKey("Chat","OfflineMessage",id);
        cache->cacheremove(primarykey);
        string sql = "DELETE FROM OfflineMessage WHERE userid = "+ id;
        auto ret = cache->MySQLquery("Chat",sql);
        if(ret == nullptr)
            return false;
        cache->cacheremove(primarykey);
        return true;
    }

    //返回用户的离线消息
    vector<OfflineMsg> query(OfflineMsg& msg){
        auto cache = DatabaseCache::GetInstance();
        string id = to_string(msg.GetId());
        string primarykey = cache->GeneratePrimaryKey("Chat","OfflineMessage",id);
        //判断是否存在消息
        auto result = cache->cachefind("OfflineMessage",primarykey);
        if(result != nullptr){
            //LOG_INFO<<"query nullptr";
            vector<OfflineMsg> res;
            OfflineMsg m;
            for(const auto& s:*result->vec){
                m.SetJsonMsg(s);
                res.emplace_back(m);
            }
            return res;
        }
        //redis中没有缓存，前往MySQL查找
        string sql = "select message from OfflineMessage where userid = "+id;
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
        string value;
        //储存到redis中去
        for(auto s:ret->str_vec){
            value +=s;
            value +=" ";
        }
        cache->cacheadd("OfflineMessage",primarykey,value,cache->RandomNum(3600,36000));
        return res;
    }
};

#endif // !OFFLINE_MSG_H
#ifndef GROUPHANDLER_H
#define GROUPHANDLER_H

#include"HeadFile.h"
#include"connection.hpp"
#include"db/group.hpp"

class GroupHandler{
public:
    //创建群组 groupname desc userid
    bool create(Group& group){
        auto cache = DatabaseCache::GetInstance();
        string creatorid = to_string(group.getcreatorid());
        string sql = "insert into AllGroup(groupname,groupdesc,creatorid) values('"+group.getname()+"','"+group.getdesc()+"',"+creatorid+")";
        auto ret = cache->MySQLquery("Chat",sql);
        if(ret == nullptr){
            return false;
        }
        int id = mysql_insert_id(ret->sock);
        group.setid(id);
        string primarykey = cache->GeneratePrimaryKey("Chat","AllGroup",to_string(id));
        string value = "{\"groupname\":\""+group.getname()+"\",\"groupdesc\":\""+group.getdesc()+"\",\"groupid\":"+to_string(id)+",\"creatorid\":"+creatorid+"}";
        cache->bm_add(primarykey);
        cache->cacheadd("AllGroup",primarykey,value,cache->RandomNum(3600,36000));
        sql = "insert into GroupUser values("+to_string(id)+","+creatorid+",'creator')";
        primarykey =cache->GeneratePrimaryKey("Chat","GroupUser",to_string(id)+":"+creatorid);
        cache->MySQLquery("Chat",sql);
        value = "{\"groupid\":"+to_string(id)+",\"userid\":"+creatorid+",\"grouprole\":\"creator\"}";
        cache->bm_add(primarykey);
        cache->cacheadd("GroupUser",primarykey,value,cache->RandomNum(3600,36000));
        return true;
    }

    //申请加入群组 groupid userid message
    bool request(int userid,int groupid,string msg){
        auto cache = DatabaseCache::GetInstance();
        string sql;
        //检查是否存在该群
        auto res = cache->cachefind("AllGroup",cache->GeneratePrimaryKey("Chat","AllGroup",to_string(groupid)));
        if(res == nullptr){
            sql = "select true from AllGroup where groupid = "+to_string(groupid);
            auto ret = cache->MySQLquery("Chat",sql);
            if(ret == nullptr || ret->res==0){
                return false;
            }
        }
        //请求一类一般不能添加布隆过滤器中，因为布隆过滤器不能删除键，一旦删除表中请求就会造成数据不一致
        sql = "insert into GroupREQ values("+to_string(groupid)+","+to_string(userid)+",'"+msg+"')";
        auto ret = cache->MySQLquery("Chat",sql);
        if(ret == nullptr){
            return false;
        }
        string primarykey = cache->GeneratePrimaryKey("Chat","GroupREQ",to_string(groupid)+":"+to_string(userid));
        string value = "{\"groupid\":"+to_string(groupid)+",\"reqid\":"+to_string(userid)+",\"message\":\""+msg+"\"}";
        cache->cacheadd("GroupREQ",primarykey,value,cache->RandomNum(3600,36000));
        return true;
    }

    //同意加入群组申请
    bool accrequest(int groupid,int reqid){
        auto cache = DatabaseCache::GetInstance();
        string gid = to_string(groupid);
        string uid = to_string(reqid);
        string primarykey = cache->GeneratePrimaryKey("Chat","GroupREQ", gid+":"+uid);
        string sql;
        //1. 检查是否有申请
        auto redisret = cache->cachefind("GroupREQ",primarykey);
        if(redisret == nullptr || redisret->str.empty()){
            sql = "select 1 from GroupREQ where groupid = "+gid+" and reqid = "+uid;
            auto ret = cache->MySQLquery("Chat",sql);
            if(ret == nullptr || ret->res == 0)
            {
                LOG_INFO<<"不存在申请";
                return false;
            }
        }

        //2.  添加进表
        sql = "insert into `GroupUser` VALUES("+gid+","+uid+",'normal')";
        auto res = cache->MySQLquery("Chat",sql);
        if(res == nullptr){
            LOG_INFO<<"添加成员错误";
            return false;
        }
        primarykey = cache->GeneratePrimaryKey("Chat","GroupUser",gid+":"+uid);
        string value = "{\"groupid\":"+gid+",\"userid\":"+uid+",\"grouprole\":\"normal\"}";
        cache->cacheadd("GroupUser",primarykey,value,cache->RandomNum(3600,36000));
        
        //3. 删除申请
        primarykey = cache->GeneratePrimaryKey("Chat","GroupREQ",gid+":"+uid);
        cache->cacheremove(primarykey);
        sql = "delete from GroupREQ where groupid = "+gid+" and reqid = "+uid;
        cache->MySQLquery("Chat",sql);
        cache->cacheremove(primarykey);
        return true;
    }

    //删除加入群组请求
    bool deleterequest(int groupid,int reqid){
        auto cache = DatabaseCache::GetInstance();
        string gid = to_string(groupid);
        string uid = to_string(reqid);
        string primarykey = cache->GeneratePrimaryKey("Chat","GroupREQ",gid+":"+uid);
        cache->cacheremove(primarykey);
        string sql = "delete from GroupREQ where groupid = "+gid+" and reqid = "+uid;
        auto ret = cache->MySQLquery("Chat",sql);
        if(ret == nullptr){
            return false;
        }
        cache->cacheremove(primarykey);
        return true;
    }

    //切换权限
    bool shiftrole(int userid ,int groupid,string role){
        auto cache = DatabaseCache::GetInstance();
        string gid = to_string(groupid);
        string uid = to_string(userid);
        string primarykey = cache->GeneratePrimaryKey("Chat","GroupUser",gid+":"+uid);
        cache->cacheremove(primarykey);
        string sql = "UPDATE `GroupUser` set grouprole = '"+role+"' where groupid = "+gid+" and userid = "+uid;
        auto ret = cache->MySQLquery("Chat",sql);
        if(ret == nullptr){
            return false;
        }
        cache->cacheremove(primarykey);
        return true;
    }

    //退出群组
    bool quitgroup(int userid,int groupid){
        auto cache = DatabaseCache::GetInstance();
         string gid = to_string(groupid);
        string uid = to_string(userid);
        string primarykey = cache->GeneratePrimaryKey("Chat","GroupUser",gid+":"+uid);
        cache->cacheremove(primarykey);
        string sql = "delete from GroupUser where groupid = "+gid+" and userid = "+uid+" and grouprole = 'normal'";
        auto ret = cache->MySQLquery("Chat",sql);
        if(ret == nullptr || mysql_affected_rows(ret->sock) == 0){
            return false;
        }
        cache->cacheremove(primarykey);
        return true;
    }

    //查询群组用户id列表
    vector<string> queryGroupMembers(int groupid,int userid){
        auto cache = DatabaseCache::GetInstance();
        string sql = "select userid from GroupUser where groupid = "+to_string(groupid)+" and userid != "+to_string(userid);
        auto ret = cache->MySQLquery("Chat",sql);
        if(ret == nullptr){
            return vector<string>();
        }
        return ret->str_vec;
    }

    vector<string> queryGroupCreator(int groupid){
        auto cache = DatabaseCache::GetInstance();
        string sql = "select userid from GroupUser where groupid = "+to_string(groupid)+" and grouprole = 'creator'";
        auto ret = cache->MySQLquery("Chat",sql);
        if(ret == nullptr){
            return vector<string>();
        }
        return ret->str_vec;
    }

    //查询用户所在群组的信息
    vector<Group> queryGroup(int userid){
        auto cache = DatabaseCache::GetInstance();
        string sql =    "select `table1`.groupid , `table1`.groupname ,`table1`.groupdesc ,`User`.id as userid ,`User`.name as username ,`GroupUser`.grouprole as role, User.state as state \
                        FROM `GroupUser` \
                        left JOIN `User` \
                        ON `GroupUser`.userid = `User`.id \
                        INNER JOIN ( \
                            select `AllGroup`.groupid as groupid,`AllGroup`.groupname as groupname,`AllGroup`.groupdesc as groupdesc \
                            FROM `GroupUser` INNER JOIN `AllGroup` \
                            ON `GroupUser`.groupid = `AllGroup`.groupid \
                            WHERE `GroupUser`.userid = "+to_string(userid)+" ) `table1` \
                        ON `GroupUser`.groupid = `table1`.groupid";
        auto ret = cache->MySQLquery("Chat",sql);
        if(ret == nullptr){
            return vector<Group>();
        }
        auto retvec = ret->str_vec;
        vector<Group> res;
        unordered_map<int,Group> m;
        int count = ret->res*ret->FieldsNum;
        for(int i= 0;i<count;i += ret->FieldsNum){
            Group t_g;
            int id =atoi(retvec[i].c_str());
            t_g.setid(id);
            t_g.setname(retvec[i+1]);
            t_g.setdesc(retvec[i+2]);
            GroupMember member;
            member.setid(atoi(retvec[i+3].c_str()));
            member.setname(retvec[i+4]);
            member.setrole(retvec[i+5]);
            member.setstate(retvec[i+6]);
            //LOG_INFO<<retvec[i]<<" "<<retvec[i+1]<<" "<<retvec[i+2]<<" "<<retvec[i+3]<<" "<<retvec[i+4]<<" "<<retvec[i+5]<<" "<<retvec[i+6];
            if(m.count(id) == 0){
                m[id] = t_g;
            }
            m[id].getmembers().emplace_back(member);
        }
        for(auto s:m){
            res.emplace_back(s.second);
        }
        return res;
    }
    
    //浏览指定群组信息
    vector<string> likequery(const string& key){
        auto cache = DatabaseCache::GetInstance();
        string like = "%"+key+"%";
        string sql = "SELECT `AllGroup`.groupid , `AllGroup`.groupname, `AllGroup`.groupdesc, `AllGroup`.creatorid,`User`.name from `AllGroup` INNER JOIN `User` ON `AllGroup`.creatorid = `User`.id WHERE groupname LIKE '"+like+"'";
        auto ret = cache->MySQLquery("Chat",sql);
        vector<string> res;
        if(ret == nullptr){
            return res;
        }
        string s;
        int count = ret->res * ret->FieldsNum;
        for(int i=0;i <count;i+= ret->FieldsNum){
            s = "groupid: "+ret->str_vec[i]+" groupname: "+ret->str_vec[i+1]+" desc: "+ret->str_vec[i+2]+" creatorid: "+ret->str_vec[i+3]+" name: "+ret->str_vec[i+4];
            res.emplace_back(s);
        }
        return res;
    }

    //查询指定群组的详细信息
    Group groupinfo(int groupid){
        auto cache = DatabaseCache::GetInstance();
        string sql = "SELECT `AllGroup`.groupid,`AllGroup`.groupname,`AllGroup`.groupdesc,`User`.id,`User`.name,`GroupUser`.grouprole,`User`.state FROM `AllGroup` INNER JOIN `GroupUser` ON `AllGroup`.groupid = `GroupUser`.groupid INNER JOIN `User` ON `User`.id = `GroupUser`.userid where `AllGroup`.groupid = "+to_string(groupid);
        auto ret = cache->MySQLquery("Chat",sql);
        Group res;
        if(ret == nullptr || ret->res == 0){
            return res;
        }
        int count =ret->res*ret->FieldsNum;
        res.setid(groupid);
        res.setname(ret->str_vec[1]);
        res.setdesc(ret->str_vec[2]);
        auto& vec = res.getmembers();
        GroupMember m;
        for(int i = 0; i< count; i+=ret->FieldsNum){
            m.setid(atoi(ret->str_vec[i+3].c_str()));
            m.setname(ret->str_vec[i+4]);
            m.setrole(ret->str_vec[i+5]);
            m.setstate(ret->str_vec[i+6]);
            vec.emplace_back(m);
        }
        return res;
    }
};


#endif // !GROUPHANDLER_H
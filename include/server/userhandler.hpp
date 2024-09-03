#include"HeadFile.h"
#include"connection.hpp"
#include"./db/user.hpp"

class UserHandler{
    public:
        //User表的插入操作
        bool insert(User& user){
            //因为insert是添加一个新数据，所以布隆选择器和缓存里都是没有的，因此需要手动添加保证数据一致性
            string sql = "insert into User(name,password,state) values(\'"+user.getname()+"\',\'"+user.getpwd()+"\',\'"+user.getstate()+"\')";
            auto cache = DatabaseCache::GetInstance();
            auto ret = cache->MySQLquery("Chat",sql);
            if(ret == nullptr){
                LOG_INFO<<"insert 执行失败: "<<sql;
                return false;
            }
            int id = mysql_insert_id(ret->sock);
            user.setid(id);
            string primarykey = cache->GeneratePrimaryKey("Chat","User",to_string(id));
            //更新布隆缓存器
            cache->bm_add(primarykey);
            string value = "{\"id\":"+to_string(id)+",\"name\":\""+user.getname()+"\",\"password\":\""+user.getpwd()+"\",\"state\":\"offline\"}";
            string time = cache->RandomNum(3600,36000);
            //添加对应缓存键
            cache->cacheadd("User",primarykey,value,time);
            return true;
        }

        //User表检测用户id密码是否正确
        bool check(User& user){
            auto cache = DatabaseCache::GetInstance();
            string id = to_string(user.getid());
            string primarykey = cache->GeneratePrimaryKey("Chat","User",id);
            //判断是否存在该用户，防止数据库穿透
            if(!cache->bm_exists(primarykey)){
                LOG_INFO<<id<<"不存在";
                return false;
            }
            //先在redis中查找该用户
            auto res = cache->cachefind("User",primarykey);
            if(res != nullptr && !res->str.empty()){
                //redis查找成功,primarykey value
                json js = json::parse(res->str);
                string pwd = js["password"].get<string>();
                user.setname(js["name"].get<string>());
                user.setstate(js["state"].get<string>());
                if(pwd == user.getpwd())
                {
                    return true;
                }
                LOG_INFO<<"password: "<<user.getpwd()<<"错误";
                return false;
            }
            //redis中没有缓存该键，在MySQL中查找，然后添加到redis中
            string sql = "select name,state from User where id = "+id+" and password = \'"+user.getpwd()+"\'";
            auto ret = cache->MySQLquery("Chat",sql);
            if(ret == nullptr || ret->res == 0){
                //该用户的passowrd与送进来的不一致
                LOG_INFO<<"password: "<<user.getpwd()<<"错误";
                return false;
            }
            user.setname(ret->str_vec[0]);
            user.setstate(ret->str_vec[1]);
            //MySQL查找成功，将其缓存到redis
            string value = "{\"id\":"+id+",\"name\":\""+user.getname()+"\",\"password\":\""+user.getpwd()+"\",\"state\":\"offline\"}";
            cache->cacheadd("User",primarykey,value,cache->RandomNum(3600,36000));
            return true;
        }

        //User表修改用户状态为online
        bool online(User& user){
            //修改表，延迟双删
            auto cache = DatabaseCache::GetInstance();
            string id = to_string(user.getid());
            string primarykey = cache->GeneratePrimaryKey("Chat","User",id);
            cache->cacheremove(primarykey);
            //更新表
            string sql = "update User set state = \'online\' where id = "+id;
            auto ret = cache->MySQLquery("Chat",sql);
            if(ret == nullptr)
                return false;
            //优化点：提交给线程池，异步处理提交的任务，确保不阻塞当前线程
            cache->cacheremove(primarykey);
            return true;
        }
        //User表修改用户状态为offline
        bool offline(User& user){
            //修改表，延迟双删
            auto cache = DatabaseCache::GetInstance();
            string id = to_string(user.getid());
            string primarykey = cache->GeneratePrimaryKey("Chat","User",id);
            cache->cacheremove(primarykey);
            //更新表
            string sql = "update User set state = \'offline\' where id = "+id;
            auto ret = cache->MySQLquery("Chat",sql);
            if(ret == nullptr)
                return false;
            //优化点：提交给线程池，异步处理提交的任务，确保不阻塞当前线程
            cache->cacheremove(primarykey);
            return true;
        }
        //User表重置用户状态为offline
        void ResetState(){
            //该函数被调用时，代表服务器程序退出，redis会被清空，也就不需要手动清除了
            auto cache = DatabaseCache::GetInstance();
            string sql = "update User set state = 'offline' where state = 'online'";
            cache->MySQLquery("Chat",sql);
        }

        //查询用户信息
        bool query(User& user){
            auto cache = DatabaseCache::GetInstance();
            string id = to_string(user.getid());
            string primarykey = cache->GeneratePrimaryKey("Chat","User",id);
            //判断是否存在过该用户
            if(!cache->bm_exists(primarykey)){
                LOG_INFO<<primarykey<<"NOT FOUND";
                return false;
            }
            auto ret = cache->cachefind("User",primarykey);
            if(ret != nullptr && !ret->str.empty()){
                //如果储存在redis缓存中
                json js = json::parse(ret->str);
                user.setname(js["name"].get<string>());
                user.setstate(js["state"].get<string>());
                return true;
            }
            //redis不存在，在MySQL中查询
            string sql = "select name,password,state from User where id = "+id;
            auto res = cache->MySQLquery("Chat",sql);
            if(res == nullptr || res->res <= 0){
                LOG_INFO<<primarykey<<" Mysql查询失败,不存在该用户";
                return false;
            }
            string name = res->str_vec[0];
            string password = res->str_vec[1];
            string state = res->str_vec[2];
            user.setname(name);
            user.setstate(state);
            //缓存到redis中
            string value = "{\"id\":"+id+",\"name\":\""+name+"\",\"password\":\""+password+"\",\"state\":\""+state+"\"}";
            cache->cacheadd("User",primarykey,value,cache->RandomNum(3600,36000));
            return true;

        }
};
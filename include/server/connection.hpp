#ifndef CONNPOOL_H
#define CONNPOOL_H


#include "HeadFile.h"

/*
    我们对数据库进行访问时，需要经过：Tcp三次握手、MySQL三次认证、SQL执行、MySQL关闭、TCP四次挥手
    其中只有SQL执行是真正工作的
    如果有多个用户访问数据库，会频繁重复上述过程
    因此我们需要使用连接池来管理用户的连接，进行连接复用

    我们设计一个mysql的连接队列
    每次从其中进行取连接、归还连接


    流程：
        1.  初始化
                    连接数据库 （主机IP、用户名、密码、数据库）
                    创建连接，数量 == 线程池数量
        
        2.  管理连接
                    获取连接
                            可能发生竞争，因此需要线程安全
                            使用容器存储连接
                    归还连接
        
        3.  连接池的名字 pool_name
                    区分不同的连接池
                            数据库数量大的情况——分库
                            获取连接时，指定数据库，如getDbConnect("数据库")
                        区分优先级：
                            主：写入数据、修改数据
                            从：读取数据
                            封装一个DBManager，存放dbpool获取实际连接
*/

#define MAX_CONN 10

#define MYSQL_HOST "127.0.0.1"
#define MYSQL_PORT 3306
#define MYSQL_USER "root"
#define MYSQL_PWD    "2836992987"
#define MYSQL_DEFAULT_DB "Chat"
#define MYSQL_CHARSET "utf8mb4"
#define MYSQL_TIMEOUT 60

#define REDIS_HOST "127.0.0.1"
#define REDIS_PORT 6379

#define TRYAGAIN_COUNT 3
#define BLOOMKEY "bloom"
#define BLOOMRATE "0.00001"
#define BLOOMCAPACITY "1000000"
#define SLEEPSEC 1

typedef struct  mysqlhead
{
    string host;
    size_t port;
    string database;
    string user;
    string password;
    string charset;
    unsigned int timeout;
    mysqlhead(const string& nhost, 
                            const size_t& nport,
                            const string& nuser,
                            const string& npassword,
                            const string ndatabase):host(nhost),port(nport),user(nuser),database(ndatabase),password(npassword),charset(MYSQL_CHARSET),timeout(MYSQL_TIMEOUT){}
}ConnectionHead;

typedef struct Result{
    int res;
    int  FieldsNum;
    MYSQL* sock;
    vector<string> FieldsName;
    vector<string> str_vec; 
    Result():res(0),FieldsNum(0),sock(nullptr){}
}MySQLResult;

typedef struct mysqlconn{
    shared_ptr<ConnectionHead> ConntionPtr;
    MYSQL* sock;
    MYSQL mysql;
    MYSQL_RES* mysqlres;
    int res;
    mysqlconn(){
        ConntionPtr = nullptr;
        sock = nullptr;
        mysqlres = nullptr;
        res = 0;
    }
}MysqlConnection;

class MysqlConnectionPool{
    private:
        bool started;
        int connNum;
        shared_ptr<ConnectionHead> head;
        queue<shared_ptr<MysqlConnection>> connpool;
        pthread_cond_t condition;
        pthread_mutex_t threadlock;
        static unordered_map<string,vector<string>> tables;
        static vector<string> databases;
        static mutex m;
        static shared_ptr<MysqlConnectionPool> MysqlPool;

        MysqlConnectionPool(int connectionnum,const string database = MYSQL_DEFAULT_DB, const string& host = MYSQL_HOST,const size_t& port = MYSQL_PORT,const string& user = MYSQL_USER,const string& password = MYSQL_PWD):started(false){
            connNum =connectionnum;
            pthread_mutex_init(&threadlock,nullptr);
            pthread_cond_init(&condition,nullptr);
            initMysqlConnPool(host,port,user,password,database);
            LOG_INFO<<"MySQL Connection Successful";
        }

    public:
        MysqlConnectionPool(const MysqlConnectionPool&) = delete;
        MysqlConnectionPool& operator=(const MysqlConnectionPool&)=delete;

        static unordered_map<string,vector<string>>& GetTables(){
            return tables;
        }

        static vector<string>& GetDataBases(){
            return databases;
        }

        static  shared_ptr<MysqlConnectionPool> GetInstance(){
            if(MysqlPool == nullptr){
                lock_guard<std::mutex>  guard(m);
                if(MysqlPool == nullptr){
                    MysqlPool = shared_ptr<MysqlConnectionPool>(new MysqlConnectionPool(MAX_CONN));
                }
            }
            return MysqlPool;
        }


        int initMysqlConnPool(const string& host,const size_t& port,const string& user,const string& password,const string database){
            if(host.empty() || user.empty() || password.empty() || database.empty() ){
                LOG_ERROR<<"INIT MYSQL CONNECTION POOL INFO IS NULL , INIT ERROR!";
                return -1;
            }
            head = make_shared<ConnectionHead>(host,port,user,password,database);
            return 0;
        }

        void Close(){
            started = false;
            while(!connpool.empty()){
                mysql_close(connpool.front()->sock);
                connpool.pop();
            }
            pthread_cond_destroy(&condition);
            pthread_mutex_destroy(&threadlock);
        }
        
        
        ~MysqlConnectionPool(){
            Close();
        }

        int start(){
            if(started)
                return -1;
            started = true;
            for(int i=0;i<connNum;i++){
                shared_ptr<MysqlConnection> temp = make_shared<MysqlConnection>();
                temp->ConntionPtr = head;
                if(mysql_init(&temp->mysql) == nullptr){
                    LOG_ERROR<<mysql_error(&temp->mysql);
                    Close();
                    return -1;
                }
                mysql_options(&temp->mysql,MYSQL_SET_CHARSET_NAME,head->charset.c_str());
                mysql_options(&temp->mysql,MYSQL_OPT_CONNECT_TIMEOUT,&head->timeout);
                mysql_options(&temp->mysql,MYSQL_OPT_READ_TIMEOUT,&head->timeout);

                //CLIENT_MULTI_STATEMENTS 发送多个语句给MYSQL
                temp->sock = mysql_real_connect(&temp->mysql,head->host.c_str(),head->user.c_str(),head->password.c_str(),head->database.c_str(),head->port,nullptr,CLIENT_MULTI_STATEMENTS);
                if(temp->sock == nullptr){
                    LOG_ERROR<<mysql_error(&temp->mysql);
                    Close();
                    return -1;
                }
                connpool.push(temp);
            }
            scan();
            return 0;
        }

        int scan(){
            auto conn = GetConnection();
            int ret = ExecuteSql(conn,"show databases");
            
            if(ret == -1){
                LOG_INFO<<"Get MySQL DataBases Failed";
                return -1;
            }
            auto res = ResEcho(conn);
            databases = res->str_vec;
            for(auto& i:databases){
                if(i=="information_schema"||i=="mysql" || i=="performance_schema"||i=="sys")
                    continue;

                string use = "use "+i;
                ret = ExecuteSql(conn,use.c_str());
                
                if(ret == -1 ){
                    LOG_INFO<<"Change DataBases Failed\n";
                    return -1;
                }

                ret = ExecuteSql(conn,"show tables");
                
                if(ret == -1 ){
                    LOG_INFO<<"Get MySQL Tables Failed\n";
                    return -1;
                }
                auto res = ResEcho(conn);
                tables[i] = res->str_vec;
            }
            RecycleConnection(conn);
            return 0;
        }

        void clear(){
            if(started == false){
                databases.clear();
                tables.clear();
            }
        }

        shared_ptr<MysqlConnection> GetConnection(){
            if(started == false)
                return nullptr;
            while(connpool.empty()||connNum<=0){
                pthread_cond_wait(&condition,&threadlock);
            }
            assert(!connpool.empty());
            shared_ptr<MysqlConnection> ret = nullptr;
            {
                lock_guard<mutex> guard(m);
                ret = connpool.front();
                connpool.pop();
            }
            return ret;
        }

        void RecycleConnection(shared_ptr<MysqlConnection>& conn){
            if(started == false)return;
            {
                lock_guard<mutex> guard(m);
                connpool.push(conn);
            }
            pthread_cond_signal(&condition);
        }

        int ExecuteSql(shared_ptr<MysqlConnection>& conn,const char* sql){
            if(started == false || conn == nullptr || sql == nullptr || conn->sock == nullptr){
                //LOG_INFO<<"ERROR 1";
                return -1;
            }
            if(conn->sock){
                conn->res = mysql_query(&conn->mysql,sql);
            }

            // if(conn->res != 0){
            //     //执行失败，重新连接MySQL
            //     LOG_INFO<<"执行失败,重新连接MySQL";
            //     mysql_close(&conn->mysql);
            //     mysql_init(&conn->mysql);
            //     shared_ptr<ConnectionHead> t = conn->ConntionPtr;
            //     mysql_options(&conn->mysql,MYSQL_SET_CHARSET_NAME,t->charset.c_str());
            //     mysql_options(&conn->mysql,MYSQL_OPT_CONNECT_TIMEOUT,&t->timeout);
            //     mysql_options(&conn->mysql,MYSQL_OPT_READ_TIMEOUT,&t->timeout);
            //     conn->sock = mysql_real_connect(&conn->mysql,t->host.c_str(),t->user.c_str(),t->password.c_str(),t->database.c_str(),t->port,nullptr,CLIENT_MULTI_STATEMENTS);
            //     if(conn->sock == nullptr){
            //         LOG_INFO<<mysql_error(&conn->mysql);
            //         return -1;
            //     }
            //     conn->res = mysql_query(&conn->mysql,sql);
            //     LOG_INFO<<"重新连接MySQL";
            // }

            return conn->res;
        }
        //执行事务
        int ExecuteTransaction(shared_ptr<MysqlConnection>& conn,const char* sql){
            if(started == false || conn == nullptr || sql == nullptr || conn->sock == nullptr){
                //LOG_INFO<<"ERROR 2";
                return -1;
            }
            if(conn->sock){
                mysql_autocommit(&conn->mysql,0);
                try{
                    conn->res = mysql_query(&conn->mysql,sql);
                    if(conn->res == 0){
                        mysql_commit(&conn->mysql);
                    }
                    else{
                        mysql_rollback(&conn->mysql);
                        conn->res=1;
                    }
                }catch(exception){
                    mysql_rollback(&conn->mysql);
                    conn->res=1;
                }
                mysql_autocommit(&conn->mysql,1);
            }else{
                conn->res = 1; // mysql_query执行失败返回非0
            }

            // if(conn->res != 0){
            //     //执行失败，重新连接MySQL
            //     mysql_close(&conn->mysql);
            //     mysql_init(&conn->mysql);
            //     shared_ptr<ConnectionHead> t = conn->ConntionPtr;
            //     mysql_options(&conn->mysql,MYSQL_SET_CHARSET_NAME,t->charset.c_str());
            //     mysql_options(&conn->mysql,MYSQL_OPT_CONNECT_TIMEOUT,&t->timeout);
            //     mysql_options(&conn->mysql,MYSQL_OPT_READ_TIMEOUT,&t->timeout);
            //     conn->sock = mysql_real_connect(&conn->mysql,t->host.c_str(),t->user.c_str(),t->password.c_str(),t->database.c_str(),t->port,nullptr,CLIENT_MULTI_STATEMENTS);
            //     if(conn->sock == nullptr){
            //         LOG_ERROR<<mysql_error(&conn->mysql);
            //         return -1;
            //     }
            //     mysql_autocommit(&conn->mysql,0);
            //     try{
            //         conn->res = mysql_query(&conn->mysql,sql);
            //         if(conn->res == 0){
            //             mysql_commit(&conn->mysql);
            //         }
            //         else{
            //             mysql_rollback(&conn->mysql);
            //             conn->res=1;
            //         }
            //     }catch(exception){
            //         mysql_rollback(&conn->mysql);
            //         conn->res=1;
            //     }
            //     mysql_autocommit(&conn->mysql,1);
            // }

            return conn->res;
        }

        shared_ptr<MySQLResult> ResEcho( shared_ptr<MysqlConnection>& conn){
            if(started == false || conn == nullptr)
                return nullptr;
            shared_ptr<MySQLResult> ret = make_shared<MySQLResult>();
            ret->sock = &conn->mysql;
            conn->mysqlres = mysql_store_result(&conn->mysql);
            if(conn->mysqlres == nullptr){
                if(mysql_field_count(&conn->mysql) == 0){
                    ret->res=0;
                    ret->FieldsNum=0;
                    ret->str_vec.emplace_back("SET EMPTY(0.0)");
                }else
                    LOG_ERROR<<"GET RESULT FILED";
                return ret;
            }
            MYSQL_ROW row;
            MYSQL_FIELD* field = nullptr;
            ret->FieldsNum = mysql_num_fields(conn->mysqlres);
            field = mysql_fetch_fields(conn->mysqlres);
            {
                lock_guard<mutex> guard(m);
                for(int j= 0;j<ret->FieldsNum;j++){
                    ret->FieldsName.emplace_back(field[j].name);
                }
                while(( row =  mysql_fetch_row(conn->mysqlres))){
                    ret->res++;
                    for(int i=0;i<ret->FieldsNum;i++)
                        ret->str_vec.emplace_back(row[i]);
                }
            }

            mysql_free_result(conn->mysqlres);
            return ret;
        }

        bool Started(){
            return started;
        }

        int GetInsertId(shared_ptr<MysqlConnection>& conn){
            return mysql_insert_id(conn->sock);
        }
};

shared_ptr<MysqlConnectionPool> MysqlConnectionPool::MysqlPool = nullptr;
mutex MysqlConnectionPool::m;
unordered_map<string,vector<string>> MysqlConnectionPool::tables;
vector<string> MysqlConnectionPool::databases;



typedef struct redisresult{
    int count;
    int len;
    int res;
    string str;
    shared_ptr<vector<string>> vec;
    bool empty;
    redisresult():empty(true),count(0),len(0),res(0),vec(nullptr),str("(nil)"){}
}RedisResult;

typedef struct redisconnect{
    int status;
    int port;
    string host;
    redisContext* conn;
    redisReply* result;
    redisconnect():port(REDIS_PORT),host(REDIS_HOST),conn(nullptr),result(nullptr),status(-1){}
    ~redisconnect(){
        destory();
    }
    void destory(){
        if(conn != nullptr)
            redisFree(conn);
    }

}RedisConnection;

class RedisCache{
    private:
        bool started;
        int conn_num;
        queue<shared_ptr<RedisConnection>> RedisPool;
        pthread_cond_t condition;
        pthread_mutex_t threadlock;
        static mutex _mutex;
        static shared_ptr<RedisCache> pool;
    
    
        RedisCache(const int& num):conn_num(num),started(false){
            assert(num >0);
            conn_num = num;
            pthread_cond_init(&condition,nullptr);
            pthread_mutex_init(&threadlock,nullptr);
            LOG_INFO<<"Redis Connection Successful";
        }
    public:
        RedisCache(const RedisCache&)=delete;
        RedisCache& operator=(const RedisCache&)=delete;
        ~RedisCache(){
            started = false;
            lock_guard<mutex> guard(_mutex);
            while (!RedisPool.empty())
            {
                RedisPool.pop();
            }
            pthread_cond_destroy(&condition);
            pthread_mutex_destroy(&threadlock);
        }

        static shared_ptr<RedisCache> GetInstance(){
            if(pool == nullptr){
                lock_guard<mutex> guard(_mutex);
                if(pool == nullptr){
                    pool = shared_ptr<RedisCache>(new RedisCache(MAX_CONN));
                    assert(pool != nullptr);
                }
            }
            return pool;
        }

        void start(int port = REDIS_PORT,string host = REDIS_HOST){
            if(started)
                return;
            started = true;
            assert(conn_num > 0);
            int num = conn_num;
            for(int i= 0; i< num;i++){
                shared_ptr<RedisConnection> temp = make_shared<RedisConnection>();
                if(temp == nullptr){
                    LOG_INFO<<"Redis Make Shared_Ptr error";
                    conn_num--;
                    continue;
                }
                temp->port = port;
                temp->host = host;
                temp->conn = redisConnect(host.c_str(),port);
                if(temp->conn == nullptr || temp->conn->err){
                    redisFree(temp->conn);
                    LOG_INFO<<"connection redis server error";
                    conn_num--;
                    continue;
                }
                RedisPool.emplace(temp);
            }
        }

        shared_ptr<RedisConnection> take(){
            if(started == false)return nullptr;
            while(RedisPool.empty()||conn_num<=0){
                pthread_cond_wait(&condition,&threadlock);
            }
            assert(!RedisPool.empty());
            shared_ptr<RedisConnection> ret = nullptr;
            {
                lock_guard<mutex> guard(_mutex);
                ret = RedisPool.front();
                RedisPool.pop();
                conn_num--;
            }
            return ret;
        }

        shared_ptr<RedisConnection> trytake(int space){
            if(started == false)return nullptr;
            timespec ts;
            if( clock_gettime(CLOCK_REALTIME,&ts) == -1 ){
                LOG_ERROR<<"get time failed";
                return nullptr;
            }
            ts.tv_sec += space;
            if(RedisPool.empty()||conn_num<=0)
                pthread_cond_timedwait(&condition,&threadlock,&ts);

            if(RedisPool.empty()){
                LOG_ERROR<<"try take failed";
                return nullptr;
            }
            shared_ptr<RedisConnection> ret = nullptr;
            {
                lock_guard<mutex> guard(_mutex);
                ret = RedisPool.front();
                RedisPool.pop();
                conn_num--;
            }
            return ret;
        }
       
        void recycle(shared_ptr<RedisConnection> conn){
            if(started == false)return;
            if(conn == nullptr){
                LOG_INFO<<"conn is destory!!!";
                return;
            }
            {
                lock_guard<mutex> guard(_mutex);
                RedisPool.push(conn);
                conn_num++;
            }
            pthread_cond_signal(&condition);
        }

        int size(){
            return conn_num;
        }

        int ExecuteNoSQL(shared_ptr<RedisConnection> conn,const char* nosql){
            if(started == false||conn == nullptr || nosql == nullptr){
                LOG_INFO<<nosql<<"-"<<"return -1";
                if(started == false)
                    LOG_INFO<<"state: "<<started;
                if(conn == nullptr)
                    LOG_INFO<<"conn nullptr";
                if(nosql == nullptr)
                    LOG_INFO<<"nosql is nullptr : "<<nosql;
                return -1;
            }
            if(conn->conn != nullptr && !conn->conn->err){
                conn->result = (redisReply*)redisCommand(conn->conn,nosql);
                //LOG_INFO<<conn->result->str;
                if(conn->result == nullptr){
                    //LOG_INFO<<"为空";
                    conn->status = 1;
                }else{
                    //LOG_INFO<<"执行: "<<nosql<<"-"<<conn->result->str;
                    conn->status = 0;
                    return 0;
                }
            }else{
                //LOG_INFO<<"redis连接有问题";
                redisFree(conn->conn);
                conn->status = 1;
            }

            if(conn->status != 0){
                redisFree(conn->conn);
                conn->conn = redisConnect(conn->host.c_str(),conn->port);
                if(conn->conn == nullptr || conn->conn->err){
                    redisFree(conn->conn);
                    LOG_ERROR<<"connection redis server error";
                    conn->status = 1;
                }else{
                    conn->result = (redisReply*)redisCommand(conn->conn,nosql);
                    if(conn->result == nullptr){
                        conn->status = 1;
                    }else{
                        conn->status = 0;
                    }
                }
            }
            return conn->status;
        }

        int ExecuteTransaction(shared_ptr<RedisConnection> conn,vector<const char*> nosql,string watch){
            if(started == false||conn == nullptr || nosql.empty())
                return -1;

             if(conn->conn != nullptr && !conn->conn->err){
                if(watch.size()>0){
                    watch = "WATCH " + watch; 
                    conn->result = (redisReply*)redisCommand(conn->conn,watch.c_str());
                    if(conn->result == nullptr)
                        conn->status = 1;
                    else{
                        conn->status = 0;
                        freeReplyObject(conn->result);
                    }
                }
                if(conn->status == 0){
                    conn->result = (redisReply*)redisCommand(conn->conn,"MULTI");
                    if(conn->result == nullptr)
                        conn->status = 1;
                    else{
                        freeReplyObject(conn->result);
                        for(auto& i:nosql)
                            redisAppendCommand(conn->conn,i);
                        conn->result = (redisReply*)redisCommand(conn->conn,"EXEC");
                        if(conn->result == nullptr)
                            conn->status = 1;
                        else
                            conn->status = 0;
                    }
                }
            }else{
                redisFree(conn->conn);
                conn->status = 1;
            }

            if(conn->status != 0){
                redisFree(conn->conn);
                conn->conn = redisConnect(conn->host.c_str(),conn->port);
                if(conn->conn == nullptr || conn->conn->err){
                    redisFree(conn->conn);
                    LOG_ERROR<<"connection redis server error";
                    conn->status = 1;
                }else{
                    if(watch.size()>0){
                        watch = "WATCH " + watch; 
                        conn->result = (redisReply*)redisCommand(conn->conn,watch.c_str());
                        if(conn->result == nullptr)
                            conn->status = 1;
                        else{
                            conn->status = 0;
                            freeReplyObject(conn->result);
                        }
                    }
                    if(conn->status == 0){
                        conn->result = (redisReply*)redisCommand(conn->conn,"MULTI");
                        if(conn->result == nullptr)
                            conn->status = 1;
                        else{
                            freeReplyObject(conn->result);
                            for(auto& i:nosql)
                                redisAppendCommand(conn->conn,i);
                            conn->result = (redisReply*)redisCommand(conn->conn,"EXEC");
                            if(conn->result == nullptr)
                                conn->status = 1;
                            else
                                conn->status = 0;
                        }
                    }
                }
            }
            return conn->status;
        }

        shared_ptr<RedisResult> ResEcho(shared_ptr<RedisConnection> conn){
            if(started == false||conn == nullptr)
                return nullptr;
            shared_ptr<RedisResult> ret = make_shared<RedisResult>();
            if(conn->result->type == REDIS_REPLY_STATUS || conn->result->type == REDIS_REPLY_STRING){
                ret->empty = false;
                ret->len = conn->result->len;
                ret->str = conn->result->str;
            }else if(conn->result->type == REDIS_REPLY_INTEGER){
                ret->empty = false;
                ret->res = conn->result->integer;
            }else if(conn->result->type == REDIS_REPLY_ARRAY){
                ret->empty = false;
                ret->vec = make_shared<vector<string>>();
                for(int i = 0;i<conn->result->elements;i++){
                    if(conn->result->element[i]->type == REDIS_REPLY_STRING)
                    { 
                        ret->count++;
                        ret->vec->emplace_back(conn->result->element[i]->str);
                    }
                    else if(conn->result->element[i]->type == REDIS_REPLY_ARRAY)
                    {
                        for(int j = 0;j<conn->result->element[i]->elements;j++){
                            ret->count++;
                            ret->vec->emplace_back(conn->result->element[i]->element[j]->str);
                        }
                    }
                  
                }
            }else if(conn->result->type == REDIS_REPLY_NIL){
                ret->empty = true;
            }else{
                LOG_ERROR<<"ResEcho Failed: "<<conn->result->str;
            }
            freeReplyObject(conn->result);

            return ret;
        }

        bool Started(){
            return started;
        }
};

class Blom{
    private:
        string key;
        shared_ptr<RedisCache> redis;
        shared_ptr<RedisConnection> bloom;
    public:
        Blom(string n_key=BLOOMKEY):key(n_key){
            redis = RedisCache::GetInstance();
        }
        ~Blom(){
            redis->recycle(bloom);
        }

        bool add(const string& str,shared_ptr<RedisConnection>& bloom){
            if(str.empty())
                return false;
            string nosql = "bf.add "+key +" "+ str;
            //LOG_INFO<<nosql;
            //LOG_INFO<<"redis state: "<<redis->Started();
            int ret = redis->ExecuteNoSQL(bloom,nosql.c_str());
            if(ret==0){
                auto res = redis->ResEcho(bloom);
                //LOG_INFO<<"res: "<<res->str<<"-"<<res->res;
                if(res->res == 1){
                    return true;
                }
            }
            //LOG_INFO<<"执行失败: "<<ret;
            return false;
        }

        bool check(const string& query,shared_ptr<RedisConnection>& bloom){
            if(query.empty())
                return false;
            string nosql = "bf.exists "+ key +" "+query;
            //LOG_INFO<<nosql;
            if(redis->ExecuteNoSQL(bloom,nosql.c_str())==0){
                if(redis->ResEcho(bloom)->res == 1)
                    return true;
            }
            return false;
        }
    
};

shared_ptr<RedisCache> RedisCache::pool = nullptr;
mutex RedisCache::_mutex;

typedef struct result{
    bool is_redis;
    bool is_mysql;
    string str;
    shared_ptr<vector<string>> vec;
    result():is_redis(false),is_mysql(false),vec(nullptr),str("Empty"){}
}CacheResult;

class DatabaseCache{
    private:
        shared_ptr<MysqlConnectionPool> mysql;
        shared_ptr<RedisCache> redis;
        Blom BlomSelection;
        pthread_cond_t _cond;
        random_device rd;
        unordered_map<string,string> _tabletypemap;
        static mutex _mutex;
        static shared_ptr<DatabaseCache> Cache;
    private:
 
        void init(shared_ptr<MysqlConnectionPool> mysqlpool = MysqlConnectionPool::GetInstance(),shared_ptr<RedisCache> redispool = RedisCache::GetInstance()){
            mysql = mysqlpool;
            redis = redispool;
            mysql->start();
            redis->start();

            auto mysqlconn = mysql->GetConnection();
            auto redisconn = redis->take();
            string nosql = "flushall";
            redis->ExecuteNoSQL(redisconn,nosql.c_str());
            bool need_scan = false;
            string str;
            int ret = -1;
            int count = 0;
            vector<string> databases = mysql->GetDataBases();
            for(auto database = databases.begin();database != databases.end();database++){
                //LOG_INFO<<"数据库："<<*database;
                str = "use "+ *database;
                ret = mysql->ExecuteSql(mysqlconn,str.c_str());
                if(ret == -1){
                    need_scan = true;
                    mysql->GetDataBases().erase(database);
                    continue;
                }
                /*
                    遍历数据库中的每一张表，并将其添加到布隆选择器，储存每条属性的唯一值（主键），并添加到redis中设计缓存
                */
                mt19937 gen(rd());//随机数种子
                unordered_map<string,vector<string>> tables = mysql->GetTables();
                for(auto table = tables[*database].begin();table != tables[*database].end();table++){
                    string tabletype = _tabletypemap[*table];
                    //LOG_INFO<<"表："<<*table<<"-"<<tabletype;
                    str = "select * from "+ *table;
                    ret = mysql->ExecuteSql(mysqlconn,str.c_str());
                    
                    if(ret == -1){
                        need_scan = true;
                        mysql->GetTables()[*database].erase(table);
                        continue;
                    }

                    auto res = mysql->ResEcho(mysqlconn);
                    if(res == nullptr || res->res<=0){
                        continue;
                    }
                    int sum_elements = res->res * res->FieldsNum;
                    uniform_int_distribution<> randomnumber(3600,36000);
                    for(int table_element = 0; table_element < sum_elements;table_element+=res->FieldsNum){
                        vector<string> values;
                        for(int i=table_element;i<table_element+res->FieldsNum;i++){
                            string temp = res->str_vec[i];
                            replace(temp.begin(),temp.end(),' ','#');
                            values.emplace_back(temp);
                        }
                            
                        string key;
                        if(tabletype == "unionstring" || tabletype == "doublestring")
                            key = *database+":" + *table + ":"+tabletype+":"+ values[0]+":"+values[1];
                        else
                            key = *database+":" + *table + ":"+tabletype+":"+ values[0];
                        
                        BlomSelection.add(key,redisconn);
                        string value;
                        value = "{";
                        string s="";
                        for(int ii=0;ii<res->FieldsNum;ii++){
                            s+="\""+res->FieldsName[ii]+"\":\""+values[ii]+"\"";
                            if(ii < res->FieldsNum-1)
                            s+=",";
                        }
                        value += s + "}";
                        replace(value.begin(),value.end(),' ','\x01');
                        //LOG_INFO<<key<<" "<<value;
                        if(tabletype == "hash")
                            nosql = "lpush "+key+" "+value;
                        else{
                            nosql = "SET "+key+" "+value + " EX "+to_string(randomnumber(gen));
                        }
                        //LOG_INFO<<"nosql: "<<nosql;
                        ret = redis->ExecuteNoSQL(redisconn,nosql.c_str());
                        if(ret == -1){
                            if(tabletype == "hash"){
                                LOG_INFO<<"DEL "<<key;
                                nosql = "del "+key;
                                redis->ExecuteNoSQL(redisconn,nosql.c_str());
                            }
                            continue;
                        }
                        
                        if(tabletype == "hash"){
                            auto a = redis->ResEcho(redisconn);
                            //LOG_INFO<<a->str;
                            nosql = "expire "+key+" "+to_string(randomnumber(gen));
                            //LOG_INFO<<"expire: "<<nosql;
                            ret = redis->ExecuteNoSQL(redisconn,nosql.c_str());
                            //如果设置失败，即MySQL存在的数据没能同步到redis缓存上去，进行通报
                            if(ret == -1)
                            {
                                LOG_INFO<<"DEL "<<key;
                                nosql = "del "+key;
                                redis->ExecuteNoSQL(redisconn,nosql.c_str());
                            }
                        }
                        //LOG_INFO<<"缓存成功";
                    }
                    
                }
                
            }
            if(need_scan){
                mysql->clear();
                mysql->scan();
            }
            redis->recycle(redisconn);
            mysql->RecycleConnection(mysqlconn);
            
        }
        
        DatabaseCache(){
            _tabletypemap.insert({"User","string"});
            _tabletypemap.insert({"OfflineMessage","hash"});
            _tabletypemap.insert({"AllGroup","string"});
            _tabletypemap.insert({"Friend","doublestring"});
            _tabletypemap.insert({"FriendREQ","unionstring"});
            _tabletypemap.insert({"GroupUser","unionstring"});
            _tabletypemap.insert({"GroupREQ","unionstring"});
            _tabletypemap.insert({"test_table1","string"});
            
            pthread_cond_init(&_cond,NULL);
            init();
        }
    
    public:

        DatabaseCache(const DatabaseCache&)=delete;
        DatabaseCache& operator=(const DatabaseCache&)=delete;
        ~DatabaseCache(){
            
        }
        static shared_ptr<DatabaseCache> GetInstance(){
            if(Cache == nullptr){
                lock_guard<mutex> guard(_mutex);
                if(Cache == nullptr)
                    Cache = shared_ptr<DatabaseCache>(new DatabaseCache());
            }
            return Cache;
        }

        string GeneratePrimaryKey(string database,string table,string primary){
            return database+":"+table+":"+_tabletypemap[table]+":"+primary;
        }

        bool bm_exists(string key){
            auto redisconn = redis->take();
            bool ret = BlomSelection.check(key,redisconn);
            redis->recycle(redisconn);
            return ret;
        }

        bool bm_add(string key){
            auto redisconn = redis->take();
            bool ret =  BlomSelection.add(key,redisconn);
            redis->recycle(redisconn);
            return ret;
        }

        shared_ptr<RedisResult> cachefind(string table,string key){
            auto redisconn = redis->take();
            string tabletype = _tabletypemap[table];
            string nosql;
            if(tabletype == "hash"){
                nosql = "lrange "+key+" 0 -1";
            }else{
                nosql = "GET "+ key;
            }
            int ret = redis->ExecuteNoSQL(redisconn,nosql.c_str());
            if(ret != 0){
                redis->recycle(redisconn);
                return nullptr;
            }
            auto result = redis->ResEcho(redisconn);                 //获取结果
            if(result == nullptr){                              //如果失败，回收redis连接到连接池
                redis->recycle(redisconn);
                //LOG_INFO<<"NoSQL获取执行结果失败返回nullptr";
                return nullptr;
            }
            if(!result->empty){          //如果查找成功
                //LOG_INFO<<"Redis 查找成功";                        
                if(tabletype == "hash" && result->vec->empty()){        
                    redis->recycle(redisconn);
                    return nullptr;
                }
                //表示是redis查找到的 
                redis->recycle(redisconn);
                return result;
            }
            redis->recycle(redisconn);
            return nullptr;
        }

        shared_ptr<MySQLResult> MySQLquery(string database,string query){
            auto mysqlconn = mysql->GetConnection();
            string sql = "use "+database;
            int ret = mysql->ExecuteSql(mysqlconn,sql.c_str());
            if(ret != 0){
                mysql->RecycleConnection(mysqlconn);
                return nullptr;
            }        
            mysql->ResEcho(mysqlconn);
            //LOG_INFO<<"切换数据库";
            //执行sql操作
            ret = mysql->ExecuteSql(mysqlconn,query.c_str());
            if(ret == 0){
                auto res = mysql->ResEcho(mysqlconn);
                mysql->RecycleConnection(mysqlconn);
                if(res == nullptr){
                    return nullptr;
                }
                return res;
            }
            mysql->RecycleConnection(mysqlconn);
            return nullptr;
        }

        bool cacheadd(string table,string key ,string value,string time){
            string tabletype = _tabletypemap[table];
            auto redisconn = redis->take();
            string nosql;
            if(tabletype == "hash")
                nosql = "lpush "+key+" "+value;
            else{
                nosql = "SET "+key+" "+value + " EX "+time;
            }
            auto ret = redis->ExecuteNoSQL(redisconn,nosql.c_str());
            if(ret != 0){
                redis->recycle(redisconn);
                return false;
            }
            if(tabletype == "hash"){
                nosql = "expire " + key + " " + time;
                // LOG_INFO<<"expire: "<<nosql;
                ret = redis->ExecuteNoSQL(redisconn, nosql.c_str());
                // 如果设置失败，即MySQL存在的数据没能同步到redis缓存上去，进行通报
                if (ret != 0)
                {
                    nosql = "del " + key;
                    redis->ExecuteNoSQL(redisconn, nosql.c_str());
                    LOG_INFO << "Redis同步错误: " << nosql;
                    redis->recycle(redisconn);
                    return false;
                }
                //LOG_INFO<<"Redis缓存成功";
            }
            redis->recycle(redisconn);
            return true;
        }

        bool cacheremove(string key){
            auto redisconn = redis->take();
            string nosql = "DEL "+key;
            auto ret = redis->ExecuteNoSQL(redisconn,nosql.c_str());
            redis->recycle(redisconn);
            if(ret != 0){   
                return false;
            }
            return true;
        }

        string RandomNum(int min,int max){
            mt19937 gen(rd());
            uniform_int_distribution<> randomnumber(min,max);
            return to_string(randomnumber(gen));
        }
};
mutex DatabaseCache::_mutex;
shared_ptr<DatabaseCache> DatabaseCache::Cache = nullptr;
#endif // !CONNPOOL_H
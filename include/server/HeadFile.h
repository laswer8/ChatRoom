#include<iostream>
#include<algorithm>
#include<functional>
#include<nlohmann/json.hpp>
#include<string>
#include<fstream>
#include<signal.h>
#include<map>
#include<unordered_map>
#include<muduo/net/EventLoop.h>
#include<muduo/net/TcpServer.h>
#include<muduo/net/TcpClient.h>
#include<memory.h>
#include<pthread.h>
#include<mutex>
#include<time.h>
#include<mysql/mysql.h>
#include<muduo/base/Logging.h>
#include<queue>
#include<unistd.h>
#include<assert.h>
#include<hiredis/hiredis.h>
#include<random>
#include<utility>
#include<vector>
#include<set>
#include<chrono>

using namespace std;
using namespace placeholders;//占位符
using namespace muduo;
using json = nlohmann::json;
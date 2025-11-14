#include<iostream>
#include<algorithm>
#include<functional>
#include<nlohmann/json.hpp>
#include<string>
#include<fstream>
#include<csignal>
#include<map>
#include<unordered_set>
#include<unordered_map>
#include<muduo/net/EventLoop.h>
#include<muduo/net/TcpServer.h>
#include<muduo/net/TcpClient.h>
#include<memory.h>
#include<pthread.h>
#include<mutex>
#include<shared_mutex>
#include<thread>
#include<ctime>
#include<mysql/mysql.h>
#include<muduo/base/Logging.h>
#include<queue>
#include<unistd.h>
#include<cassert>
#include<hiredis/hiredis.h>
#include<random>
#include<utility>
#include<vector>
#include<set>
#include<chrono>
#include<filesystem>
#include<condition_variable>

using namespace std;
using namespace placeholders;//占位符
using namespace muduo;
using json = nlohmann::json;
namespace fs = ::filesystem;
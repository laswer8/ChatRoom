#pragma once

#include <iostream>
#include <algorithm>
#include <functional>
#include <nlohmann/json.hpp>
#include <string>
#include <fstream>
#include <csignal>
#include <map>
#include <unordered_set>
#include <unordered_map>
#include <muduo/net/EventLoop.h>
#include <muduo/net/TcpServer.h>
#include <muduo/net/TcpClient.h>
#include <memory.h>
#include <pthread.h>
#include <mutex>
#include <shared_mutex>
#include <thread>
#include <ctime>
#include <mysql/mysql.h>
#include <muduo/base/Logging.h>
#include <queue>
#include <unistd.h>
#include <cassert>
#include <hiredis/hiredis.h>
#include <random>
#include <utility>
#include <vector>
#include <set>
#include <chrono>
#include <filesystem>
#include <condition_variable>
#include <openssl/evp.h>
#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <openssl/md5.h>
#include <openssl/sha.h>
#include <future>
#include <jwt-cpp/jwt.h>
#include <bcrypt/BCrypt.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <websocketpp/server.hpp>
#include <websocketpp/config/asio.hpp>
#include <librdkafka/rdkafkacpp.h>
#include <boost/asio/ssl.hpp>
#include <sstream>
#include <boost/asio/ip/tcp.hpp>

using namespace std;
using namespace placeholders;//占位符
using namespace muduo;
using json = nlohmann::json;
namespace fs = ::filesystem;
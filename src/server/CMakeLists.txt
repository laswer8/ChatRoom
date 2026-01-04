#定义SRC_LIST包含该目录下所有的源文件
aux_source_directory(. SRC_LIST)
#头文件搜索路径
include_directories(${PROJECT_SOURCE_DIR}/include)
include_directories(${PROJECT_SOURCE_DIR}/include/server)
include_directories(${PROJECT_SOURCE_DIR}/include/server/redis)
include_directories(${PROJECT_SOURCE_DIR}/include/server/db)
include_directories(${PROJECT_SOURCE_DIR}/include/server/dbhandler)
include_directories(${PROJECT_SOURCE_DIR}/include/server/tables)




find_package(OpenSSL REQUIRED)
find_package(jwt-cpp CONFIG REQUIRED)
find_package(Threads REQUIRED)

include_directories(
        ${CMAKE_SOURCE_DIR}/include
        ${OPENSSL_INCLUDE_DIR}
)
#生成可执行文件
add_executable(ChatServer ${SRC_LIST})
#链接需要的动态库或静态库
target_link_libraries(ChatServer PRIVATE
        muduo_net muduo_base pthread mysqlclient hiredis
        jwt-cpp::jwt-cpp OpenSSL::SSL OpenSSL::Crypto bcrypt rdkafka++
        boost_system boost_regex boost_date_time)

set (EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin)


## 源文件
#file(GLOB_RECURSE SERVER_SOURCES
#    "*.cpp"
#    "*.hpp"
#    "*.h"
#)
#
## 生成可执行文件
#add_executable(ChatServer
#    ${SERVER_SOURCES}
#)
#
## 查找依赖项
#find_package(OpenSSL REQUIRED)
#find_package(Threads REQUIRED)
#
## 设置包含目录
#target_include_directories(ChatServer
#    PRIVATE
#        ${PROJECT_SOURCE_DIR}/include
#        ${OPENSSL_INCLUDE_DIR}
#        ${MYSQL_INCLUDE_DIR}
#)
#
## 链接库
#target_link_libraries(ChatServer
#    PRIVATE
#        # muduo网络库
#        muduo_net
#        muduo_base
#
#        # 系统库
#        Threads::Threads
#        OpenSSL::SSL
#        OpenSSL::Crypto
#        dl
#        rt
#
#        # 数据库
#        ${MYSQL_LIBRARY}
#        hiredis
#
#        # 消息队列
#        rdkafka++
#        rdkafka
#
#        # 其他依赖
#        bcrypt
#        websocketpp
#        ssl
#        crypto
#        pthread
#)
#
## 设置目标属性
#set_target_properties(ChatServer PROPERTIES
#    OUTPUT_NAME "chatserver"
#    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
#)
cmake_minimum_required(VERSION 3.5)
project(ChatRoom VERSION 1.0.0 LANGUAGES CXX)

# 设置C++标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

#编译选项
set(CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS} -g)
set(CMAKE_CXX_FLAGS_RELEASE -o2)

#头文件搜索路径
include_directories(${PROJECT_SOURCE_DIR}/include)
include_directories(${PROJECT_SOURCE_DIR}/include/server)
include_directories(${PROJECT_SOURCE_DIR}/include/server/redis)
include_directories(${PROJECT_SOURCE_DIR}/include/server/db)
include_directories(${PROJECT_SOURCE_DIR}/include/server/dbhandler)
include_directories(${PROJECT_SOURCE_DIR}/include/server/tables)

#加载子目录
add_subdirectory(${PROJECT_SOURCE_DIR}/src)



#cmake_minimum_required(VERSION 3.12)
#project(ChatRoom VERSION 1.0.0 LANGUAGES CXX)
#
## 设置C++标准
#set(CMAKE_CXX_STANDARD 17)
#set(CMAKE_CXX_STANDARD_REQUIRED ON)
#
## 编译选项
#set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -Wall -Wextra -Wpedantic -Werror")
#set(CMAKE_CXX_FLAGS_RELEASE "-O2")
#
## 输出目录
#set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/lib)
#set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/lib)
#set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/bin)
#
## 查找依赖包
#find_package(Threads REQUIRED)
#find_package(OpenSSL REQUIRED)
#find_package(PkgConfig REQUIRED)
#
## 使用pkg-config查找其他库
#pkg_check_modules(MUDUO REQUIRED IMPORTED_TARGET muduo_net muduo_base)
#pkg_check_modules(HIREDIS REQUIRED IMPORTED_TARGET hiredis)
#pkg_check_modules(RDKAFKA REQUIRED IMPORTED_TARGET rdkafka++)
#pkg_check_modules(JWT REQUIRED IMPORTED_TARGET jwt-cpp)
#pkg_check_modules(BCRYPT REQUIRED IMPORTED_TARGET bcrypt)
#pkg_check_modules(WEBSOCKETPP REQUIRED IMPORTED_TARGET websocketpp)
#pkg_check_modules(NLOHMANN_JSON REQUIRED IMPORTED_TARGET nlohmann_json)
#pkg_check_modules(BOOST REQUIRED IMPORTED_TARGET boost_uuid)
#
## 查找MySQL
#find_path(MYSQL_INCLUDE_DIR mysql.h
#        PATH_SUFFIXES mysql
#        PATHS
#        /usr/local/mysql/include
#        /usr/include/mysql
#        /usr/local/include/mysql
#)
#
#find_library(MYSQL_LIBRARY
#        NAMES mysqlclient mysqlclient_r
#        PATHS
#        /usr/local/mysql/lib
#        /usr/lib/mysql
#        /usr/local/lib/mysql
#)
#
#if(NOT MYSQL_INCLUDE_DIR OR NOT MYSQL_LIBRARY)
#    message(FATAL_ERROR "MySQL libraries or headers not found")
#endif()
#
## 查找所有源文件
#file(GLOB_RECURSE SERVER_SOURCES
#        "src/server/*.cpp"
#        "src/server/*.hpp"
#        "src/server/*.h"
#)
#
#file(GLOB_RECURSE CLIENT_SOURCES
#        "src/client/*.cpp"
#        "src/client/*.hpp"
#        "src/client/*.h"
#)
#
## 添加服务器可执行文件
#add_executable(ChatServer
#        ${SERVER_SOURCES}
#)
#
## 设置服务器包含目录
#target_include_directories(ChatServer
#        PRIVATE
#        ${PROJECT_SOURCE_DIR}/include
#        ${MYSQL_INCLUDE_DIR}
#        ${OPENSSL_INCLUDE_DIR}
#)
#
## 链接服务器库
#target_link_libraries(ChatServer
#        PRIVATE
#        PkgConfig::MUDUO
#        PkgConfig::HIREDIS
#        PkgConfig::RDKAFKA
#        PkgConfig::JWT
#        PkgConfig::BCRYPT
#        PkgConfig::WEBSOCKETPP
#        PkgConfig::NLOHMANN_JSON
#        PkgConfig::BOOST
#        OpenSSL::SSL
#        OpenSSL::Crypto
#        ${MYSQL_LIBRARY}
#        Threads::Threads
#        dl
#        rt
#)
#
## 添加客户端可执行文件
#add_executable(ChatClient
#        ${CLIENT_SOURCES}
#)
#
## 设置客户端包含目录
#target_include_directories(ChatClient
#        PRIVATE
#        ${PROJECT_SOURCE_DIR}/include
#)
#
## 链接客户端库
#target_link_libraries(ChatClient
#        PRIVATE
#        pthread
#        ssl
#        crypto
#)
#
## 设置目标属性
#set_target_properties(
#        ChatServer
#        ChatClient
#        PROPERTIES
#        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
#        OUTPUT_NAME_DEBUG "${CMAKE_PROJECT_NAME}_debug"
#        OUTPUT_NAME_RELEASE "${CMAKE_PROJECT_NAME}"
#)
#
## 安装目标
#install(TARGETS ChatServer ChatClient
#        RUNTIME DESTINATION bin
#        LIBRARY DESTINATION lib
#        ARCHIVE DESTINATION lib
#)
#
## 安装头文件
#install(DIRECTORY ${PROJECT_SOURCE_DIR}/include/
#        DESTINATION include
#        FILES_MATCHING PATTERN "*.h*"
#)
#
## 添加测试
##enable_testing()
##add_subdirectory(test)
#
## 添加可执行文件到IDE项目
#source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/src
#        PREFIX "Source Files"
#        FILES ${SERVER_SOURCES} ${CLIENT_SOURCES}
#)
#
## 添加编译后事件，复制必要的资源文件
#add_custom_command(TARGET ChatServer POST_BUILD
#        COMMAND ${CMAKE_COMMAND} -E copy_directory
#        ${CMAKE_SOURCE_DIR}/lua $<TARGET_FILE_DIR:ChatServer>/lua
#        COMMENT "Copying lua scripts..."
#)


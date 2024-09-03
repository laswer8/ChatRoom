#定义SRC_LIST包含该目录下所有的源文件
aux_source_directory(. SRC_LIST)

#生成可执行文件
add_executable(ChatServer ${SRC_LIST})
#链接需要的动态库或静态库
target_link_libraries(ChatServer muduo_net muduo_base pthread mysqlclient hiredis)

#aux_source_directory(. SRC_LIST)
#
#add_executable(ChatClient ${SRC_LIST})
#
#target_link_libraries(ChatClient pthread)

# 源文件
file(GLOB_RECURSE CLIENT_SOURCES 
    "*.cpp" 
    "*.hpp" 
    "*.h"
)
 
# 生成可执行文件
add_executable(ChatClient 
    ${CLIENT_SOURCES}
)
 
# 链接库
target_link_libraries(ChatClient
    PRIVATE
        pthread
        ssl
        crypto
)
 
# 设置目标属性
set_target_properties(ChatClient PROPERTIES
    OUTPUT_NAME "chatclient"
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )
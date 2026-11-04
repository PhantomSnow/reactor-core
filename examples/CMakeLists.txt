add_executable(echo_server echo/echo_server.cpp)
target_link_libraries(echo_server PRIVATE tinynet)

add_executable(http_server http/http_server.cpp)
target_link_libraries(http_server PRIVATE tinynet)

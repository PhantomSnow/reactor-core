set(TINYNET_TESTS
    buffer_test
    socket_test
    channel_test
    eventloop_test
    tcp_server_test
)

foreach(test_name IN LISTS TINYNET_TESTS)
    add_executable(${test_name} ${test_name}.cpp)
    target_link_libraries(${test_name} PRIVATE tinynet)
    add_test(NAME ${test_name} COMMAND ${test_name})
    set_tests_properties(${test_name} PROPERTIES TIMEOUT 60)
endforeach()

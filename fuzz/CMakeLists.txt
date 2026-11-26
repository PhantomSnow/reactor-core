add_executable(buffer_fuzzer buffer_fuzzer.cpp)
target_link_libraries(buffer_fuzzer PRIVATE tinynet)
target_compile_options(buffer_fuzzer PRIVATE -fsanitize=fuzzer)
target_link_options(buffer_fuzzer PRIVATE -fsanitize=fuzzer)

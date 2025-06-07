# 添加测试可执行文件
add_executable(test_hash_calculations
    test_hash_calculations.c
)

add_executable(test_json_extraction
    test_json_extraction.c
)

add_executable(test_consistent_hash
    test_consistent_hash.c
)

# 设置包括目录
target_include_directories(test_hash_calculations PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${cmocka_SOURCE_DIR}/include
)

target_include_directories(test_json_extraction PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${cmocka_SOURCE_DIR}/include
)

target_include_directories(test_consistent_hash PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${cmocka_SOURCE_DIR}/include
)

# 链接到CMocka库
target_link_libraries(test_hash_calculations
    cmocka
    cjson  # cJSON库
    m  # 数学库，用于sqrt等函数
    module_wrapper  # 链接到模块包装库
)

target_link_libraries(test_json_extraction
    cmocka
    cjson  # cJSON库
    module_wrapper
)

target_link_libraries(test_consistent_hash
    cmocka
    cjson  # cJSON库
    module_wrapper
)

# 添加测试
add_test(NAME test_hash_calculations COMMAND test_hash_calculations)
add_test(NAME test_json_extraction COMMAND test_json_extraction)
add_test(NAME test_consistent_hash COMMAND test_consistent_hash)

# 设置测试属性
set_tests_properties(test_hash_calculations PROPERTIES TIMEOUT 10)
set_tests_properties(test_json_extraction PROPERTIES TIMEOUT 10)
set_tests_properties(test_consistent_hash PROPERTIES TIMEOUT 10) 
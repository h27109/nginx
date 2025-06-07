#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <string.h>
#include <stdio.h>
#include <cjson/cJSON.h>

#include "nginx_stub.h"

/* 通过包装层访问模块函数 */
extern ngx_int_t wrapped_ngx_http_upstream_json_validate_depth(cJSON *json, ngx_uint_t max_depth, 
                                                             ngx_uint_t current_depth, ngx_log_t *log);

/* 测试JSON深度验证 */
static void test_json_depth_validation(void **state) {
    (void) state; /* 未使用 */
    
    /* 创建简单JSON对象 */
    cJSON *json = cJSON_CreateObject();
    assert_non_null(json);
    
    ngx_log_t log = {0};
    
    /* 测试深度为0的情况 */
    ngx_int_t result = wrapped_ngx_http_upstream_json_validate_depth(json, 5, 0, &log);
    assert_int_equal(result, 0); /* NGX_OK */
    
    /* 测试超过最大深度的情况 */
    result = wrapped_ngx_http_upstream_json_validate_depth(json, 5, 6, &log);
    assert_int_equal(result, -1); /* NGX_ERROR */
    
    cJSON_Delete(json);
}

/* 测试嵌套JSON对象 */
static void test_nested_json_validation(void **state) {
    (void) state; /* 未使用 */
    
    /* 创建嵌套JSON对象 */
    cJSON *root = cJSON_CreateObject();
    cJSON *level1 = cJSON_CreateObject();
    cJSON *level2 = cJSON_CreateObject();
    
    assert_non_null(root);
    assert_non_null(level1);
    assert_non_null(level2);
    
    cJSON_AddItemToObject(root, "level1", level1);
    cJSON_AddItemToObject(level1, "level2", level2);
    cJSON_AddStringToObject(level2, "value", "test");
    
    ngx_log_t log = {0};
    
    /* 测试深度限制为5的情况（应该通过） */
    ngx_int_t result = wrapped_ngx_http_upstream_json_validate_depth(root, 5, 0, &log);
    assert_int_equal(result, 0); /* NGX_OK */
    
    /* 测试深度限制为1的情况（应该失败） */
    result = wrapped_ngx_http_upstream_json_validate_depth(root, 1, 0, &log);
    assert_int_equal(result, -1); /* NGX_ERROR */
    
    cJSON_Delete(root);
}

/* 测试JSON数组 */
static void test_json_array_validation(void **state) {
    (void) state; /* 未使用 */
    
    /* 创建JSON数组 */
    cJSON *array = cJSON_CreateArray();
    cJSON *item1 = cJSON_CreateString("item1");
    cJSON *item2 = cJSON_CreateString("item2");
    
    assert_non_null(array);
    assert_non_null(item1);
    assert_non_null(item2);
    
    cJSON_AddItemToArray(array, item1);
    cJSON_AddItemToArray(array, item2);
    
    ngx_log_t log = {0};
    
    /* 测试数组深度验证 */
    ngx_int_t result = wrapped_ngx_http_upstream_json_validate_depth(array, 5, 0, &log);
    assert_int_equal(result, 0); /* NGX_OK */
    
    cJSON_Delete(array);
}

/* 测试基本类型JSON */
static void test_primitive_json_validation(void **state) {
    (void) state; /* 未使用 */
    
    ngx_log_t log = {0};
    
    /* 测试字符串 */
    cJSON *str = cJSON_CreateString("test");
    assert_non_null(str);
    ngx_int_t result = wrapped_ngx_http_upstream_json_validate_depth(str, 5, 0, &log);
    assert_int_equal(result, 0); /* NGX_OK */
    cJSON_Delete(str);
    
    /* 测试数字 */
    cJSON *num = cJSON_CreateNumber(42);
    assert_non_null(num);
    result = wrapped_ngx_http_upstream_json_validate_depth(num, 5, 0, &log);
    assert_int_equal(result, 0); /* NGX_OK */
    cJSON_Delete(num);
    
    /* 测试布尔值 */
    cJSON *bool_val = cJSON_CreateBool(1);
    assert_non_null(bool_val);
    result = wrapped_ngx_http_upstream_json_validate_depth(bool_val, 5, 0, &log);
    assert_int_equal(result, 0); /* NGX_OK */
    cJSON_Delete(bool_val);
    
    /* 测试null */
    cJSON *null_val = cJSON_CreateNull();
    assert_non_null(null_val);
    result = wrapped_ngx_http_upstream_json_validate_depth(null_val, 5, 0, &log);
    assert_int_equal(result, 0); /* NGX_OK */
    cJSON_Delete(null_val);
}

/* 测试运行入口 */
int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_json_depth_validation),
        cmocka_unit_test(test_nested_json_validation),
        cmocka_unit_test(test_json_array_validation),
        cmocka_unit_test(test_primitive_json_validation),
    };
    
    return cmocka_run_group_tests(tests, NULL, NULL);
} 
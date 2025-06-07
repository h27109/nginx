#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <string.h>
#include <stdio.h>

#include "nginx_stub.h"

/* 通过包装层访问模块函数 */
extern int wrapped_ngx_http_upstream_json_chash_cmp_points(const void *one, const void *two);
extern ngx_uint_t wrapped_ngx_http_upstream_find_json_chash_point(void *points_ptr, uint32_t hash);

/* 测试一致性哈希点比较函数 */
static void test_chash_cmp_points(void **state) {
    (void) state; /* 未使用 */
    
    /* 定义测试点结构 */
    typedef struct {
        uint32_t hash;
        void *server;
    } test_point_t;
    
    test_point_t point1 = {100, NULL};
    test_point_t point2 = {200, NULL};
    test_point_t point3 = {100, NULL};
    
    /* 测试小于关系 */
    int result = wrapped_ngx_http_upstream_json_chash_cmp_points(&point1, &point2);
    assert_int_equal(result, -1);
    
    /* 测试大于关系 */
    result = wrapped_ngx_http_upstream_json_chash_cmp_points(&point2, &point1);
    assert_int_equal(result, 1);
    
    /* 测试相等关系 */
    result = wrapped_ngx_http_upstream_json_chash_cmp_points(&point1, &point3);
    assert_int_equal(result, 0);
}

/* 测试哈希点查找函数 */
static void test_find_chash_point(void **state) {
    (void) state; /* 未使用 */
    
    /* 定义测试点集合结构 */
    typedef struct {
        ngx_uint_t number;
        struct {
            uint32_t hash;
            void *server;
        } point[5];
    } test_points_t;
    
    test_points_t points;
    points.number = 5;
    points.point[0].hash = 100;
    points.point[1].hash = 200;
    points.point[2].hash = 300;
    points.point[3].hash = 400;
    points.point[4].hash = 500;
    
    /* 测试查找存在的哈希值 */
    ngx_uint_t index = wrapped_ngx_http_upstream_find_json_chash_point(&points, 300);
    assert_int_equal(index, 2);
    
    /* 测试查找不存在的哈希值（应该返回插入位置） */
    index = wrapped_ngx_http_upstream_find_json_chash_point(&points, 250);
    assert_int_equal(index, 2); /* 应该插入在300之前 */
    
    /* 测试查找小于所有值的哈希 */
    index = wrapped_ngx_http_upstream_find_json_chash_point(&points, 50);
    assert_int_equal(index, 0);
    
    /* 测试查找大于所有值的哈希 */
    index = wrapped_ngx_http_upstream_find_json_chash_point(&points, 600);
    assert_int_equal(index, 5);
}

/* 测试边界情况 */
static void test_chash_edge_cases(void **state) {
    (void) state; /* 未使用 */
    
    /* 测试空点集合 */
    typedef struct {
        ngx_uint_t number;
        struct {
            uint32_t hash;
            void *server;
        } point[1];
    } empty_points_t;
    
    empty_points_t empty_points;
    empty_points.number = 0;
    
    ngx_uint_t index = wrapped_ngx_http_upstream_find_json_chash_point(&empty_points, 100);
    assert_int_equal(index, 0);
    
    /* 测试单个点的情况 */
    empty_points.number = 1;
    empty_points.point[0].hash = 200;
    
    /* 查找小于唯一点的哈希 */
    index = wrapped_ngx_http_upstream_find_json_chash_point(&empty_points, 100);
    assert_int_equal(index, 0);
    
    /* 查找等于唯一点的哈希 */
    index = wrapped_ngx_http_upstream_find_json_chash_point(&empty_points, 200);
    assert_int_equal(index, 0);
    
    /* 查找大于唯一点的哈希 */
    index = wrapped_ngx_http_upstream_find_json_chash_point(&empty_points, 300);
    assert_int_equal(index, 1);
}

/* 测试哈希分布 */
static void test_chash_distribution(void **state) {
    (void) state; /* 未使用 */
    
    /* 创建多个测试点 */
    typedef struct {
        ngx_uint_t number;
        struct {
            uint32_t hash;
            void *server;
        } point[10];
    } large_points_t;
    
    large_points_t points;
    points.number = 10;
    
    /* 初始化点集合 */
    for (int i = 0; i < 10; i++) {
        points.point[i].hash = (i + 1) * 100; /* 100, 200, 300, ..., 1000 */
        points.point[i].server = NULL;
    }
    
    /* 测试多个查找操作 */
    ngx_uint_t index;
    
    index = wrapped_ngx_http_upstream_find_json_chash_point(&points, 150);
    assert_int_equal(index, 1); /* 应该在200之前 */
    
    index = wrapped_ngx_http_upstream_find_json_chash_point(&points, 550);
    assert_int_equal(index, 5); /* 应该在600之前 */
    
    index = wrapped_ngx_http_upstream_find_json_chash_point(&points, 1000);
    assert_int_equal(index, 9); /* 精确匹配最后一个 */
    
    index = wrapped_ngx_http_upstream_find_json_chash_point(&points, 1100);
    assert_int_equal(index, 10); /* 超出所有点 */
}

/* 测试运行入口 */
int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_chash_cmp_points),
        cmocka_unit_test(test_find_chash_point),
        cmocka_unit_test(test_chash_edge_cases),
        cmocka_unit_test(test_chash_distribution),
    };
    
    return cmocka_run_group_tests(tests, NULL, NULL);
} 
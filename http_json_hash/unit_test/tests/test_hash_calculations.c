#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

#include "nginx_stub.h"

/* 通过包装层访问模块函数 */
extern uint32_t wrapped_ngx_http_upstream_json_hash_getblock32(const uint8_t *data, int offset);
extern uint32_t wrapped_ngx_http_upstream_json_hash_murmur3(const void *key, int len, uint32_t seed);
extern uint32_t wrapped_ngx_http_upstream_json_hash_calculate(u_char *data, size_t len, ngx_uint_t method);

/* 测试获取32位块函数 */
static void test_getblock32(void **state) {
    (void) state; /* 未使用 */
    
    uint8_t test_data[] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
    uint32_t result = wrapped_ngx_http_upstream_json_hash_getblock32(test_data, 0);
    
    /* 验证字节序正确 */
    assert_int_equal(result, 0x04030201);
    
    /* 测试偏移量 */
    result = wrapped_ngx_http_upstream_json_hash_getblock32(test_data, 4);
    assert_int_equal(result, 0x08070605);
}

/* 测试MurmurHash3算法 */
static void test_murmur3_hash(void **state) {
    (void) state; /* 未使用 */
    
    /* 测试常见情况 */
    const char *key1 = "test";
    uint32_t hash1 = wrapped_ngx_http_upstream_json_hash_murmur3(key1, strlen(key1), 0);
    assert_int_not_equal(hash1, 0); /* 应该产生非零哈希值 */
    
    /* 测试空字符串 - MurmurHash3对空字符串可能返回0，这是正常的 */
    const char *key2 = "";
    uint32_t hash2 = wrapped_ngx_http_upstream_json_hash_murmur3(key2, strlen(key2), 0);
    /* 空字符串的哈希值是确定的，不需要检查是否非零 */
    
    /* 测试不同种子值 */
    uint32_t hash3 = wrapped_ngx_http_upstream_json_hash_murmur3(key1, strlen(key1), 42);
    assert_int_not_equal(hash1, hash3); /* 不同种子应产生不同哈希值 */
    
    /* 测试相同输入产生相同输出 */
    uint32_t hash4 = wrapped_ngx_http_upstream_json_hash_murmur3(key1, strlen(key1), 0);
    assert_int_equal(hash1, hash4); /* 相同输入和种子应产生相同哈希值 */
    
    /* 测试空字符串的一致性 */
    uint32_t hash5 = wrapped_ngx_http_upstream_json_hash_murmur3(key2, strlen(key2), 0);
    assert_int_equal(hash2, hash5); /* 相同的空字符串应产生相同哈希值 */
    
    /* 测试不同长度输入 */
    const char *key6 = "testtest";
    uint32_t hash6 = wrapped_ngx_http_upstream_json_hash_murmur3(key6, strlen(key6), 0);
    assert_int_not_equal(hash1, hash6); /* 不同长度输入应产生不同哈希值 */
}

/* 测试统一哈希计算接口 */
static void test_hash_calculate(void **state) {
    (void) state; /* 未使用 */
    
    const char *key = "user123";
    size_t key_len = strlen(key);
    
    /* 测试CRC32方法 (0) */
    uint32_t hash1 = wrapped_ngx_http_upstream_json_hash_calculate((u_char*)key, key_len, 0);
    assert_int_not_equal(hash1, 0);
    
    /* 测试MurmurHash3方法 (1) */
    uint32_t hash2 = wrapped_ngx_http_upstream_json_hash_calculate((u_char*)key, key_len, 1);
    assert_int_not_equal(hash2, 0);
    
    /* 验证不同方法产生不同哈希值 */
    assert_int_not_equal(hash1, hash2);
    
    /* 验证相同输入相同方法产生相同哈希值 */
    uint32_t hash3 = wrapped_ngx_http_upstream_json_hash_calculate((u_char*)key, key_len, 0);
    assert_int_equal(hash1, hash3);
}

/* 测试哈希分布均匀性 */
static void test_hash_distribution(void **state) {
    (void) state; /* 未使用 */
    
    /* 测试10000个键在10个桶中的分布 */
    const int NUM_KEYS = 10000;
    const int NUM_BUCKETS = 10;
    int bucket_counts[NUM_BUCKETS];
    char key[32];
    
    /* 初始化数组 */
    for (int i = 0; i < NUM_BUCKETS; i++) {
        bucket_counts[i] = 0;
    }
    
    for (int i = 0; i < NUM_KEYS; i++) {
        snprintf(key, sizeof(key), "test_key_%d", i);
        uint32_t hash = wrapped_ngx_http_upstream_json_hash_calculate((u_char*)key, strlen(key), 1); /* 使用MurmurHash3 */
        int bucket = hash % NUM_BUCKETS;
        bucket_counts[bucket]++;
    }
    
    /* 计算标准差，检查分布均匀性 */
    int expected_per_bucket = NUM_KEYS / NUM_BUCKETS;
    double sum_squared_diff = 0;
    
    for (int i = 0; i < NUM_BUCKETS; i++) {
        int diff = bucket_counts[i] - expected_per_bucket;
        sum_squared_diff += diff * diff;
    }
    
    double std_dev = sqrt(sum_squared_diff / NUM_BUCKETS);
    
    /* 标准差不应超过预期平均值的10% */
    assert_true(std_dev < 0.1 * expected_per_bucket);
}

/* 测试边界情况 */
static void test_hash_edge_cases(void **state) {
    (void) state; /* 未使用 */
    
    /* 测试单字节输入 */
    const char key1[] = {0x00};
    uint32_t hash1 = wrapped_ngx_http_upstream_json_hash_calculate((u_char*)key1, 1, 1);
    assert_int_not_equal(hash1, 0);
    
    /* 测试非ASCII字符 */
    const char key2[] = {0xFF, 0xFE, 0xFD, 0xFC};
    uint32_t hash2 = wrapped_ngx_http_upstream_json_hash_calculate((u_char*)key2, 4, 1);
    assert_int_not_equal(hash2, 0);
    
    /* 测试长度为3的输入（非4字节对齐，测试尾部处理） */
    const char key3[] = {0x01, 0x02, 0x03};
    uint32_t hash3 = wrapped_ngx_http_upstream_json_hash_calculate((u_char*)key3, 3, 1);
    assert_int_not_equal(hash3, 0);
}

/* 测试运行入口 */
int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_getblock32),
        cmocka_unit_test(test_murmur3_hash),
        cmocka_unit_test(test_hash_calculate),
        cmocka_unit_test(test_hash_distribution),
        cmocka_unit_test(test_hash_edge_cases),
    };
    
    return cmocka_run_group_tests(tests, NULL, NULL);
} 
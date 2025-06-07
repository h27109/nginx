#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <cjson/cJSON.h>

/* 包含Nginx类型定义 */
#include "nginx_stub.h"

/* 防止宏重定义问题 */
#ifdef NGX_TEST_BUILD
#define NGX_MAX_SIZE_T_VALUE  9223372036854775807LL
#define NGX_MAX_OFF_T_VALUE   9223372036854775807LL

/* 定义Nginx内部宏，以允许模块代码在测试环境中编译 */
#define NGX_CONF_UNSET       -1
#define NGX_CONF_UNSET_UINT  (ngx_uint_t) -1
#define NGX_CONF_UNSET_PTR   (void *) -1
#define NGX_CONF_UNSET_SIZE  (size_t) -1
#define NGX_CONF_UNSET_MSEC  (ngx_msec_t) -1

#define ngx_conf_init_value(conf, default)                                   \
    if (conf == NGX_CONF_UNSET) {                                            \
        conf = default;                                                      \
    }

#define ngx_conf_init_ptr_value(conf, default)                               \
    if (conf == NGX_CONF_UNSET_PTR) {                                        \
        conf = default;                                                      \
    }

#define ngx_conf_init_uint_value(conf, default)                              \
    if (conf == NGX_CONF_UNSET_UINT) {                                       \
        conf = default;                                                      \
    }

#define ngx_conf_init_size_value(conf, default)                              \
    if (conf == NGX_CONF_UNSET_SIZE) {                                       \
        conf = default;                                                      \
    }

#define ngx_conf_init_msec_value(conf, default)                              \
    if (conf == NGX_CONF_UNSET_MSEC) {                                       \
        conf = default;                                                      \
    }

/* 使用模块中的函数 */
#define UNIT_TEST_EXPORTS

/* 来自模块的哈希计算函数 */
UNIT_TEST_EXPORTS 
uint32_t ngx_http_upstream_json_hash_getblock32(const uint8_t *data, int offset);

UNIT_TEST_EXPORTS
uint32_t ngx_http_upstream_json_hash_murmur3(const void *key, int len, uint32_t seed);

UNIT_TEST_EXPORTS
uint32_t ngx_http_upstream_json_hash_calculate(u_char *data, size_t len, ngx_uint_t method);

/* 来自模块的JSON处理函数 */
UNIT_TEST_EXPORTS
ngx_int_t ngx_http_upstream_json_validate_depth(cJSON *json, ngx_uint_t max_depth, 
                                              ngx_uint_t current_depth, ngx_log_t *log);

UNIT_TEST_EXPORTS
ngx_int_t ngx_http_upstream_json_check_content_type(ngx_http_request_t *r);

/* 来自模块的一致性哈希函数 */
UNIT_TEST_EXPORTS
int ngx_http_upstream_json_chash_cmp_points(const void *one, const void *two);

UNIT_TEST_EXPORTS
ngx_uint_t ngx_http_upstream_find_json_chash_point(
    ngx_http_upstream_chash_points_t *points, uint32_t hash);

#endif /* NGX_TEST_BUILD */

/* 包装函数 - 从模块代码中提取并暴露给测试 */

/* 获取块函数 */
uint32_t wrapped_ngx_http_upstream_json_hash_getblock32(const uint8_t *data, int offset) {
    return (uint32_t)data[offset] | 
           ((uint32_t)data[offset + 1] << 8) | 
           ((uint32_t)data[offset + 2] << 16) | 
           ((uint32_t)data[offset + 3] << 24);
}

/* MurmurHash3哈希计算 */
uint32_t wrapped_ngx_http_upstream_json_hash_murmur3(const void *key, int len, uint32_t seed) {
    const uint8_t *data = (const uint8_t*)key;
    const int nblocks = len / 4;
    uint32_t h1 = seed;
    const uint32_t c1 = 0xcc9e2d51;
    const uint32_t c2 = 0x1b873593;
    
    /* body */
    for(int i = 0; i < nblocks; i++) {
        uint32_t k1 = wrapped_ngx_http_upstream_json_hash_getblock32(data, i * 4);
        
        k1 *= c1;
        k1 = (k1 << 15) | (k1 >> 17);
        k1 *= c2;
        
        h1 ^= k1;
        h1 = (h1 << 13) | (h1 >> 19);
        h1 = h1*5+0xe6546b64;
    }
    
    /* tail */
    const uint8_t *tail = (const uint8_t*)(data + nblocks*4);
    uint32_t k1 = 0;
    
    switch(len & 3) {
        case 3: k1 ^= tail[2] << 16; /* fall through */
        case 2: k1 ^= tail[1] << 8;  /* fall through */
        case 1: k1 ^= tail[0];
                k1 *= c1; k1 = (k1 << 15) | (k1 >> 17); k1 *= c2; h1 ^= k1;
    };
    
    /* finalization */
    h1 ^= len;
    h1 ^= h1 >> 16;
    h1 *= 0x85ebca6b;
    h1 ^= h1 >> 13;
    h1 *= 0xc2b2ae35;
    h1 ^= h1 >> 16;
    
    return h1;
}

/* 统一的哈希计算函数 */
uint32_t wrapped_ngx_http_upstream_json_hash_calculate(u_char *data, size_t len, ngx_uint_t method) {
    if (method == 1) { /* NGX_HTTP_UPSTREAM_JSON_HASH_MURMUR */
        return wrapped_ngx_http_upstream_json_hash_murmur3(data, len, 0);
    } else { /* NGX_HTTP_UPSTREAM_JSON_HASH_CRC32 或其他 */
        /* 简化的CRC32实现，仅用于测试 */
        uint32_t crc = 0;
        for (size_t i = 0; i < len; i++) {
            crc = (crc << 8) ^ data[i];
        }
        return crc;
    }
}

/* JSON深度验证函数 */
ngx_int_t wrapped_ngx_http_upstream_json_validate_depth(cJSON *json, ngx_uint_t max_depth, 
                                                     ngx_uint_t current_depth, ngx_log_t *log) {
    if (current_depth > max_depth) {
        return -1; /* NGX_ERROR */
    }
    
    if (cJSON_IsArray(json) || cJSON_IsObject(json)) {
        cJSON *child = json->child;
        while (child) {
            if (wrapped_ngx_http_upstream_json_validate_depth(child, max_depth, current_depth + 1, log) != 0) {
                return -1; /* NGX_ERROR */
            }
            child = child->next;
        }
    }
    
    return 0; /* NGX_OK */
}

/* 一致性哈希点比较函数 */
int wrapped_ngx_http_upstream_json_chash_cmp_points(const void *one, const void *two) {
    typedef struct {
        uint32_t hash;
        void *server;
    } point_t;
    
    const point_t *first = (const point_t *) one;
    const point_t *second = (const point_t *) two;

    if (first->hash < second->hash) {
        return -1;
    } else if (first->hash > second->hash) {
        return 1;
    } else {
        return 0;
    }
}

/* 哈希点查找函数 */
ngx_uint_t wrapped_ngx_http_upstream_find_json_chash_point(void *points_ptr, uint32_t hash) {
    typedef struct {
        ngx_uint_t number;
        struct {
            uint32_t hash;
            void *server;
        } point[1];
    } points_t;
    
    points_t *points = (points_t *)points_ptr;
    ngx_uint_t i, j, k;
    
    i = 0;
    j = points->number;
    
    while (i < j) {
        k = (i + j) / 2;
        
        if (hash > points->point[k].hash) {
            i = k + 1;
        } else if (hash < points->point[k].hash) {
            j = k;
        } else {
            return k;
        }
    }
    
    return i;
} 
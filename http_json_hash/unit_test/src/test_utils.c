#include "test_utils.h"
#include <stdio.h>

/* 全局变量，用于存储测试时分配的内存 */
static void **allocated_memory = NULL;
static size_t allocated_count = 0;
static size_t allocated_size = 0;

/* 初始化内存跟踪 */
static void init_memory_tracking(void) {
    if (allocated_memory == NULL) {
        allocated_size = 100;
        allocated_memory = calloc(allocated_size, sizeof(void *));
        allocated_count = 0;
    }
}

/* 添加内存到跟踪列表 */
static void track_memory(void *ptr) {
    init_memory_tracking();
    if (allocated_count >= allocated_size) {
        allocated_size *= 2;
        allocated_memory = realloc(allocated_memory, allocated_size * sizeof(void *));
    }
    allocated_memory[allocated_count++] = ptr;
}

/* 释放所有跟踪的内存 */
static void free_tracked_memory(void) {
    if (allocated_memory) {
        for (size_t i = 0; i < allocated_count; i++) {
            if (allocated_memory[i]) {
                free(allocated_memory[i]);
                allocated_memory[i] = NULL;
            }
        }
        free(allocated_memory);
        allocated_memory = NULL;
        allocated_count = 0;
        allocated_size = 0;
    }
}

/* 模拟ngx_palloc，为测试分配内存 */
void *ngx_palloc(ngx_pool_t *pool, size_t size) {
    (void)pool; /* 忽略pool参数 */
    void *ptr = malloc(size);
    if (ptr) {
        track_memory(ptr);
    }
    return ptr;
}

/* 模拟ngx_pcalloc，为测试分配并清零内存 */
void *ngx_pcalloc(ngx_pool_t *pool, size_t size) {
    (void)pool; /* 忽略pool参数 */
    void *ptr = calloc(1, size);
    if (ptr) {
        track_memory(ptr);
    }
    return ptr;
}

/* 模拟ngx_pfree，为测试释放内存 */
int ngx_pfree(ngx_pool_t *pool, void *p) {
    (void)pool; /* 忽略pool参数 */
    for (size_t i = 0; i < allocated_count; i++) {
        if (allocated_memory[i] == p) {
            free(p);
            allocated_memory[i] = NULL;
            return 0;
        }
    }
    return -1;
}

/* 创建ngx_str_t */
ngx_str_t *create_ngx_str(void *pool, const char *text) {
    ngx_str_t *str = ngx_pcalloc(pool, sizeof(ngx_str_t));
    if (str == NULL) {
        return NULL;
    }
    
    if (text) {
        str->len = strlen(text);
        str->data = ngx_pcalloc(pool, str->len + 1);
        if (str->data == NULL) {
            return NULL;
        }
        memcpy(str->data, text, str->len);
    }
    
    return str;
}

/* 设置ngx_str_t */
void ngx_str_set(ngx_str_t *str, const char *text) {
    if (str && text) {
        str->len = strlen(text);
        str->data = ngx_pcalloc(NULL, str->len + 1);
        memcpy(str->data, text, str->len);
    }
}

/* 创建测试请求 */
ngx_http_request_t *create_test_request(void) {
    ngx_http_request_t *r = ngx_pcalloc(NULL, sizeof(ngx_http_request_t));
    if (r == NULL) {
        return NULL;
    }
    
    r->pool = (ngx_pool_t *)1; /* 设置一个非NULL值，仅用于测试 */
    r->log = ngx_pcalloc(NULL, sizeof(ngx_log_t));
    r->connection = ngx_pcalloc(NULL, sizeof(ngx_connection_t));
    r->connection->log = r->log;
    r->headers_in = ngx_pcalloc(NULL, sizeof(void *)); /* 占位，具体结构在测试中模拟 */
    r->headers_out = ngx_pcalloc(NULL, sizeof(void *)); /* 占位，具体结构在测试中模拟 */
    
    return r;
}

/* 销毁测试请求 */
void destroy_test_request(ngx_http_request_t *r) {
    /* 请求的销毁会在free_tracked_memory中处理 */
    (void)r;
}

/* 创建测试JSON对象 */
cJSON *create_test_json(const char *json_text) {
    return cJSON_Parse(json_text);
}

/* 添加JSON请求体到请求 */
void add_json_body_to_request(ngx_http_request_t *r, const char *json_text) {
    /* 创建请求体结构 */
    r->request_body = ngx_pcalloc(NULL, sizeof(void *));
    
    /* 创建缓冲区链 */
    ngx_chain_t *chain = ngx_pcalloc(NULL, sizeof(ngx_chain_t));
    ngx_buf_t *buf = ngx_pcalloc(NULL, sizeof(ngx_buf_t));
    
    /* 设置缓冲区内容 */
    size_t len = strlen(json_text);
    unsigned char *data = ngx_pcalloc(NULL, len + 1);
    memcpy(data, json_text, len);
    
    /* 设置缓冲区指针 */
    buf->pos = data;
    buf->last = data + len;
    buf->start = data;
    buf->end = data + len;
    buf->memory = 1;
    
    chain->buf = buf;
    chain->next = NULL;
    
    /* 模拟request_body->bufs */
    r->request_body = ngx_pcalloc(NULL, sizeof(void *));
    ngx_chain_t **bufs = (ngx_chain_t **)r->request_body;
    *bufs = chain;
}

/* 创建服务器配置 */
ngx_http_upstream_json_hash_srv_conf_t *create_test_srv_conf(void) {
    ngx_http_upstream_json_hash_srv_conf_t *jhcf = ngx_pcalloc(NULL, sizeof(ngx_http_upstream_json_hash_srv_conf_t));
    if (!jhcf) {
        return NULL;
    }
    
    /* 设置默认值 */
    jhcf->virtual_nodes = NGX_HTTP_UPSTREAM_JSON_HASH_DEFAULT_VIRTUAL_NODES;
    jhcf->hash_method = NGX_HTTP_UPSTREAM_JSON_HASH_CRC32;
    jhcf->max_json_depth = NGX_HTTP_UPSTREAM_JSON_HASH_DEFAULT_MAX_DEPTH;
    jhcf->check_content_type = 1;
    jhcf->stats_interval = 10000;
    jhcf->max_json_size = 1024 * 1024;
    jhcf->max_virtual_memory = 2 * 1024 * 1024;
    
    /* 设置默认fallback key */
    ngx_str_set(&jhcf->fallback_key, "default");
    
    /* 初始化统计字段 */
    jhcf->hash_requests = 0;
    jhcf->hash_failures = 0;
    jhcf->json_parse_time = 0;
    jhcf->content_type_checks = 0;
    jhcf->stats_resets = 0;
    
    return jhcf;
}

/* 销毁服务器配置 */
void destroy_test_srv_conf(ngx_http_upstream_json_hash_srv_conf_t *jhcf) {
    /* 配置的销毁会在free_tracked_memory中处理 */
    (void)jhcf;
}

/* 设置服务器配置的JSON字段 */
void set_json_field(ngx_http_upstream_json_hash_srv_conf_t *jhcf, const char *field) {
    if (jhcf && field) {
        ngx_str_set(&jhcf->json_field, field);
    }
}

/* 设置哈希方法 */
void set_hash_method(ngx_http_upstream_json_hash_srv_conf_t *jhcf, ngx_uint_t method) {
    if (jhcf) {
        jhcf->hash_method = method;
    }
}

/* 创建一组测试服务器 */
void create_test_peers(ngx_uint_t num_peers, ngx_str_t **servers, ngx_uint_t **weights) {
    *servers = ngx_pcalloc(NULL, num_peers * sizeof(ngx_str_t));
    *weights = ngx_pcalloc(NULL, num_peers * sizeof(ngx_uint_t));
    
    for (ngx_uint_t i = 0; i < num_peers; i++) {
        char server_name[32];
        snprintf(server_name, sizeof(server_name), "server%lu", (unsigned long)i);
        ngx_str_set(&(*servers)[i], server_name);
        (*weights)[i] = 1; /* 默认权重为1 */
    }
}

/* 清理所有测试资源，适合在测试后调用 */
void cleanup_test_resources(void) {
    free_tracked_memory();
}

/* 模拟ngx_strcmp */
ngx_int_t ngx_strcmp(const unsigned char *s1, const unsigned char *s2) {
    return strcmp((const char *)s1, (const char *)s2);
}

/* 模拟ngx_memn2cmp */
ngx_int_t ngx_memn2cmp(const unsigned char *s1, const unsigned char *s2, size_t n1, size_t n2) {
    size_t n = n1 < n2 ? n1 : n2;
    int rc = memcmp(s1, s2, n);
    
    if (rc == 0) {
        return (n1 - n2);
    }
    
    return rc;
}

/* CRC32哈希函数模拟 */
uint32_t ngx_crc32_long(unsigned char *p, size_t len) {
    uint32_t crc = 0;
    for (size_t i = 0; i < len; i++) {
        crc = (crc << 8) ^ p[i];
    }
    return crc;
} 
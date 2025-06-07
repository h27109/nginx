/*
 * 单元测试：ngx_http_upstream_json_hash_module
 * 测试各种异常场景和边界情况
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <fcntl.h>

/* 模拟nginx核心结构和函数 */
typedef struct {
    size_t      len;
    u_char     *data;
} ngx_str_t;

typedef struct {
    void       *elts;
    ngx_uint_t  nelts;
    size_t      size;
    ngx_uint_t  nalloc;
    ngx_pool_t *pool;
} ngx_array_t;

typedef struct ngx_pool_s ngx_pool_t;
typedef struct ngx_connection_s ngx_connection_t;
typedef struct ngx_http_request_s ngx_http_request_t;
typedef struct ngx_buf_s ngx_buf_t;
typedef struct ngx_chain_s ngx_chain_t;
typedef struct ngx_log_s ngx_log_t;
typedef struct ngx_file_s ngx_file_t;
typedef struct ngx_conf_s ngx_conf_t;
typedef struct ngx_command_s ngx_command_t;

/* 模拟nginx内存池 */
struct ngx_pool_s {
    char dummy[1024];
};

/* 模拟nginx日志 */
struct ngx_log_s {
    int level;
};

/* 模拟nginx文件 */
struct ngx_file_s {
    int fd;
    ngx_str_t name;
    ngx_log_t *log;
};

/* 模拟nginx缓冲区 */
struct ngx_buf_s {
    u_char *pos;
    u_char *last;
    off_t   file_pos;
    off_t   file_last;
    u_char *start;
    u_char *end;
    ngx_file_t *file;
    unsigned in_file:1;
    unsigned temporary:1;
    unsigned memory:1;
    unsigned mmap:1;
    unsigned recycled:1;
    unsigned in_file_only:1;
    unsigned flush:1;
    unsigned sync:1;
    unsigned last_buf:1;
    unsigned last_in_chain:1;
};

/* 模拟nginx链表 */
struct ngx_chain_s {
    ngx_buf_t    *buf;
    ngx_chain_t  *next;
};

/* 模拟nginx连接 */
struct ngx_connection_s {
    ngx_log_t *log;
};

/* 模拟nginx请求体 */
typedef struct {
    ngx_chain_t *bufs;
    ngx_buf_t   *buf;
} ngx_http_request_body_t;

/* 模拟nginx配置 */
struct ngx_conf_s {
    ngx_array_t *args;
    ngx_pool_t  *pool;
};

/* 模拟nginx请求 */
struct ngx_http_request_s {
    ngx_connection_t         *connection;
    ngx_pool_t               *pool;
    ngx_http_request_body_t  *request_body;
};

/* 包含我们要测试的模块代码 */
#define NGX_OK          0
#define NGX_ERROR      -1
#define NGX_CONF_ERROR  "error"
#define NGX_CONF_OK     "ok"

#define NGX_LOG_DEBUG_HTTP  7
#define NGX_LOG_WARN        4
#define NGX_LOG_ERR         3
#define NGX_LOG_EMERG       0

typedef unsigned int ngx_uint_t;
typedef int ngx_int_t;
typedef unsigned char u_char;
typedef long off_t;
typedef long ssize_t;

/* 模拟nginx函数 */
static void* test_pool_memory[10000];
static int test_pool_index = 0;

void* ngx_palloc(ngx_pool_t *pool, size_t size) {
    if (test_pool_index >= 9999) return NULL;
    test_pool_memory[test_pool_index] = malloc(size);
    return test_pool_memory[test_pool_index++];
}

void* ngx_pcalloc(ngx_pool_t *pool, size_t size) {
    void *p = ngx_palloc(pool, size);
    if (p) memset(p, 0, size);
    return p;
}

void ngx_memzero(void *buf, size_t n) {
    memset(buf, 0, n);
}

void ngx_memcpy(void *dst, const void *src, size_t n) {
    memcpy(dst, src, n);
}

size_t ngx_strlen(const char *s) {
    return strlen(s);
}

size_t ngx_buf_size(ngx_buf_t *b) {
    if (b->in_file) {
        return b->file_last - b->file_pos;
    }
    return b->last - b->pos;
}

ssize_t ngx_read_file(ngx_file_t *file, u_char *buf, size_t size, off_t offset) {
    ssize_t n = pread(file->fd, buf, size, offset);
    return n;
}

u_char* ngx_sprintf(u_char *buf, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int len = vsprintf((char*)buf, fmt, args);
    va_end(args);
    return buf + len;
}

uint32_t ngx_crc32_long(u_char *p, size_t len) {
    uint32_t crc = 0;
    for (size_t i = 0; i < len; i++) {
        crc = (crc << 8) ^ p[i];
    }
    return crc;
}

void ngx_log_error(ngx_uint_t level, ngx_log_t *log, ngx_int_t err, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    printf("[LOG %u] ", level);
    vprintf(fmt, args);
    printf("\n");
    va_end(args);
}

void ngx_log_debug2(ngx_uint_t level, ngx_log_t *log, ngx_int_t err, const char *fmt, ...) {
    // Debug日志在测试中可以忽略
}

void ngx_conf_log_error(ngx_uint_t level, ngx_conf_t *cf, ngx_int_t err, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    printf("[CONF LOG %u] ", level);
    vprintf(fmt, args);
    printf("\n");
    va_end(args);
}

int ngx_strcmp(const u_char *s1, const u_char *s2) {
    return strcmp((char*)s1, (char*)s2);
}

int ngx_memn2cmp(u_char *s1, u_char *s2, size_t n1, size_t n2) {
    if (n1 != n2) return n1 - n2;
    return memcmp(s1, s2, n1);
}

/* 包含要测试的核心函数声明 */
#include <cjson/cJSON.h>

/* 从模块中复制需要测试的结构体定义 */
typedef struct {
    ngx_str_t                           json_field;
    ngx_uint_t                          consistent_hash;
    void                               *points;
} ngx_http_upstream_json_hash_srv_conf_t;

/* 声明要测试的函数 */
static ngx_str_t *ngx_http_upstream_extract_json_field(ngx_http_request_t *r, ngx_str_t *field_name);
static char *ngx_http_upstream_json_hash(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);

/* 这里包含实际的函数实现（从模块文件中复制） */
static ngx_str_t *
ngx_http_upstream_extract_json_field(ngx_http_request_t *r, ngx_str_t *field_name)
{
    ngx_chain_t     *cl;
    ngx_buf_t       *buf;
    u_char          *body_data, *p;
    size_t           body_len = 0, len;
    cJSON           *json;
    cJSON           *field;
    ngx_str_t       *result;
    char            *field_str, *field_value;
    ngx_file_t       file;
    ssize_t          n;

    if (r->request_body == NULL || r->request_body->bufs == NULL) {
        ngx_log_error(NGX_LOG_WARN, r->connection->log, 0,
                      "request body is empty");
        return NULL;
    }

    /* 计算body总长度 */
    for (cl = r->request_body->bufs; cl; cl = cl->next) {
        body_len += ngx_buf_size(cl->buf);
    }

    if (body_len == 0) {
        ngx_log_error(NGX_LOG_WARN, r->connection->log, 0,
                      "request body length is zero");
        return NULL;
    }

    /* 限制最大JSON大小，防止内存攻击 */
    if (body_len > 1024 * 1024) { /* 1MB limit */
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                      "request body too large: %uz", body_len);
        return NULL;
    }

    /* 分配内存并复制body数据 */
    body_data = ngx_palloc(r->pool, body_len + 1);
    if (body_data == NULL) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                      "failed to allocate memory for request body");
        return NULL;
    }

    p = body_data;
    for (cl = r->request_body->bufs; cl; cl = cl->next) {
        buf = cl->buf;
        if (buf->in_file) {
            /* 处理存储在文件中的数据 */
            ngx_memzero(&file, sizeof(ngx_file_t));
            file.fd = buf->file->fd;
            file.name = buf->file->name;
            file.log = r->connection->log;
            
            len = buf->file_last - buf->file_pos;
            n = ngx_read_file(&file, p, len, buf->file_pos);
            if (n == NGX_ERROR || (size_t) n != len) {
                ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                              "failed to read request body from file");
                return NULL;
            }
            p += len;
        } else {
            len = buf->last - buf->pos;
            if (p + len > body_data + body_len) {
                ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                              "buffer overflow detected");
                return NULL;
            }
            ngx_memcpy(p, buf->pos, len);
            p += len;
        }
    }
    *p = '\0';

    /* 解析JSON */
    json = cJSON_Parse((char *) body_data);
    if (json == NULL) {
        const char *error_ptr = cJSON_GetErrorPtr();
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                      "failed to parse JSON from request body, error: %s",
                      error_ptr ? error_ptr : "unknown");
        return NULL;
    }

    /* 提取指定字段 */
    field_str = ngx_palloc(r->pool, field_name->len + 1);
    if (field_str == NULL) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                      "failed to allocate memory for field name");
        cJSON_Delete(json);
        return NULL;
    }
    ngx_memcpy(field_str, field_name->data, field_name->len);
    field_str[field_name->len] = '\0';

    field = cJSON_GetObjectItem(json, field_str);
    if (field == NULL) {
        ngx_log_error(NGX_LOG_WARN, r->connection->log, 0,
                      "field '%s' not found in JSON", field_str);
        cJSON_Delete(json);
        return NULL;
    }

    /* 支持多种类型的字段 */
    if (cJSON_IsString(field)) {
        field_value = cJSON_GetStringValue(field);
        if (field_value == NULL) {
            ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                          "failed to get string value for field '%s'", field_str);
            cJSON_Delete(json);
            return NULL;
        }
    } else if (cJSON_IsNumber(field)) {
        /* 将数字转换为字符串 */
        double num_value = cJSON_GetNumberValue(field);
        field_str = ngx_palloc(r->pool, 32); /* 足够存储数字字符串 */
        if (field_str == NULL) {
            cJSON_Delete(json);
            return NULL;
        }
        ngx_sprintf((u_char *)field_str, "%.0f", num_value);
        field_value = field_str;
    } else {
        ngx_log_error(NGX_LOG_WARN, r->connection->log, 0,
                      "field '%s' is not a string or number", field_str);
        cJSON_Delete(json);
        return NULL;
    }

    /* 创建结果字符串 */
    result = ngx_palloc(r->pool, sizeof(ngx_str_t));
    if (result == NULL) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                      "failed to allocate memory for result");
        cJSON_Delete(json);
        return NULL;
    }

    result->len = ngx_strlen(field_value);
    if (result->len == 0) {
        ngx_log_error(NGX_LOG_WARN, r->connection->log, 0,
                      "field '%s' has empty value", field_name->data);
        cJSON_Delete(json);
        return NULL;
    }

    result->data = ngx_palloc(r->pool, result->len);
    if (result->data == NULL) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                      "failed to allocate memory for result data");
        cJSON_Delete(json);
        return NULL;
    }
    ngx_memcpy(result->data, field_value, result->len);

    cJSON_Delete(json);
    return result;
}

/* 简化版配置解析函数用于测试 */
static char *
ngx_http_upstream_json_hash(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_upstream_json_hash_srv_conf_t *jhcf = conf;
    ngx_str_t *value;

    value = cf->args->elts;

    if (jhcf->json_field.data) {
        return "is duplicate";
    }

    /* 验证字段名不为空 */
    if (value[1].len == 0) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                          "json field name cannot be empty");
        return NGX_CONF_ERROR;
    }

    /* 验证字段名长度合理 */
    if (value[1].len > 256) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                          "json field name too long: %uz", value[1].len);
        return NGX_CONF_ERROR;
    }

    jhcf->json_field = value[1];

    if (cf->args->nelts == 3) {
        if (ngx_strcmp(value[2].data, (u_char*)"consistent") == 0) {
            jhcf->consistent_hash = 1;
        } else {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                              "invalid parameter \"%V\"", &value[2]);
            return NGX_CONF_ERROR;
        }
    }

    return NGX_CONF_OK;
}

/* 测试辅助函数 */
static void cleanup_test_pool() {
    for (int i = 0; i < test_pool_index; i++) {
        free(test_pool_memory[i]);
    }
    test_pool_index = 0;
}

static ngx_http_request_t* create_test_request(const char* json_body) {
    ngx_http_request_t *r = malloc(sizeof(ngx_http_request_t));
    ngx_connection_t *c = malloc(sizeof(ngx_connection_t));
    ngx_log_t *log = malloc(sizeof(ngx_log_t));
    ngx_pool_t *pool = malloc(sizeof(ngx_pool_t));
    ngx_http_request_body_t *body = malloc(sizeof(ngx_http_request_body_t));
    ngx_chain_t *chain = malloc(sizeof(ngx_chain_t));
    ngx_buf_t *buf = malloc(sizeof(ngx_buf_t));

    memset(r, 0, sizeof(ngx_http_request_t));
    memset(c, 0, sizeof(ngx_connection_t));
    memset(log, 0, sizeof(ngx_log_t));
    memset(pool, 0, sizeof(ngx_pool_t));
    memset(body, 0, sizeof(ngx_http_request_body_t));
    memset(chain, 0, sizeof(ngx_chain_t));
    memset(buf, 0, sizeof(ngx_buf_t));

    c->log = log;
    r->connection = c;
    r->pool = pool;
    r->request_body = body;
    body->bufs = chain;
    chain->buf = buf;
    chain->next = NULL;

    if (json_body && strlen(json_body) > 0) {
        buf->pos = (u_char*)json_body;
        buf->last = (u_char*)(json_body + strlen(json_body));
        buf->memory = 1;
        buf->in_file = 0;
    }

    return r;
}

static void free_test_request(ngx_http_request_t *r) {
    if (r) {
        if (r->connection) {
            if (r->connection->log) free(r->connection->log);
            free(r->connection);
        }
        if (r->pool) free(r->pool);
        if (r->request_body) {
            if (r->request_body->bufs) {
                if (r->request_body->bufs->buf) free(r->request_body->bufs->buf);
                free(r->request_body->bufs);
            }
            free(r->request_body);
        }
        free(r);
    }
}

/* 测试用例 */
void test_normal_json_string_field() {
    printf("测试用例 1: 正常JSON字符串字段提取\n");
    
    ngx_http_request_t *r = create_test_request("{\"user_id\":\"12345\",\"name\":\"test\"}");
    ngx_str_t field_name = {7, (u_char*)"user_id"};
    
    ngx_str_t *result = ngx_http_upstream_extract_json_field(r, &field_name);
    
    assert(result != NULL);
    assert(result->len == 5);
    assert(memcmp(result->data, "12345", 5) == 0);
    
    printf("✓ 正常JSON字符串字段提取测试通过\n");
    
    free_test_request(r);
    cleanup_test_pool();
}

void test_normal_json_number_field() {
    printf("测试用例 2: 正常JSON数字字段提取\n");
    
    ngx_http_request_t *r = create_test_request("{\"user_id\":12345,\"score\":88.5}");
    ngx_str_t field_name = {7, (u_char*)"user_id"};
    
    ngx_str_t *result = ngx_http_upstream_extract_json_field(r, &field_name);
    
    assert(result != NULL);
    assert(result->len == 5);
    assert(memcmp(result->data, "12345", 5) == 0);
    
    printf("✓ 正常JSON数字字段提取测试通过\n");
    
    free_test_request(r);
    cleanup_test_pool();
}

void test_empty_body() {
    printf("测试用例 3: 空请求体\n");
    
    ngx_http_request_t *r = create_test_request("");
    ngx_str_t field_name = {7, (u_char*)"user_id"};
    
    ngx_str_t *result = ngx_http_upstream_extract_json_field(r, &field_name);
    
    assert(result == NULL);
    
    printf("✓ 空请求体测试通过\n");
    
    free_test_request(r);
    cleanup_test_pool();
}

void test_null_body() {
    printf("测试用例 4: NULL请求体\n");
    
    ngx_http_request_t *r = create_test_request(NULL);
    r->request_body = NULL;
    ngx_str_t field_name = {7, (u_char*)"user_id"};
    
    ngx_str_t *result = ngx_http_upstream_extract_json_field(r, &field_name);
    
    assert(result == NULL);
    
    printf("✓ NULL请求体测试通过\n");
    
    free_test_request(r);
    cleanup_test_pool();
}

void test_invalid_json() {
    printf("测试用例 5: 无效JSON格式\n");
    
    ngx_http_request_t *r = create_test_request("{invalid json format");
    ngx_str_t field_name = {7, (u_char*)"user_id"};
    
    ngx_str_t *result = ngx_http_upstream_extract_json_field(r, &field_name);
    
    assert(result == NULL);
    
    printf("✓ 无效JSON格式测试通过\n");
    
    free_test_request(r);
    cleanup_test_pool();
}

void test_missing_field() {
    printf("测试用例 6: 字段不存在\n");
    
    ngx_http_request_t *r = create_test_request("{\"name\":\"test\",\"age\":25}");
    ngx_str_t field_name = {7, (u_char*)"user_id"};
    
    ngx_str_t *result = ngx_http_upstream_extract_json_field(r, &field_name);
    
    assert(result == NULL);
    
    printf("✓ 字段不存在测试通过\n");
    
    free_test_request(r);
    cleanup_test_pool();
}

void test_empty_field_value() {
    printf("测试用例 7: 字段值为空\n");
    
    ngx_http_request_t *r = create_test_request("{\"user_id\":\"\",\"name\":\"test\"}");
    ngx_str_t field_name = {7, (u_char*)"user_id"};
    
    ngx_str_t *result = ngx_http_upstream_extract_json_field(r, &field_name);
    
    assert(result == NULL);
    
    printf("✓ 字段值为空测试通过\n");
    
    free_test_request(r);
    cleanup_test_pool();
}

void test_unsupported_field_type() {
    printf("测试用例 8: 不支持的字段类型（数组）\n");
    
    ngx_http_request_t *r = create_test_request("{\"user_id\":[1,2,3],\"name\":\"test\"}");
    ngx_str_t field_name = {7, (u_char*)"user_id"};
    
    ngx_str_t *result = ngx_http_upstream_extract_json_field(r, &field_name);
    
    assert(result == NULL);
    
    printf("✓ 不支持的字段类型测试通过\n");
    
    free_test_request(r);
    cleanup_test_pool();
}

void test_large_body() {
    printf("测试用例 9: 超大请求体（模拟）\n");
    
    // 创建一个模拟超大body的请求
    ngx_http_request_t *r = create_test_request("{\"user_id\":\"12345\"}");
    
    // 手动设置buf大小为超过限制
    ngx_buf_t *buf = r->request_body->bufs->buf;
    buf->last = buf->pos + (1024 * 1024 + 1); // 超过1MB限制
    
    ngx_str_t field_name = {7, (u_char*)"user_id"};
    
    ngx_str_t *result = ngx_http_upstream_extract_json_field(r, &field_name);
    
    assert(result == NULL);
    
    printf("✓ 超大请求体测试通过\n");
    
    free_test_request(r);
    cleanup_test_pool();
}

void test_file_based_body() {
    printf("测试用例 10: 文件存储的请求体\n");
    
    // 创建临时文件
    char temp_file[] = "/tmp/test_body_XXXXXX";
    int fd = mkstemp(temp_file);
    assert(fd != -1);
    
    const char *json_content = "{\"user_id\":\"54321\",\"name\":\"file_test\"}";
    write(fd, json_content, strlen(json_content));
    
    ngx_http_request_t *r = create_test_request(NULL);
    ngx_buf_t *buf = r->request_body->bufs->buf;
    ngx_file_t *file = malloc(sizeof(ngx_file_t));
    
    memset(file, 0, sizeof(ngx_file_t));
    file->fd = fd;
    file->name.data = (u_char*)temp_file;
    file->name.len = strlen(temp_file);
    file->log = r->connection->log;
    
    buf->file = file;
    buf->in_file = 1;
    buf->file_pos = 0;
    buf->file_last = strlen(json_content);
    
    ngx_str_t field_name = {7, (u_char*)"user_id"};
    
    ngx_str_t *result = ngx_http_upstream_extract_json_field(r, &field_name);
    
    assert(result != NULL);
    assert(result->len == 5);
    assert(memcmp(result->data, "54321", 5) == 0);
    
    printf("✓ 文件存储的请求体测试通过\n");
    
    close(fd);
    unlink(temp_file);
    free(file);
    free_test_request(r);
    cleanup_test_pool();
}

void test_config_validation() {
    printf("测试用例 11: 配置验证\n");
    
    ngx_conf_t cf;
    ngx_array_t args;
    ngx_str_t values[3];
    ngx_pool_t pool;
    ngx_http_upstream_json_hash_srv_conf_t conf;
    
    memset(&cf, 0, sizeof(cf));
    memset(&args, 0, sizeof(args));
    memset(&conf, 0, sizeof(conf));
    
    cf.args = &args;
    cf.pool = &pool;
    args.elts = values;
    
    // 测试1: 正常配置
    values[0].data = (u_char*)"json_hash";
    values[0].len = 9;
    values[1].data = (u_char*)"user_id";
    values[1].len = 7;
    args.nelts = 2;
    
    char *result = ngx_http_upstream_json_hash(&cf, NULL, &conf);
    assert(strcmp(result, NGX_CONF_OK) == 0);
    
    // 重置配置
    memset(&conf, 0, sizeof(conf));
    
    // 测试2: 空字段名
    values[1].data = (u_char*)"";
    values[1].len = 0;
    
    result = ngx_http_upstream_json_hash(&cf, NULL, &conf);
    assert(strcmp(result, NGX_CONF_ERROR) == 0);
    
    // 重置配置
    memset(&conf, 0, sizeof(conf));
    
    // 测试3: 字段名过长
    char long_field[300];
    memset(long_field, 'a', 299);
    long_field[299] = '\0';
    values[1].data = (u_char*)long_field;
    values[1].len = 299;
    
    result = ngx_http_upstream_json_hash(&cf, NULL, &conf);
    assert(strcmp(result, NGX_CONF_ERROR) == 0);
    
    // 重置配置
    memset(&conf, 0, sizeof(conf));
    
    // 测试4: consistent参数
    values[1].data = (u_char*)"user_id";
    values[1].len = 7;
    values[2].data = (u_char*)"consistent";
    values[2].len = 10;
    args.nelts = 3;
    
    result = ngx_http_upstream_json_hash(&cf, NULL, &conf);
    assert(strcmp(result, NGX_CONF_OK) == 0);
    assert(conf.consistent_hash == 1);
    
    // 重置配置
    memset(&conf, 0, sizeof(conf));
    
    // 测试5: 无效参数
    values[2].data = (u_char*)"invalid";
    values[2].len = 7;
    
    result = ngx_http_upstream_json_hash(&cf, NULL, &conf);
    assert(strcmp(result, NGX_CONF_ERROR) == 0);
    
    printf("✓ 配置验证测试通过\n");
}

void test_nested_json() {
    printf("测试用例 12: 嵌套JSON（顶级字段）\n");
    
    ngx_http_request_t *r = create_test_request("{\"user\":{\"id\":\"12345\",\"name\":\"test\"},\"user_id\":\"67890\"}");
    ngx_str_t field_name = {7, (u_char*)"user_id"};
    
    ngx_str_t *result = ngx_http_upstream_extract_json_field(r, &field_name);
    
    assert(result != NULL);
    assert(result->len == 5);
    assert(memcmp(result->data, "67890", 5) == 0);
    
    printf("✓ 嵌套JSON测试通过\n");
    
    free_test_request(r);
    cleanup_test_pool();
}

void test_special_characters() {
    printf("测试用例 13: 特殊字符处理\n");
    
    ngx_http_request_t *r = create_test_request("{\"user_id\":\"test@123.com\",\"special\":\"中文测试\"}");
    ngx_str_t field_name = {7, (u_char*)"user_id"};
    
    ngx_str_t *result = ngx_http_upstream_extract_json_field(r, &field_name);
    
    assert(result != NULL);
    assert(result->len == 12);
    assert(memcmp(result->data, "test@123.com", 12) == 0);
    
    printf("✓ 特殊字符处理测试通过\n");
    
    free_test_request(r);
    cleanup_test_pool();
}

void test_boolean_field() {
    printf("测试用例 14: 布尔类型字段\n");
    
    ngx_http_request_t *r = create_test_request("{\"user_id\":true,\"active\":false}");
    ngx_str_t field_name = {7, (u_char*)"user_id"};
    
    ngx_str_t *result = ngx_http_upstream_extract_json_field(r, &field_name);
    
    assert(result == NULL); // 布尔类型不支持
    
    printf("✓ 布尔类型字段测试通过\n");
    
    free_test_request(r);
    cleanup_test_pool();
}

void test_null_field() {
    printf("测试用例 15: NULL字段值\n");
    
    ngx_http_request_t *r = create_test_request("{\"user_id\":null,\"name\":\"test\"}");
    ngx_str_t field_name = {7, (u_char*)"user_id"};
    
    ngx_str_t *result = ngx_http_upstream_extract_json_field(r, &field_name);
    
    assert(result == NULL); // NULL值不支持
    
    printf("✓ NULL字段值测试通过\n");
    
    free_test_request(r);
    cleanup_test_pool();
}

int main() {
    printf("开始执行 nginx upstream json hash 模块单元测试\n");
    printf("==============================================\n");
    
    test_normal_json_string_field();
    test_normal_json_number_field();
    test_empty_body();
    test_null_body();
    test_invalid_json();
    test_missing_field();
    test_empty_field_value();
    test_unsupported_field_type();
    test_large_body();
    test_file_based_body();
    test_config_validation();
    test_nested_json();
    test_special_characters();
    test_boolean_field();
    test_null_field();
    
    printf("==============================================\n");
    printf("所有测试用例通过！模块功能正常 ✓\n");
    
    return 0;
} 
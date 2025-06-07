/*
 * Copyright (C) nginx-json-hash-module
 */

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include <cjson/cJSON.h>
#include <limits.h>
#include <stdint.h>

/* 哈希算法类型 */
#define NGX_HTTP_UPSTREAM_JSON_HASH_CRC32    0
#define NGX_HTTP_UPSTREAM_JSON_HASH_MURMUR   1
#define NGX_HTTP_UPSTREAM_JSON_HASH_XXHASH   2

/* 默认配置值 */
#define NGX_HTTP_UPSTREAM_JSON_HASH_DEFAULT_VIRTUAL_NODES  160
#define NGX_HTTP_UPSTREAM_JSON_HASH_DEFAULT_MAX_DEPTH      32


typedef struct {
    uint32_t                            hash;
    ngx_str_t                          *server;
} ngx_http_upstream_chash_point_t;


typedef struct {
    ngx_uint_t                          number;
    ngx_http_upstream_chash_point_t     point[1];
} ngx_http_upstream_chash_points_t;


typedef struct {
    ngx_str_t                           json_field;
    ngx_uint_t                          consistent_hash;
    ngx_uint_t                          virtual_nodes;        /* 虚拟节点数，可配置 */
    ngx_uint_t                          hash_method;          /* 哈希算法类型 */
    ngx_uint_t                          max_json_depth;       /* JSON最大嵌套深度 */
    ngx_uint_t                          check_content_type;   /* 是否检查Content-Type */
    ngx_str_t                           fallback_key;         /* 失败时的备用哈希键 */
    size_t                              max_json_size;        /* 最大JSON体大小 */
    size_t                              max_virtual_memory;   /* 虚拟节点最大内存限制 */
    ngx_http_upstream_chash_points_t   *points;
    
    /* 性能统计信息 */
    ngx_atomic_t                        hash_requests;        /* 哈希请求总数 */
    ngx_atomic_t                        hash_failures;        /* 哈希失败次数 */
    ngx_atomic_t                        json_parse_time;      /* JSON解析累计时间(微秒) */
    ngx_atomic_t                        content_type_checks;  /* Content-Type检查次数 */
    ngx_msec_t                          stats_interval;      /* 统计日志间隔时间(毫秒) */
    ngx_atomic_t                        stats_resets;        /* 统计重置次数 */
#if (NGX_HTTP_UPSTREAM_ZONE)
    ngx_uint_t                          config;
#endif
} ngx_http_upstream_json_hash_srv_conf_t;


typedef struct {
    /* the round robin data must be first */
    ngx_http_upstream_rr_peer_data_t    rrp;
    ngx_http_upstream_json_hash_srv_conf_t *conf;
    ngx_str_t                           hash_key;
    ngx_uint_t                          tries;
    ngx_uint_t                          rehash;
    uint32_t                            hash;
    ngx_event_get_peer_pt               get_rr_peer;
} ngx_http_upstream_json_hash_peer_data_t;


static ngx_int_t ngx_http_upstream_init_json_hash(ngx_conf_t *cf,
    ngx_http_upstream_srv_conf_t *us);
static ngx_int_t ngx_http_upstream_init_json_hash_peer(ngx_http_request_t *r,
    ngx_http_upstream_srv_conf_t *us);
static ngx_int_t ngx_http_upstream_get_json_hash_peer(ngx_peer_connection_t *pc,
    void *data);

static ngx_int_t ngx_http_upstream_init_json_chash(ngx_conf_t *cf,
    ngx_http_upstream_srv_conf_t *us);

static int ngx_libc_cdecl
    ngx_http_upstream_json_chash_cmp_points(const void *one, const void *two);
static ngx_uint_t ngx_http_upstream_find_json_chash_point(
    ngx_http_upstream_chash_points_t *points, uint32_t hash);

static ngx_int_t ngx_http_upstream_get_json_chash_peer(ngx_peer_connection_t *pc,
    void *data);

static ngx_str_t *ngx_http_upstream_extract_json_field(ngx_http_request_t *r,
    ngx_str_t *field_name);
static void ngx_http_upstream_json_hash_body_handler(ngx_http_request_t *r);

static uint32_t ngx_http_upstream_json_hash_murmur3(const void *key, int len, uint32_t seed);
static uint32_t ngx_http_upstream_json_hash_calculate(u_char *data, size_t len, ngx_uint_t method);
static ngx_int_t ngx_http_upstream_json_validate_depth(cJSON *json, ngx_uint_t max_depth, ngx_uint_t current_depth, ngx_log_t *log);
static ngx_int_t ngx_http_upstream_json_check_content_type(ngx_http_request_t *r);
static ngx_str_t *ngx_http_upstream_extract_json_field(ngx_http_request_t *r, ngx_str_t *field_name);
static void ngx_http_upstream_json_hash_log_stats(ngx_http_request_t *r, ngx_http_upstream_json_hash_srv_conf_t *jhcf);

static void *ngx_http_upstream_json_hash_create_conf(ngx_conf_t *cf);
static char *ngx_http_upstream_json_hash(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);


static char *ngx_http_upstream_json_hash_virtual_nodes(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static char *ngx_http_upstream_json_hash_method(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static char *ngx_http_upstream_json_hash_max_depth(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static char *ngx_http_upstream_json_hash_check_content_type(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static char *ngx_http_upstream_json_hash_fallback_key(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static char *ngx_http_upstream_json_hash_stats_interval(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static char *ngx_http_upstream_json_hash_max_size(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static char *ngx_http_upstream_json_hash_max_virtual_memory(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);

static ngx_command_t  ngx_http_upstream_json_hash_commands[] = {

    { ngx_string("json_hash"),
      NGX_HTTP_UPS_CONF|NGX_CONF_TAKE12,
      ngx_http_upstream_json_hash,
      NGX_HTTP_SRV_CONF_OFFSET,
      0,
      NULL },

    { ngx_string("json_hash_virtual_nodes"),
      NGX_HTTP_UPS_CONF|NGX_CONF_TAKE1,
      ngx_http_upstream_json_hash_virtual_nodes,
      NGX_HTTP_SRV_CONF_OFFSET,
      0,
      NULL },

    { ngx_string("json_hash_method"),
      NGX_HTTP_UPS_CONF|NGX_CONF_TAKE1,
      ngx_http_upstream_json_hash_method,
      NGX_HTTP_SRV_CONF_OFFSET,
      0,
      NULL },

    { ngx_string("json_hash_max_depth"),
      NGX_HTTP_UPS_CONF|NGX_CONF_TAKE1,
      ngx_http_upstream_json_hash_max_depth,
      NGX_HTTP_SRV_CONF_OFFSET,
      0,
      NULL },

    { ngx_string("json_hash_check_content_type"),
      NGX_HTTP_UPS_CONF|NGX_CONF_TAKE1,
      ngx_http_upstream_json_hash_check_content_type,
      NGX_HTTP_SRV_CONF_OFFSET,
      0,
      NULL },

    { ngx_string("json_hash_fallback_key"),
      NGX_HTTP_UPS_CONF|NGX_CONF_TAKE1,
      ngx_http_upstream_json_hash_fallback_key,
      NGX_HTTP_SRV_CONF_OFFSET,
      0,
      NULL },

    { ngx_string("json_hash_stats_interval"),
      NGX_HTTP_UPS_CONF|NGX_CONF_TAKE1,
      ngx_http_upstream_json_hash_stats_interval,
      NGX_HTTP_SRV_CONF_OFFSET,
      0,
      NULL },

    { ngx_string("json_hash_max_size"),
      NGX_HTTP_UPS_CONF|NGX_CONF_TAKE1,
      ngx_http_upstream_json_hash_max_size,
      NGX_HTTP_SRV_CONF_OFFSET,
      0,
      NULL },

    { ngx_string("json_hash_max_virtual_memory"),
      NGX_HTTP_UPS_CONF|NGX_CONF_TAKE1,
      ngx_http_upstream_json_hash_max_virtual_memory,
      NGX_HTTP_SRV_CONF_OFFSET,
      0,
      NULL },

      ngx_null_command
};


static ngx_http_module_t  ngx_http_upstream_json_hash_module_ctx = {
    NULL,                                  /* preconfiguration */
    NULL,                                  /* postconfiguration */

    NULL,                                  /* create main configuration */
    NULL,                                  /* init main configuration */

    ngx_http_upstream_json_hash_create_conf, /* create server configuration */
    NULL,                                  /* merge server configuration */

    NULL,                                  /* create location configuration */
    NULL                                   /* merge location configuration */
};


ngx_module_t  ngx_http_upstream_json_hash_module = {
    NGX_MODULE_V1,
    &ngx_http_upstream_json_hash_module_ctx, /* module context */
    ngx_http_upstream_json_hash_commands,   /* module directives */
    NGX_HTTP_MODULE,                       /* module type */
    NULL,                                  /* init master */
    NULL,                                  /* init module */
    NULL,                                  /* init process */
    NULL,                                  /* init thread */
    NULL,                                  /* exit thread */
    NULL,                                  /* exit process */
    NULL,                                  /* exit master */
    NGX_MODULE_V1_PADDING
};


static ngx_int_t
ngx_http_upstream_init_json_hash(ngx_conf_t *cf, ngx_http_upstream_srv_conf_t *us)
{
    ngx_http_upstream_json_hash_srv_conf_t  *jhcf;

    jhcf = ngx_http_conf_upstream_srv_conf(us, ngx_http_upstream_json_hash_module);

    if (ngx_http_upstream_init_round_robin(cf, us) != NGX_OK) {
        return NGX_ERROR;
    }

    us->peer.init = ngx_http_upstream_init_json_hash_peer;

    if (jhcf->consistent_hash) {
        if (ngx_http_upstream_init_json_chash(cf, us) != NGX_OK) {
            return NGX_ERROR;
        }
    }

    return NGX_OK;
}


static ngx_int_t
ngx_http_upstream_init_json_hash_peer(ngx_http_request_t *r,
    ngx_http_upstream_srv_conf_t *us)
{
    ngx_http_upstream_json_hash_srv_conf_t  *jhcf;
    ngx_http_upstream_json_hash_peer_data_t *jhp;

    jhp = ngx_palloc(r->pool, sizeof(ngx_http_upstream_json_hash_peer_data_t));
    if (jhp == NULL) {
        return NGX_ERROR;
    }

    r->upstream->peer.data = &jhp->rrp;

    if (ngx_http_upstream_init_round_robin_peer(r, us) != NGX_OK) {
        return NGX_ERROR;
    }

    jhcf = ngx_http_conf_upstream_srv_conf(us, ngx_http_upstream_json_hash_module);

    if (jhcf->consistent_hash) {
        r->upstream->peer.get = ngx_http_upstream_get_json_chash_peer;
    } else {
        r->upstream->peer.get = ngx_http_upstream_get_json_hash_peer;
    }

    jhp->conf = jhcf;
    jhp->tries = 0;
    jhp->rehash = 0;
    jhp->hash = 0;
    jhp->get_rr_peer = ngx_http_upstream_get_round_robin_peer;

    /* 读取请求体 */
    if (ngx_http_read_client_request_body(r, ngx_http_upstream_json_hash_body_handler) != NGX_OK) {
        return NGX_ERROR;
    }

    return NGX_OK;
}


static void
ngx_http_upstream_json_hash_body_handler(ngx_http_request_t *r)
{
    ngx_http_upstream_json_hash_peer_data_t *jhp;
    ngx_http_upstream_json_hash_srv_conf_t  *jhcf;
    ngx_str_t                               *hash_key;
    ngx_msec_t                               start_time;

    jhp = (ngx_http_upstream_json_hash_peer_data_t *) r->upstream->peer.data;
    jhcf = jhp->conf;

    /* 统计：增加哈希请求计数 */
    ngx_atomic_fetch_add(&jhcf->hash_requests, 1);
    start_time = ngx_current_msec;

    /* 从JSON body中提取字段值 */
    hash_key = ngx_http_upstream_extract_json_field(r, &jhcf->json_field);
    
    /* 统计：记录JSON解析时间 */
    ngx_atomic_fetch_add(&jhcf->json_parse_time, ngx_current_msec - start_time);
    
    if (hash_key && hash_key->len > 0) {
        jhp->hash_key = *hash_key;
        ngx_log_debug2(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                       "json hash key: \"%V\" from field: \"%V\"",
                       &jhp->hash_key, &jhcf->json_field);
    } else {
        /* 统计：哈希提取失败 */
        ngx_atomic_fetch_add(&jhcf->hash_failures, 1);
        
        /* 如果提取失败，使用配置的fallback key */
        jhp->hash_key = jhcf->fallback_key;
        ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                       "json hash key extraction failed, using fallback key: \"%V\"",
                       &jhp->hash_key);
    }
    
    /* 定期记录统计信息 */
    ngx_http_upstream_json_hash_log_stats(r, jhcf);
}


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
    ngx_http_upstream_json_hash_srv_conf_t *jhcf;

    jhcf = ngx_http_get_module_srv_conf(r, ngx_http_upstream_json_hash_module);

    /* 检查Content-Type */
    if (jhcf->check_content_type) {
        ngx_atomic_fetch_add(&jhcf->content_type_checks, 1);
        if (ngx_http_upstream_json_check_content_type(r) != NGX_OK) {
            ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                           "request content-type is not application/json");
            return NULL;
        }
    }

    if (r->request_body == NULL || r->request_body->bufs == NULL) {
        ngx_log_error(NGX_LOG_WARN, r->connection->log, 0,
                      "request body is empty");
        return NULL;
    }

    /* 计算body总长度，防止溢出 */
    for (cl = r->request_body->bufs; cl; cl = cl->next) {
        size_t len = ngx_buf_size(cl->buf);
        if (SIZE_MAX - body_len < len) {
            ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                          "request body length overflow");
            return NULL;
        }
        body_len += len;
    }

    if (body_len == 0) {
        ngx_log_error(NGX_LOG_WARN, r->connection->log, 0,
                      "request body length is zero");
        return NULL;
    }

    /* 限制最大JSON大小，防止内存攻击 */
    if (body_len > jhcf->max_json_size) {
        ngx_log_error(NGX_LOG_WARN, r->connection->log, 0,
                      "request body too large: %uz, max allowed: %uz", 
                      body_len, jhcf->max_json_size);
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
            
            /* 限制单次文件读取大小，防止阻塞 */
            if (len > 64 * 1024) { /* 64KB limit per read */
                ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                              "file chunk too large: %uz, max allowed: 65536", len);
                return NULL;
            }
            
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
        /* 释放已分配的body_data内存 */
        ngx_pfree(r->pool, body_data);
        return NULL;
    }

    /* 验证JSON深度 */
    if (jhcf->max_json_depth > 0 &&
        ngx_http_upstream_json_validate_depth(json, jhcf->max_json_depth, 0, r->connection->log) != NGX_OK) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                      "JSON nesting depth exceeds limit: %ui", jhcf->max_json_depth);
        cJSON_Delete(json);
        ngx_pfree(r->pool, body_data);
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
        ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                       "field '%s' not found in JSON object", field_str);
        cJSON_Delete(json);
        ngx_pfree(r->pool, body_data);
        return NULL;
    }

    /* 支持多种类型的字段，包括布尔值和null */
    if (cJSON_IsString(field)) {
        field_value = cJSON_GetStringValue(field);
        if (field_value == NULL) {
            ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                          "failed to get string value for field '%s'", field_str);
            cJSON_Delete(json);
            ngx_pfree(r->pool, body_data);
            return NULL;
        }
    } else if (cJSON_IsNumber(field)) {
        /* 将数字转换为字符串，避免精度丢失 */
        double num_value = cJSON_GetNumberValue(field);
        char *num_str = ngx_palloc(r->pool, 64); /* 增加缓冲区大小以防止溢出 */
        if (num_str == NULL) {
            ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                          "failed to allocate memory for number conversion");
            cJSON_Delete(json);
            ngx_pfree(r->pool, body_data);
            return NULL;
        }
        
        /* 检查是否为整数，避免精度丢失 */
        if (num_value == (long long)num_value && num_value >= LLONG_MIN && num_value <= LLONG_MAX) {
            /* 整数处理 */
            ngx_sprintf((u_char *)num_str, "%L", (long long)num_value);
        } else {
            /* 浮点数处理，使用更精确的格式 */
            ngx_sprintf((u_char *)num_str, "%.17g", num_value);
        }
        field_value = num_str;
        ngx_log_debug2(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                       "converted number field '%s' to string: '%s'", field_str, field_value);
    } else if (cJSON_IsBool(field)) {
        /* 将布尔值转换为字符串 */
        field_value = cJSON_IsTrue(field) ? "true" : "false";
        ngx_log_debug2(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                       "converted boolean field '%s' to string: '%s'", field_str, field_value);
    } else if (cJSON_IsNull(field)) {
        /* 将null转换为字符串 */
        field_value = "null";
        ngx_log_debug2(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                       "converted null field '%s' to string: '%s'", field_str, field_value);
    } else {
        /* 尝试将其他类型转换为字符串 */
        char *json_string = cJSON_Print(field);
        if (json_string) {
            size_t json_len = ngx_strlen(json_string);
            field_value = ngx_palloc(r->pool, json_len + 1);
            if (field_value) {
                ngx_memcpy((u_char *)field_value, json_string, json_len + 1);
                free(json_string); /* cJSON_Print使用malloc分配 */
                ngx_log_debug2(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                               "converted complex field '%s' to JSON string: '%s'", field_str, field_value);
            } else {
                free(json_string); /* 修复：确保释放json_string */
                ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                              "failed to allocate memory for field value conversion");
                cJSON_Delete(json);
                ngx_pfree(r->pool, body_data);
                return NULL;
            }
        } else {
            ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                           "field '%s' has unsupported type and cannot be converted to string", field_str);
            cJSON_Delete(json);
            ngx_pfree(r->pool, body_data);
            return NULL;
        }
    }

    /* 创建结果字符串 */
    result = ngx_palloc(r->pool, sizeof(ngx_str_t));
    if (result == NULL) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                      "failed to allocate memory for result");
        cJSON_Delete(json);
        ngx_pfree(r->pool, body_data);  /* 修复：释放body_data */
        return NULL;
    }

    result->len = ngx_strlen(field_value);
    if (result->len == 0) {
        ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                       "field '%V' has empty value, treating as valid hash key", field_name);
        /* 空字符串是有效的哈希键，分配至少1字节以避免NULL指针 */
        result->data = ngx_palloc(r->pool, 1);
        if (result->data == NULL) {
            ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                          "failed to allocate memory for empty result data");
            cJSON_Delete(json);
            ngx_pfree(r->pool, body_data);  /* 修复：释放body_data */
            return NULL;
        }
        result->data[0] = '\0'; /* 确保以null结尾 */
    } else {
        result->data = ngx_palloc(r->pool, result->len);
        if (result->data == NULL) {
            ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                          "failed to allocate memory for result data");
            cJSON_Delete(json);
            ngx_pfree(r->pool, body_data);  /* 修复：释放body_data */
            return NULL;
        }
        ngx_memcpy(result->data, field_value, result->len);
    }

    cJSON_Delete(json);
    return result;
}


/* 字节序安全的读取32位数据 */
static uint32_t
ngx_http_upstream_json_hash_getblock32(const uint8_t *data, int offset)
{
    return (uint32_t)data[offset] | 
           ((uint32_t)data[offset + 1] << 8) | 
           ((uint32_t)data[offset + 2] << 16) | 
           ((uint32_t)data[offset + 3] << 24);
}

/* MurmurHash3算法实现 */
static uint32_t
ngx_http_upstream_json_hash_murmur3(const void *key, int len, uint32_t seed)
{
    const uint8_t *data = (const uint8_t*)key;
    const int nblocks = len / 4;
    uint32_t h1 = seed;
    const uint32_t c1 = 0xcc9e2d51;
    const uint32_t c2 = 0x1b873593;
    
    /* body - 使用字节序安全的方式读取数据 */
    for(int i = 0; i < nblocks; i++) {
        uint32_t k1 = ngx_http_upstream_json_hash_getblock32(data, i * 4);
        
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
        case 3: k1 ^= tail[2] << 16;
        case 2: k1 ^= tail[1] << 8;
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

/* 统一的哈希计算接口 */
static uint32_t
ngx_http_upstream_json_hash_calculate(u_char *data, size_t len, ngx_uint_t method)
{
    switch (method) {
        case NGX_HTTP_UPSTREAM_JSON_HASH_MURMUR:
            return ngx_http_upstream_json_hash_murmur3(data, len, 0);
        case NGX_HTTP_UPSTREAM_JSON_HASH_CRC32:
        default:
            return ngx_crc32_long(data, len);
    }
}

/* JSON深度验证 - 使用栈结构避免递归栈溢出 */
typedef struct {
    cJSON *node;
    ngx_uint_t depth;
} ngx_json_stack_item_t;

static ngx_int_t
ngx_http_upstream_json_validate_depth(cJSON *json, ngx_uint_t max_depth, ngx_uint_t current_depth, ngx_log_t *log)
{
    ngx_json_stack_item_t stack[NGX_HTTP_UPSTREAM_JSON_HASH_DEFAULT_MAX_DEPTH + 1];
    ngx_uint_t stack_top = 0;
    cJSON *child;
    
    /* 检查深度限制 */
    if (max_depth > NGX_HTTP_UPSTREAM_JSON_HASH_DEFAULT_MAX_DEPTH) {
        /* 对于超大深度限制，直接拒绝以避免栈溢出风险 */
        ngx_log_error(NGX_LOG_ERR, log, 0,
                      "max_depth %ui exceeds maximum safe limit %ui, request rejected",
                      max_depth, NGX_HTTP_UPSTREAM_JSON_HASH_DEFAULT_MAX_DEPTH);
        return NGX_ERROR;
    }
    
    /* 使用栈迭代验证深度 */
    stack[stack_top].node = json;
    stack[stack_top].depth = current_depth;
    stack_top++;
    
    while (stack_top > 0) {
        /* 出栈 */
        stack_top--;
        cJSON *current = stack[stack_top].node;
        ngx_uint_t depth = stack[stack_top].depth;
        
        if (depth > max_depth) {
            return NGX_ERROR;
        }
        
        if (cJSON_IsArray(current) || cJSON_IsObject(current)) {
            child = current->child;
            while (child) {
                /* 检查栈空间 */
                if (stack_top >= NGX_HTTP_UPSTREAM_JSON_HASH_DEFAULT_MAX_DEPTH) {
                    return NGX_ERROR;
                }
                
                /* 入栈 */
                stack[stack_top].node = child;
                stack[stack_top].depth = depth + 1;
                stack_top++;
                
                child = child->next;
            }
        }
    }
    
    return NGX_OK;
}

/* Content-Type检查 */
static ngx_int_t
ngx_http_upstream_json_check_content_type(ngx_http_request_t *r)
{
    ngx_str_t *content_type;
    u_char *ct_data;
    size_t ct_len;
    
    if (r->headers_in.content_type == NULL) {
        ngx_log_error(NGX_LOG_DEBUG, r->connection->log, 0,
                      "json_hash: no Content-Type header");
        return NGX_ERROR;
    }
    
    content_type = &r->headers_in.content_type->value;
    ct_data = content_type->data;
    ct_len = content_type->len;
    
    /* 优化：快速路径检查最常见的application/json，支持带参数的Content-Type */
    if (ct_len >= 16) {
        /* 使用位操作优化大小写比较的性能 */
        if ((ct_data[0] | 0x20) == 'a' && (ct_data[1] | 0x20) == 'p' &&
            ngx_strncasecmp(ct_data, (u_char *) "application/json", 16) == 0) {
            /* 检查后面是否跟着分号、空格或结束符 */
            if (ct_len == 16 || ct_data[16] == ';' || ct_data[16] == ' ') {
                return NGX_OK;
            }
        }
    }
    
    /* 次级检查：text/json，支持带参数的Content-Type */
    if (ct_len >= 9) {
        if ((ct_data[0] | 0x20) == 't' && 
            ngx_strncasecmp(ct_data, (u_char *) "text/json", 9) == 0) {
            /* 检查后面是否跟着分号、空格或结束符 */
            if (ct_len == 9 || ct_data[9] == ';' || ct_data[9] == ' ') {
                return NGX_OK;
            }
        }
    }
    
    ngx_log_error(NGX_LOG_DEBUG, r->connection->log, 0,
                  "json_hash: unsupported Content-Type: \"%V\"",
                  content_type);
    return NGX_ERROR;
}


static ngx_int_t
ngx_http_upstream_get_json_hash_peer(ngx_peer_connection_t *pc, void *data)
{
    ngx_http_upstream_json_hash_peer_data_t *jhp = data;

    time_t                        now;
    u_char                        buf[NGX_INT_T_LEN];
    size_t                        size;
    uint32_t                      hash;
    ngx_int_t                     w;
    uintptr_t                     m;
    ngx_uint_t                    n, p;
    ngx_http_upstream_rr_peer_t  *peer;

    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, pc->log, 0,
                   "get json hash peer, try: %ui", pc->tries);

    ngx_http_upstream_rr_peers_rlock(jhp->rrp.peers);

    if (jhp->tries > 20 || jhp->rrp.peers->number < 2 || jhp->hash_key.len == 0) {
        ngx_http_upstream_rr_peers_unlock(jhp->rrp.peers);
        return jhp->get_rr_peer(pc, &jhp->rrp);
    }

    now = ngx_time();

    pc->cached = 0;
    pc->connection = NULL;

    for ( ;; ) {

        hash = ngx_http_upstream_json_hash_calculate(jhp->hash_key.data, jhp->hash_key.len, jhp->conf->hash_method);

        if (jhp->rehash > 0) {
            size = ngx_sprintf(buf, "%uD", jhp->rehash) - buf;
            hash = ngx_http_upstream_json_hash_calculate(buf, size, jhp->conf->hash_method) ^ hash; /* 组合原hash和rehash */
        }

        w = hash % jhp->rrp.peers->total_weight;
        peer = jhp->rrp.peers->peer;
        p = 0;

        while (w >= peer->weight) {
            w -= peer->weight;
            peer = peer->next;
            p++;
        }

        n = p / (8 * sizeof(uintptr_t));
        m = (uintptr_t) 1 << p % (8 * sizeof(uintptr_t));

        if (jhp->rrp.tried[n] & m) {
            goto next;
        }

        ngx_log_debug2(NGX_LOG_DEBUG_HTTP, pc->log, 0,
                       "get json hash peer: %ui %04XA", p, (uint64_t) m);

        if (peer->down) {
            goto next;
        }

        if (peer->max_fails
            && peer->fails >= peer->max_fails
            && now - peer->checked <= peer->fail_timeout)
        {
            goto next;
        }

        if (peer->max_conns && peer->conns >= peer->max_conns) {
            goto next;
        }

        break;

    next:

        if (++jhp->tries > 20) {
            ngx_http_upstream_rr_peers_unlock(jhp->rrp.peers);
            return jhp->get_rr_peer(pc, &jhp->rrp);
        }

        if (++jhp->rehash > 3) {
            jhp->rehash = 0;
        }
    }

    jhp->rrp.current = peer;

    pc->sockaddr = peer->sockaddr;
    pc->socklen = peer->socklen;
    pc->name = &peer->name;

    peer->conns++;

    if (now - peer->checked > peer->fail_timeout) {
        peer->checked = now;
    }

    ngx_http_upstream_rr_peers_unlock(jhp->rrp.peers);

    jhp->rrp.tried[n] |= m;
    jhp->hash = hash;

    return NGX_OK;
}


/* 一致性哈希相关函数 */
static ngx_int_t
ngx_http_upstream_init_json_chash(ngx_conf_t *cf, ngx_http_upstream_srv_conf_t *us)
{
    size_t                                 size;
    uint32_t                               hash;
    ngx_str_t                             *server;
    ngx_uint_t                             npoints, i, j;
    ngx_http_upstream_rr_peer_t           *peer;
    ngx_http_upstream_rr_peers_t          *peers;
    ngx_http_upstream_chash_points_t      *points;
    ngx_http_upstream_json_hash_srv_conf_t *jhcf;


    jhcf = ngx_http_conf_upstream_srv_conf(us, ngx_http_upstream_json_hash_module);
    peers = us->peer.data;
    npoints = peers->total_weight * jhcf->virtual_nodes;

    size = sizeof(ngx_http_upstream_chash_points_t)
           + sizeof(ngx_http_upstream_chash_point_t) * (npoints - 1);

    points = ngx_palloc(cf->pool, size);
    if (points == NULL) {
        return NGX_ERROR;
    }

    points->number = 0;

    for (peer = peers->peer; peer; peer = peer->next) {
        server = &peer->server;

        /*
         * hash为每个upstream server创建虚拟节点
         */

        for (j = 0; j < (ngx_uint_t)(jhcf->virtual_nodes * peer->weight); j++) {
            if (j == 0) {
                /* 第一个虚拟节点使用服务器名直接哈希 */
                hash = ngx_http_upstream_json_hash_calculate(server->data, server->len, jhcf->hash_method);
            } else {
                /* 后续虚拟节点使用服务器名+索引的复合哈希，避免简单的字节操作 */
                u_char buf[256];
                ngx_uint_t len;
                
                len = ngx_snprintf(buf, sizeof(buf), "%V#%ui", server, j) - buf;
                hash = ngx_http_upstream_json_hash_calculate(buf, len, jhcf->hash_method);
            }

            points->point[points->number].hash = hash;
            points->point[points->number].server = server;
            points->number++;
        }
    }

    ngx_qsort(points->point,
              points->number,
              sizeof(ngx_http_upstream_chash_point_t),
              ngx_http_upstream_json_chash_cmp_points);

    /* 哈希碰撞处理：保留不同服务器的点，即使哈希值相同 */
    for (i = 0, j = 1; j < points->number; j++) {
        if (points->point[i].hash != points->point[j].hash ||
            ngx_strcmp(points->point[i].server->data, points->point[j].server->data) != 0) {
            points->point[++i] = points->point[j];
        } else {
            /* 记录哈希碰撞 */
            ngx_log_debug3(NGX_LOG_DEBUG_HTTP, cf->log, 0,
                          "json hash collision: servers '%V' and '%V' have same hash %ui",
                          points->point[i].server, points->point[j].server, 
                          points->point[i].hash);
        }
    }

    points->number = i + 1;

    jhcf->points = points;

    return NGX_OK;
}


static int ngx_libc_cdecl
ngx_http_upstream_json_chash_cmp_points(const void *one, const void *two)
{
    ngx_http_upstream_chash_point_t *first =
                                        (ngx_http_upstream_chash_point_t *) one;
    ngx_http_upstream_chash_point_t *second =
                                        (ngx_http_upstream_chash_point_t *) two;

    if (first->hash < second->hash) {
        return -1;

    } else if (first->hash > second->hash) {
        return 1;

    } else {
        return 0;
    }
}


static ngx_uint_t
ngx_http_upstream_find_json_chash_point(ngx_http_upstream_chash_points_t *points,
    uint32_t hash)
{
    ngx_uint_t                        i, j, k;
    ngx_http_upstream_chash_point_t  *point;

    /* find first point >= hash */

    point = &points->point[0];

    i = 0;
    j = points->number;

    while (i < j) {
        k = (i + j) / 2;

        if (hash > point[k].hash) {
            i = k + 1;

        } else if (hash < point[k].hash) {
            j = k;

        } else {
            return k;
        }
    }

    return i;
}

static ngx_int_t
ngx_http_upstream_get_json_chash_peer(ngx_peer_connection_t *pc, void *data)
{
    ngx_http_upstream_json_hash_peer_data_t *jhp = data;

    time_t                              now;
    uint32_t                            hash;
    uintptr_t                           m;
    ngx_uint_t                          n;
    ngx_http_upstream_rr_peer_t        *peer, *p;
    ngx_http_upstream_chash_point_t    *point;
    ngx_http_upstream_chash_points_t   *points;
    ngx_http_upstream_json_hash_srv_conf_t *jhcf;

    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, pc->log, 0,
                   "get json chash peer, try: %ui", pc->tries);

    pc->cached = 0;
    pc->connection = NULL;

    jhcf = jhp->conf;
    points = jhcf->points;

    if (jhp->tries > 20 || points->number <= 1 || jhp->hash_key.len == 0) {
        return jhp->get_rr_peer(pc, &jhp->rrp);
    }

    now = ngx_time();

    for ( ;; ) {

        hash = ngx_http_upstream_json_hash_calculate(jhp->hash_key.data, jhp->hash_key.len, jhcf->hash_method);

        if (jhp->rehash > 0) {
            u_char buf[NGX_INT_T_LEN];
            size_t size = ngx_sprintf(buf, "%uD", jhp->rehash) - buf;
            uint32_t rehash_val = ngx_http_upstream_json_hash_calculate(buf, size, jhcf->hash_method);
            hash ^= rehash_val;  /* 组合原hash和rehash，而不是覆盖 */
        }

        n = ngx_http_upstream_find_json_chash_point(points, hash);
        point = &points->point[n % points->number];

        /* 在一致性哈希环中查找对应的实际peer */
        ngx_http_upstream_rr_peers_rlock(jhp->rrp.peers);

        for (peer = jhp->rrp.peers->peer; peer; peer = peer->next) {
            if (ngx_memn2cmp(peer->server.data, point->server->data,
                           peer->server.len, point->server->len) == 0) {
                break;
            }
        }

        if (peer == NULL) {
            ngx_http_upstream_rr_peers_unlock(jhp->rrp.peers);
            goto next;
        }

        /* 计算peer在peers数组中的位置 */
        for (n = 0, p = jhp->rrp.peers->peer; p && p != peer; p = p->next, n++);
        
        m = (uintptr_t) 1 << n % (8 * sizeof(uintptr_t));
        n = n / (8 * sizeof(uintptr_t));

        if (jhp->rrp.tried[n] & m) {
            ngx_http_upstream_rr_peers_unlock(jhp->rrp.peers);
            goto next;
        }

        ngx_log_debug2(NGX_LOG_DEBUG_HTTP, pc->log, 0,
                       "get json chash peer: %ui %04XA", n, (uint64_t) m);

        if (peer->down) {
            ngx_http_upstream_rr_peers_unlock(jhp->rrp.peers);
            goto next;
        }

        if (peer->max_fails
            && peer->fails >= peer->max_fails
            && now - peer->checked <= peer->fail_timeout)
        {
            ngx_http_upstream_rr_peers_unlock(jhp->rrp.peers);
            goto next;
        }

        if (peer->max_conns && peer->conns >= peer->max_conns) {
            ngx_http_upstream_rr_peers_unlock(jhp->rrp.peers);
            goto next;
        }

        break;

    next:

        if (++jhp->tries > 20) {
            return jhp->get_rr_peer(pc, &jhp->rrp);
        }

        if (++jhp->rehash > 3) {
            jhp->rehash = 0;
        }
    }

    jhp->rrp.current = peer;

    pc->sockaddr = peer->sockaddr;
    pc->socklen = peer->socklen;
    pc->name = &peer->name;

    peer->conns++;

    if (now - peer->checked > peer->fail_timeout) {
        peer->checked = now;
    }

    ngx_http_upstream_rr_peers_unlock(jhp->rrp.peers);

    jhp->rrp.tried[n] |= m;
    jhp->hash = hash;

    return NGX_OK;
}


static void *
ngx_http_upstream_json_hash_create_conf(ngx_conf_t *cf)
{
    ngx_http_upstream_json_hash_srv_conf_t  *conf;

    conf = ngx_pcalloc(cf->pool, sizeof(ngx_http_upstream_json_hash_srv_conf_t));
    if (conf == NULL) {
        return NULL;
    }

    /*
     * set by ngx_pcalloc():
     *
     *     conf->json_field = { 0, NULL };
     *     conf->consistent_hash = 0;
     *     conf->points = NULL;
     */

    /* 设置默认值 */
    conf->virtual_nodes = NGX_HTTP_UPSTREAM_JSON_HASH_DEFAULT_VIRTUAL_NODES;
    conf->hash_method = NGX_HTTP_UPSTREAM_JSON_HASH_CRC32;
    conf->max_json_depth = NGX_HTTP_UPSTREAM_JSON_HASH_DEFAULT_MAX_DEPTH;
    conf->check_content_type = 1; /* 默认开启Content-Type检查 */
    conf->stats_interval = 10000; /* 默认10秒统计间隔 */
    conf->max_json_size = 1024 * 1024; /* 默认1MB最大JSON大小 */
    conf->max_virtual_memory = 2 * 1024 * 1024; /* 默认2MB虚拟节点内存限制 */
    
    /* 设置默认fallback key */
    ngx_str_set(&conf->fallback_key, "default");
    
    /* 初始化统计字段 */
    conf->hash_requests = 0;
    conf->hash_failures = 0;
    conf->json_parse_time = 0;
    conf->content_type_checks = 0;
    conf->stats_resets = 0;

    return conf;
}


static char *
ngx_http_upstream_json_hash(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_upstream_json_hash_srv_conf_t *jhcf = conf;

    ngx_str_t                         *value;
    ngx_http_upstream_srv_conf_t      *uscf;

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

    uscf = ngx_http_conf_get_module_srv_conf(cf, ngx_http_upstream_module);

    if (uscf->peer.init_upstream) {
        ngx_conf_log_error(NGX_LOG_WARN, cf, 0,
                          "load balancing method redefined");
    }

    uscf->peer.init_upstream = ngx_http_upstream_init_json_hash;

    uscf->flags = NGX_HTTP_UPSTREAM_CREATE
                  |NGX_HTTP_UPSTREAM_WEIGHT
                  |NGX_HTTP_UPSTREAM_MAX_FAILS
                  |NGX_HTTP_UPSTREAM_FAIL_TIMEOUT
                  |NGX_HTTP_UPSTREAM_DOWN
                  |NGX_HTTP_UPSTREAM_BACKUP;

    if (cf->args->nelts == 3) {
        if (ngx_strcmp(value[2].data, "consistent") == 0) {
            jhcf->consistent_hash = 1;
        } else {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                              "invalid parameter \"%V\"", &value[2]);
            return NGX_CONF_ERROR;
        }
    }

    return NGX_CONF_OK;
}

/* 虚拟节点数配置 */
static char *
ngx_http_upstream_json_hash_virtual_nodes(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_upstream_json_hash_srv_conf_t *jhcf = conf;
    ngx_str_t *value;
    ngx_int_t n;
    size_t estimated_memory;

    value = cf->args->elts;
    n = ngx_atoi(value[1].data, value[1].len);

    if (n == NGX_ERROR || n < 1 || n > 10000) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                          "invalid virtual_nodes value \"%V\", must be between 1 and 10000",
                          &value[1]);
        return NGX_CONF_ERROR;
    }
    
    /* 检查内存使用量，防止过度消耗 */

    /* 精确计算内存使用量 */
    /* 每个虚拟节点需要：
     * - ngx_http_upstream_chash_point_t 结构：12字节（hash + server指针）
     * - 服务器名字符串：平均32字节
     * - 内存对齐开销：约8字节
     * 总计约52字节/节点，保守估计60字节
     */
    estimated_memory = n * 60;
    if (estimated_memory > jhcf->max_virtual_memory) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                          "virtual nodes count \"%ui\" would consume too much memory: %uzKB, "
                          "maximum allowed: %uzKB (configure with json_hash_max_virtual_memory)",
                          n, estimated_memory / 1024, jhcf->max_virtual_memory / 1024);
        return NGX_CONF_ERROR;
    } else if (estimated_memory > 512 * 1024) { /* 超过512KB提示警告 */
        ngx_conf_log_error(NGX_LOG_WARN, cf, 0,
                          "large virtual nodes count \"%ui\" will consume approximately %uzKB memory "
                          "(estimated: %uz bytes per node)",
                          n, estimated_memory / 1024, (size_t)60);
    } else if (n > 1000) {
        ngx_conf_log_error(NGX_LOG_INFO, cf, 0,
                          "using %ui virtual nodes, estimated memory usage: %uzKB",
                          n, estimated_memory / 1024);
    }

    jhcf->virtual_nodes = n;
    return NGX_CONF_OK;
}

/* 哈希算法配置 */
static char *
ngx_http_upstream_json_hash_method(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_upstream_json_hash_srv_conf_t *jhcf = conf;
    ngx_str_t *value;

    value = cf->args->elts;

    if (ngx_strcmp(value[1].data, "crc32") == 0) {
        jhcf->hash_method = NGX_HTTP_UPSTREAM_JSON_HASH_CRC32;
    } else if (ngx_strcmp(value[1].data, "murmur3") == 0) {
        jhcf->hash_method = NGX_HTTP_UPSTREAM_JSON_HASH_MURMUR;
    } else {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                          "invalid hash method \"%V\", must be \"crc32\" or \"murmur3\"",
                          &value[1]);
        return NGX_CONF_ERROR;
    }

    return NGX_CONF_OK;
}

/* JSON最大深度配置 */
static char *
ngx_http_upstream_json_hash_max_depth(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_upstream_json_hash_srv_conf_t *jhcf = conf;
    ngx_str_t *value;
    ngx_int_t n;

    value = cf->args->elts;
    n = ngx_atoi(value[1].data, value[1].len);

    if (n == NGX_ERROR || n < 0 || n > 1000) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                          "invalid max_depth value \"%V\", must be between 0 and 1000 (0 means unlimited)",
                          &value[1]);
        return NGX_CONF_ERROR;
    }
    
    /* 安全性提示 */
    if (n > NGX_HTTP_UPSTREAM_JSON_HASH_DEFAULT_MAX_DEPTH) {
        ngx_conf_log_error(NGX_LOG_WARN, cf, 0,
                          "json_hash_max_depth set to %ui, which exceeds safe limit %ui. "
                          "Deep JSON nesting may impact performance and security",
                          n, NGX_HTTP_UPSTREAM_JSON_HASH_DEFAULT_MAX_DEPTH);
    } else if (n == 0) {
        ngx_conf_log_error(NGX_LOG_WARN, cf, 0,
                          "json_hash_max_depth set to 0: unlimited JSON nesting depth. "
                          "This may pose security risks with malicious JSON payloads");
    }

    jhcf->max_json_depth = n;
    return NGX_CONF_OK;
}

/* Content-Type检查配置 */
static char *
ngx_http_upstream_json_hash_check_content_type(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_upstream_json_hash_srv_conf_t *jhcf = conf;
    ngx_str_t *value;

    value = cf->args->elts;

    if (ngx_strcmp(value[1].data, "on") == 0) {
        jhcf->check_content_type = 1;
    } else if (ngx_strcmp(value[1].data, "off") == 0) {
        jhcf->check_content_type = 0;
    } else {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                          "invalid check_content_type value \"%V\", must be \"on\" or \"off\"",
                          &value[1]);
        return NGX_CONF_ERROR;
    }

    return NGX_CONF_OK;
}

/* fallback key配置 */
static char *
ngx_http_upstream_json_hash_fallback_key(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_upstream_json_hash_srv_conf_t *jhcf = conf;
    ngx_str_t *value;

    if (jhcf->fallback_key.data != NULL) {
        return "is duplicate";
    }

    value = cf->args->elts;

    /* 验证fallback key不为空 */
    if (value[1].len == 0) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                          "fallback key cannot be empty");
        return NGX_CONF_ERROR;
    }

    /* 验证fallback key长度合理 */
    if (value[1].len > 256) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                          "fallback key too long: %uz", value[1].len);
        return NGX_CONF_ERROR;
    }

    jhcf->fallback_key = value[1];
    return NGX_CONF_OK;
}

/* 统计间隔配置 */
static char *
ngx_http_upstream_json_hash_stats_interval(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_upstream_json_hash_srv_conf_t *jhcf = conf;
    ngx_str_t *value;
    ngx_int_t n;

    value = cf->args->elts;
    n = ngx_atoi(value[1].data, value[1].len);

    if (n == NGX_ERROR || n < 0 || n > 3600000) { /* 最大1小时 */
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                          "invalid stats_interval value \"%V\", must be between 0 and 3600000 (0 means disable stats)",
                          &value[1]);
        return NGX_CONF_ERROR;
    }

    jhcf->stats_interval = n;
    
    if (n == 0) {
        ngx_conf_log_error(NGX_LOG_INFO, cf, 0,
                          "json_hash_stats_interval set to 0: statistics logging disabled");
    }
    
    return NGX_CONF_OK;
}

/* 最大JSON大小配置 */
static char *
ngx_http_upstream_json_hash_max_size(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_upstream_json_hash_srv_conf_t *jhcf = conf;
    ngx_str_t *value;

    value = cf->args->elts;
    jhcf->max_json_size = ngx_parse_size(&value[1]);
    
    if (jhcf->max_json_size == (size_t) NGX_ERROR) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                          "invalid max json size value \"%V\"", &value[1]);
        return NGX_CONF_ERROR;
    }

    if (jhcf->max_json_size < 1024) { /* 最小1KB */
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                          "max json size too small: %uz, minimum is 1024", 
                          jhcf->max_json_size);
        return NGX_CONF_ERROR;
    }

    return NGX_CONF_OK;
}

/* 虚拟节点最大内存配置 */
static char *
ngx_http_upstream_json_hash_max_virtual_memory(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_upstream_json_hash_srv_conf_t *jhcf = conf;
    ngx_str_t *value;

    value = cf->args->elts;
    jhcf->max_virtual_memory = ngx_parse_size(&value[1]);
    
    if (jhcf->max_virtual_memory == (size_t) NGX_ERROR) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                          "invalid max virtual memory value \"%V\"", &value[1]);
        return NGX_CONF_ERROR;
    }

    if (jhcf->max_virtual_memory < 64 * 1024) { /* 最小64KB */
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                          "max virtual memory too small: %uz, minimum is 65536", 
                          jhcf->max_virtual_memory);
        return NGX_CONF_ERROR;
    }

    return NGX_CONF_OK;
}

/* 统计信息记录函数 - 解决多进程问题 */
static void
ngx_http_upstream_json_hash_log_stats(ngx_http_request_t *r, ngx_http_upstream_json_hash_srv_conf_t *jhcf)
{
    ngx_msec_t current_time = ngx_current_msec;
    static ngx_msec_t last_log_time = 0;
    static pid_t last_pid = 0;
    pid_t current_pid = ngx_getpid();
    
    /* 每个worker进程独立统计，避免多进程冲突 */
    if (current_pid != last_pid) {
        last_log_time = 0; /* 新进程重置时间 */
        last_pid = current_pid;
    }
    
    /* 根据配置间隔记录统计信息（0表示禁用） */
    if (jhcf->stats_interval > 0 && current_time - last_log_time > jhcf->stats_interval) {
        ngx_atomic_t requests = jhcf->hash_requests;
        ngx_atomic_t failures = jhcf->hash_failures;
        ngx_atomic_t content_checks = jhcf->content_type_checks;
        ngx_atomic_t parse_time = jhcf->json_parse_time;
        
        /* 检查统计变量的溢出风险并自动重置 */
        if (requests > (NGX_ATOMIC_T_MAX / 2)) {
            ngx_atomic_t reset_count = ngx_atomic_fetch_add(&jhcf->stats_resets, 1) + 1;
            
            ngx_log_error(NGX_LOG_WARN, r->connection->log, 0,
                          "json_hash stats[worker:%P]: hash_requests approaching overflow: %ui, "
                          "resetting stats (reset #%ui)",
                          current_pid, (ngx_uint_t)requests, (ngx_uint_t)reset_count);
            
            /* 检查重置频率，如果过于频繁则发出告警 */
            if (reset_count > 10) {
                ngx_log_error(NGX_LOG_ALERT, r->connection->log, 0,
                              "json_hash stats[worker:%P]: frequent stats resets detected (%ui times), "
                              "possible system overload or configuration issue",
                              current_pid, (ngx_uint_t)reset_count);
            }
            
            /* 重置统计信息以防止溢出 */
            jhcf->hash_requests = 0;
            jhcf->hash_failures = 0;
            jhcf->json_parse_time = 0;
            jhcf->content_type_checks = 0;
            return; /* 重置后直接返回，下次统计将从新值开始 */
        }
        
        ngx_log_error(NGX_LOG_INFO, r->connection->log, 0,
                      "json_hash stats[worker:%P]: requests=%ui, failures=%ui, "
                      "content_type_checks=%ui, avg_parse_time=%uims, "
                      "failure_rate=%.2f%%, success_rate=%.2f%%",
                      current_pid,
                      (ngx_uint_t)requests,
                      (ngx_uint_t)failures,
                      (ngx_uint_t)content_checks,
                      requests > 0 ? (ngx_uint_t)(parse_time / requests) : 0,
                      requests > 0 ? (float)failures * 100.0 / requests : 0.0,
                      requests > 0 ? (float)(requests - failures) * 100.0 / requests : 0.0);
        
        last_log_time = current_time;
    }
} 
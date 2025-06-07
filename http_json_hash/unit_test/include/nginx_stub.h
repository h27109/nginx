#ifndef NGINX_STUB_H
#define NGINX_STUB_H

#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>

/* 定义Nginx基本类型 */
typedef intptr_t        ngx_int_t;
typedef uintptr_t       ngx_uint_t;
typedef intptr_t        ngx_flag_t;
typedef int             ngx_msec_int_t;
typedef long            ngx_msec_t;
typedef unsigned char   u_char;

#define NGX_OK          0
#define NGX_ERROR       -1
#define NGX_AGAIN       -2
#define NGX_BUSY        -3
#define NGX_DONE        -4
#define NGX_DECLINED    -5
#define NGX_ABORT       -6

/* 定义Nginx日志结构 */
typedef struct {
    void   *file;
    size_t  line;
} ngx_log_t;

#define NGX_LOG_DEBUG   0
#define NGX_LOG_INFO    1
#define NGX_LOG_WARN    2
#define NGX_LOG_ERR     3
#define NGX_LOG_ALERT   4
#define NGX_LOG_EMERG   5

/* 定义Nginx字符串结构 */
typedef struct {
    size_t      len;
    u_char     *data;
} ngx_str_t;

/* 定义Nginx缓冲区结构 */
typedef struct {
    u_char     *pos;
    u_char     *last;
    u_char     *start;
    u_char     *end;
    int         in_file;
    size_t      file_pos;
    size_t      file_last;
    void       *file;
    void       *shadow;
    unsigned    temporary:1;
    unsigned    memory:1;
    unsigned    mmap:1;
    unsigned    recycled:1;
    unsigned    in_event:1;
    unsigned    flush:1;
    unsigned    sync:1;
    unsigned    last_buf:1;
    unsigned    last_in_chain:1;
    unsigned    last_shadow:1;
    unsigned    temp_file:1;
} ngx_buf_t;

/* 定义链表结构 */
typedef struct ngx_chain_s  ngx_chain_t;

struct ngx_chain_s {
    ngx_buf_t    *buf;
    ngx_chain_t  *next;
};

/* 定义Nginx内存池结构 */
typedef void  ngx_pool_t;

/* 定义配置结构 */
typedef struct {
    void               *pool;
    ngx_log_t          *log;
    void               *args;
    void               *conf;
    ngx_uint_t          module_type;
    ngx_uint_t          cmd_type;
    void               *handler_conf;
    char               *file_name;
    ngx_uint_t          line;
    void               *errstr;
    ngx_str_t          *name;
    ngx_uint_t          op;
} ngx_conf_t;

/* 定义Nginx文件结构 */
typedef struct {
    int            fd;
    ngx_str_t      name;
    ngx_log_t     *log;
    size_t         offset;
    size_t         sys_offset;
    size_t         size;
} ngx_file_t;

/* 定义Nginx HTTP请求结构 */
typedef struct {
    void                *main;
    void                *parent;
    void                *pool;
    ngx_log_t           *log;
    void                *upstream;
    void                *connection;
    void                *headers_in;
    void                *headers_out;
    void                *request_body;
    unsigned int         method;
    ngx_str_t            uri;
    char                *uri_start;
    char                *uri_end;
    char                *unparsed_uri;
} ngx_http_request_t;

/* 定义请求体结构 */
typedef struct {
    ngx_chain_t         *bufs;
    ngx_chain_t         *buf;
    void                *temp_file;
} ngx_http_request_body_t;

/* 定义HTTP头部结构 */
typedef struct {
    ngx_str_t            key;
    ngx_str_t            value;
} ngx_table_elt_t;

/* 定义HTTP头部结构 */
typedef struct {
    ngx_table_elt_t     *content_type;
} ngx_http_headers_in_t;

/* 定义一致性哈希点结构 */
typedef struct {
    uint32_t             hash;
    ngx_str_t           *server;
} ngx_http_upstream_chash_point_t;

/* 定义一致性哈希点集合结构 */
typedef struct {
    ngx_uint_t                number;
    ngx_http_upstream_chash_point_t     point[1];
} ngx_http_upstream_chash_points_t;

/* 定义连接结构 */
typedef struct {
    void                *log;
} ngx_connection_t;

/* 定义一些函数原型 */
#define ngx_palloc(pool, size)  malloc(size)
#define ngx_pcalloc(pool, size) calloc(1, size)
#define ngx_pfree(pool, ptr)    free(ptr)
#define ngx_memcpy              memcpy
#define ngx_memzero(buf, n)     memset(buf, 0, n)

/* CRC32函数 */
uint32_t ngx_crc32_long(u_char *data, size_t len);

/* 日志函数 */
void ngx_log_debug0(ngx_uint_t level, ngx_log_t *log, int err, const char *fmt, ...);
void ngx_log_debug1(ngx_uint_t level, ngx_log_t *log, int err, const char *fmt, ...);
void ngx_log_debug2(ngx_uint_t level, ngx_log_t *log, int err, const char *fmt, ...);
void ngx_log_error(ngx_uint_t level, ngx_log_t *log, int err, const char *fmt, ...);

/* 字符串操作函数 */
ngx_int_t ngx_strncasecmp(u_char *s1, u_char *s2, size_t n);
u_char *ngx_sprintf(u_char *buf, const char *fmt, ...);
size_t ngx_strlen(u_char *p);
ngx_int_t ngx_memn2cmp(u_char *s1, u_char *s2, size_t n1, size_t n2);

/* 内存池操作函数 */
void *ngx_create_pool(size_t size, ngx_log_t *log);
void ngx_destroy_pool(ngx_pool_t *pool);

/* 时间相关函数 */
void ngx_time_update(void);
extern ngx_msec_t ngx_current_msec;

/* 原子操作函数 */
#define ngx_atomic_t          uint32_t
#define NGX_ATOMIC_T_LEN      sizeof("-2147483648")
#define NGX_ATOMIC_T_MAX      2147483647
#define ngx_atomic_fetch_add(p, n)  ((*p) += (n))

/* 进程ID获取函数 */
#define ngx_getpid()          getpid()

/* 文件读取函数 */
ssize_t ngx_read_file(ngx_file_t *file, u_char *buf, size_t size, off_t offset);

/* 缓冲区大小计算函数 */
#define ngx_buf_size(b)       (((b)->last - (b)->pos) + ((b)->in_file ? ((b)->file_last - (b)->file_pos) : 0))

/* 解析大小函数 */
size_t ngx_parse_size(ngx_str_t *line);

/* 排序函数 */
#define ngx_qsort               qsort

/* HTTP上游模块结构 */
typedef struct {
    void                *data;
    void                *get;
} ngx_peer_connection_t;

/* HTTP上游模块RR数据结构 */
typedef struct {
    void                *peers;
    void                *current;
    void                *tried;
} ngx_http_upstream_rr_peer_data_t;

#endif /* NGINX_STUB_H */ 
#include "nginx_stub.h"
#include <stdarg.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

/* 日志函数实现 */
void ngx_log_error(ngx_uint_t level, ngx_log_t *log, int err, const char *fmt, ...) {
    (void)log;
    (void)err;
    
    if (level <= NGX_LOG_INFO) {
        va_list args;
        va_start(args, fmt);
        
        fprintf(stderr, "[NGINX LOG %lu] ", (unsigned long)level);
        vfprintf(stderr, fmt, args);
        fprintf(stderr, "\n");
        
        va_end(args);
    }
}

void ngx_log_debug0(ngx_uint_t level, ngx_log_t *log, int err, const char *fmt) {
    (void)level;
    (void)log;
    (void)err;
    (void)fmt;
    /* 在测试中，调试日志通常不输出 */
}

void ngx_log_debug1(ngx_uint_t level, ngx_log_t *log, int err, const char *fmt, uintptr_t arg1) {
    (void)level;
    (void)log;
    (void)err;
    (void)fmt;
    (void)arg1;
    /* 在测试中，调试日志通常不输出 */
}

void ngx_log_debug2(ngx_uint_t level, ngx_log_t *log, int err, const char *fmt, uintptr_t arg1, uintptr_t arg2) {
    (void)level;
    (void)log;
    (void)err;
    (void)fmt;
    (void)arg1;
    (void)arg2;
    /* 在测试中，调试日志通常不输出 */
}

void ngx_log_debug3(ngx_uint_t level, ngx_log_t *log, int err, const char *fmt, uintptr_t arg1, uintptr_t arg2, uintptr_t arg3) {
    (void)level;
    (void)log;
    (void)err;
    (void)fmt;
    (void)arg1;
    (void)arg2;
    (void)arg3;
    /* 在测试中，调试日志通常不输出 */
}

/* 格式化输出函数 */
unsigned char *ngx_sprintf(unsigned char *buf, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    
    int len = vsprintf((char *)buf, fmt, args);
    
    va_end(args);
    
    return buf + len;
}

unsigned char *ngx_snprintf(unsigned char *buf, size_t max, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    
    int len = vsnprintf((char *)buf, max, fmt, args);
    if (len < 0 || (size_t)len >= max) {
        len = max - 1;
    }
    
    va_end(args);
    
    return buf + len;
}

/* 原子操作 */
void ngx_atomic_fetch_add(ngx_atomic_t *value, ngx_atomic_int_t add) {
    /* 简单实现，非原子 */
    *value += add;
}

/* 时间相关函数 */
time_t ngx_time(void) {
    return time(NULL);
}

ngx_uint_t ngx_getpid(void) {
    return (ngx_uint_t)getpid();
}

/* 排序函数 */
void ngx_qsort(void *base, size_t n, size_t size, int (*cmp)(const void *, const void *)) {
    qsort(base, n, size, cmp);
}

/* 用于解析配置的函数 */
size_t ngx_parse_size(ngx_str_t *line) {
    if (line == NULL || line->data == NULL || line->len == 0) {
        return (size_t)NGX_ERROR;
    }
    
    char *str = malloc(line->len + 1);
    if (str == NULL) {
        return (size_t)NGX_ERROR;
    }
    
    memcpy(str, line->data, line->len);
    str[line->len] = '\0';
    
    size_t size = 0;
    char *p = str;
    
    while (*p >= '0' && *p <= '9') {
        size = size * 10 + (*p++ - '0');
    }
    
    switch (*p) {
        case 'K':
        case 'k':
            size *= 1024;
            break;
        case 'M':
        case 'm':
            size *= 1024 * 1024;
            break;
        case 'G':
        case 'g':
            size *= 1024 * 1024 * 1024;
            break;
    }
    
    free(str);
    
    return size;
} 
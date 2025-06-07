#ifndef TEST_UTILS_H
#define TEST_UTILS_H

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdlib.h>
#include <string.h>

#include "nginx_stub.h"
#include "cjson_stub.h"
#include "ngx_http_upstream_json_hash.h"

/* 测试辅助函数 */

/* 创建ngx_str_t */
ngx_str_t *create_ngx_str(void *pool, const char *text);

/* 创建测试请求 */
ngx_http_request_t *create_test_request(void);

/* 销毁测试请求 */
void destroy_test_request(ngx_http_request_t *r);

/* 创建测试JSON对象 */
cJSON *create_test_json(const char *json_text);

/* 添加JSON请求体到请求 */
void add_json_body_to_request(ngx_http_request_t *r, const char *json_text);

/* 创建服务器配置 */
ngx_http_upstream_json_hash_srv_conf_t *create_test_srv_conf(void);

/* 销毁服务器配置 */
void destroy_test_srv_conf(ngx_http_upstream_json_hash_srv_conf_t *jhcf);

/* 设置服务器配置的JSON字段 */
void set_json_field(ngx_http_upstream_json_hash_srv_conf_t *jhcf, const char *field);

/* 设置哈希方法 */
void set_hash_method(ngx_http_upstream_json_hash_srv_conf_t *jhcf, ngx_uint_t method);

/* 创建一组测试服务器 */
void create_test_peers(ngx_uint_t num_peers, ngx_str_t **servers, ngx_uint_t **weights);

#endif /* TEST_UTILS_H */ 
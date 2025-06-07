/*
 * Copyright (C) nginx-json-hash-module
 */

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include <cjson/cJSON.h>


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
    ngx_http_upstream_chash_points_t   *points;
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

static void *ngx_http_upstream_json_hash_create_conf(ngx_conf_t *cf);
static char *ngx_http_upstream_json_hash(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);


static ngx_command_t  ngx_http_upstream_json_hash_commands[] = {

    { ngx_string("json_hash"),
      NGX_HTTP_UPS_CONF|NGX_CONF_TAKE12,
      ngx_http_upstream_json_hash,
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

    jhp = (ngx_http_upstream_json_hash_peer_data_t *) r->upstream->peer.data;
    jhcf = jhp->conf;

    /* 从JSON body中提取字段值 */
    hash_key = ngx_http_upstream_extract_json_field(r, &jhcf->json_field);
    
    if (hash_key && hash_key->len > 0) {
        jhp->hash_key = *hash_key;
        ngx_log_debug2(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                       "json hash key: \"%V\" from field: \"%V\"",
                       &jhp->hash_key, &jhcf->json_field);
    } else {
        /* 如果提取失败，使用默认值或者客户端IP */
        jhp->hash_key.data = (u_char *) "default";
        jhp->hash_key.len = 7;
        ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                       "json hash key extraction failed, using default: \"%V\"",
                       &jhp->hash_key);
    }
}


static ngx_str_t *
ngx_http_upstream_extract_json_field(ngx_http_request_t *r, ngx_str_t *field_name)
{
    ngx_chain_t     *cl;
    ngx_buf_t       *buf;
    u_char          *body_data;
    size_t           body_len = 0;
    cJSON           *json;
    cJSON           *field;
    ngx_str_t       *result;
    char            *field_str;

    if (r->request_body == NULL || r->request_body->bufs == NULL) {
        return NULL;
    }

    /* 计算body总长度 */
    for (cl = r->request_body->bufs; cl; cl = cl->next) {
        body_len += ngx_buf_size(cl->buf);
    }

    if (body_len == 0) {
        return NULL;
    }

    /* 分配内存并复制body数据 */
    body_data = ngx_palloc(r->pool, body_len + 1);
    if (body_data == NULL) {
        return NULL;
    }

    u_char *p = body_data;
    for (cl = r->request_body->bufs; cl; cl = cl->next) {
        buf = cl->buf;
        if (buf->in_file) {
            /* 如果数据在文件中，需要读取文件 */
            /* 这里简化实现，实际应该读取文件内容 */
            continue;
        }
        size_t len = buf->last - buf->pos;
        ngx_memcpy(p, buf->pos, len);
        p += len;
    }
    *p = '\0';

    /* 解析JSON */
    json = cJSON_Parse((char *) body_data);
    if (json == NULL) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                      "failed to parse JSON from request body");
        return NULL;
    }

    /* 提取指定字段 */
    field_str = ngx_palloc(r->pool, field_name->len + 1);
    if (field_str == NULL) {
        cJSON_Delete(json);
        return NULL;
    }
    ngx_memcpy(field_str, field_name->data, field_name->len);
    field_str[field_name->len] = '\0';

    field = cJSON_GetObjectItem(json, field_str);
    if (field == NULL || !cJSON_IsString(field)) {
        cJSON_Delete(json);
        return NULL;
    }

    /* 创建结果字符串 */
    result = ngx_palloc(r->pool, sizeof(ngx_str_t));
    if (result == NULL) {
        cJSON_Delete(json);
        return NULL;
    }

    char *field_value = cJSON_GetStringValue(field);
    result->len = ngx_strlen(field_value);
    result->data = ngx_palloc(r->pool, result->len);
    if (result->data == NULL) {
        cJSON_Delete(json);
        return NULL;
    }
    ngx_memcpy(result->data, field_value, result->len);

    cJSON_Delete(json);
    return result;
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

        hash = ngx_crc32_long(jhp->hash_key.data, jhp->hash_key.len);

        if (jhp->rehash > 0) {
            size = ngx_sprintf(buf, "%uD", jhp->rehash) - buf;
            hash = ngx_crc32_long(buf, size);
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
    union {
        uint32_t                           value;
        u_char                             byte[4];
    } prev_hash;

    jhcf = ngx_http_conf_upstream_srv_conf(us, ngx_http_upstream_json_hash_module);
    peers = us->peer.data;
    npoints = peers->total_weight * 160;

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

        for (j = 0; j < (ngx_uint_t)(160 * peer->weight); j++) {
            hash = ngx_crc32_long(server->data, server->len);

            if (j) {
                prev_hash.value = hash;
                prev_hash.byte[0] = (u_char) (prev_hash.byte[0] + j);
                prev_hash.byte[1] = (u_char) (prev_hash.byte[1] + j);
                prev_hash.byte[2] = (u_char) (prev_hash.byte[2] + j);
                prev_hash.byte[3] = (u_char) (prev_hash.byte[3] + j);
                hash = prev_hash.value;
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

    for (i = 0, j = 1; j < points->number; j++) {
        if (points->point[i].hash != points->point[j].hash) {
            points->point[++i] = points->point[j];
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

        hash = ngx_crc32_long(jhp->hash_key.data, jhp->hash_key.len);

        if (jhp->rehash > 0) {
            hash = ngx_crc32_long((u_char *) &jhp->rehash, sizeof(ngx_uint_t));
        }

        n = ngx_http_upstream_find_json_chash_point(points, hash);
        point = &points->point[n % points->number];

        /* TODO: 查找实际的peer */
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
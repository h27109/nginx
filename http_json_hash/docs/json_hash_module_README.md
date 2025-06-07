# Nginx JSON Hash 负载均衡模块

## 功能概述

这是一个自定义的nginx upstream模块，可以根据HTTP请求体中JSON格式数据的指定字段进行hash计算，实现负载均衡分流。该模块特别适用于需要基于用户ID、会话ID等业务字段进行一致性路由的场景。

## 主要特性

- **JSON字段解析**: 自动解析HTTP请求体中的JSON数据
- **灵活的hash键**: 支持指定任意JSON字段作为hash键
- **一致性hash**: 支持一致性hash算法，在服务器变更时保持更好的分布稳定性
- **故障转移**: 支持服务器健康检查和故障转移
- **权重支持**: 支持为不同服务器设置权重

## 编译要求

- nginx 1.12+ 
- cJSON库 (用于JSON解析)
- 标准的nginx编译环境

## 安装步骤

### 1. 安装依赖

```bash
# Ubuntu/Debian
sudo apt-get install libcjson-dev

# CentOS/RHEL  
sudo yum install libcjson-devel

# 或者手动编译安装cJSON
git clone https://github.com/DaveGamble/cJSON.git
cd cJSON
mkdir build && cd build
cmake ..
make && sudo make install
```

### 2. 编译nginx

将模块文件放置到nginx源码的`src/http/modules/`目录下：

```bash
# 假设你的nginx源码目录为 /path/to/nginx
cp ngx_http_upstream_json_hash_module.c /path/to/nginx/src/http/modules/

# 配置nginx编译，添加该模块
cd /path/to/nginx
./configure \
    --add-module=src/http/modules/ \
    --with-http_upstream_hash_module \
    --with-http_upstream_least_conn_module \
    --with-http_upstream_ip_hash_module \
    [其他配置选项...]

# 编译和安装
make && sudo make install
```

### 3. 或者作为动态模块编译

```bash
./configure \
    --add-dynamic-module=src/http/modules/ \
    [其他配置选项...]

make modules
```

然后在nginx配置中加载：
```nginx
load_module modules/ngx_http_upstream_json_hash_module.so;
```

## 配置语法

```nginx
Syntax:    json_hash field_name [consistent];
Default:   —
Context:   upstream
```

### 参数说明

- `field_name`: JSON中用于hash计算的字段名
- `consistent`: 可选参数，启用一致性hash算法

## 使用示例

### 1. 基本配置

```nginx
upstream backend_api {
    json_hash user_id;
    
    server 192.168.1.10:8080;
    server 192.168.1.11:8080;
    server 192.168.1.12:8080;
}

server {
    listen 80;
    
    location /api/ {
        proxy_pass http://backend_api;
        proxy_request_buffering on;  # 重要：确保请求体被完整读取
    }
}
```

### 2. 一致性hash配置

```nginx
upstream backend_consistent {
    json_hash user_id consistent;
    
    server 192.168.1.20:8080 weight=3;
    server 192.168.1.21:8080 weight=2;
    server 192.168.1.22:8080 weight=1;
}
```

### 3. 请求示例

```bash
# 示例1: 根据user_id分流
curl -X POST http://example.com/api/login \
  -H "Content-Type: application/json" \
  -d '{
    "user_id": "12345",
    "username": "john_doe",
    "timestamp": 1703123456
  }'

# 示例2: 根据session_id分流  
curl -X POST http://example.com/api/action \
  -H "Content-Type: application/json" \
  -d '{
    "session_id": "abc123xyz",
    "action": "update_profile",
    "data": {...}
  }'
```

## 工作原理

1. **请求接收**: nginx接收到HTTP请求后，模块会读取完整的请求体
2. **JSON解析**: 使用cJSON库解析请求体中的JSON数据
3. **字段提取**: 根据配置的字段名从JSON中提取对应的值
4. **Hash计算**: 对提取的字段值进行CRC32 hash计算
5. **服务器选择**: 根据hash值和权重分布选择目标服务器
6. **请求转发**: 将请求转发到选定的后端服务器

## 故障处理

- 如果JSON解析失败，模块会记录错误日志并回退到轮询算法
- 如果指定字段不存在，会使用默认值或客户端IP进行hash
- 支持服务器健康检查和自动故障转移

## 性能考虑

- JSON解析会带来一定的CPU开销，建议在高并发场景下进行性能测试
- 请求体缓冲会占用额外内存，建议合理设置`client_body_buffer_size`
- 一致性hash算法在节点变更时的重新分布开销较小

## 调试

启用debug日志可以查看详细的分流过程：

```nginx
error_log /var/log/nginx/error.log debug;
```

日志示例：
```
2023/12/21 10:30:15 [debug] 12345#0: json hash key: "12345" from field: "user_id"
2023/12/21 10:30:15 [debug] 12345#0: get json hash peer: 1 04A
```

## 限制和注意事项

1. 只支持`application/json`类型的请求体
2. 请求体大小受`client_max_body_size`限制
3. 目前只支持字符串类型的JSON字段作为hash键
4. 需要确保cJSON库正确安装和链接

## 兼容性

- 支持nginx 1.12及以上版本
- 与其他nginx模块兼容
- 支持nginx reload配置重载

## 贡献和支持

如有问题或建议，请提交issue或pull request。

## 许可证

本模块基于nginx的许可证发布。 
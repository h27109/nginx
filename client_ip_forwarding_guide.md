# 客户端IP传递配置说明

## 概述

在使用IP直接接入IDC的架构中，需要正确传递和记录真实的客户端IP地址，避免在日志中只看到网关IP。

## IP传递流程

```
真实用户 -> 云端网关 -> IDC反向代理 -> 应用服务器
   IP1       IP2        IP3           IP4
```

### 头部字段说明

| 头部字段 | 说明 | 示例值 |
|---------|------|-------|
| `X-Original-IP` | 原始客户端IP（云端网关记录） | 120.132.145.88 |
| `X-Real-IP` | 当前认为的真实客户端IP | 120.132.145.88 |
| `X-Forwarded-For` | 完整的转发链 | 120.132.145.88, 47.98.123.45 |
| `X-Gateway-IP` | 云端网关IP | 47.98.123.45 |
| `X-IDC-Gateway-IP` | IDC网关IP | 123.125.114.144 |

## 配置要点

### 1. 云端网关配置 (multi_idc_public_gateway.conf)

```nginx
# 在代理时设置关键头部
proxy_set_header X-Real-IP $remote_addr;
proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
proxy_set_header X-Original-IP $remote_addr;  # 保存原始客户端IP
proxy_set_header X-Gateway-IP $server_addr;   # 网关自身IP
```

### 2. IDC反向代理配置 (idc_reverse_proxy.conf)

```nginx
# 获取真实客户端IP的映射
map $http_x_original_ip $real_client_ip {
    default $http_x_original_ip;
    "" $remote_addr;  # 如果没有X-Original-IP头，使用remote_addr
}

# 日志格式 - 使用真实客户端IP作为第一个字段
log_format idc_log '$http_x_original_ip - $remote_user [$time_local] "$request" '
                   'real_client_ip="$http_x_original_ip" '
                   'gateway_ip="$http_x_gateway_ip" '
                   'forwarded_for="$http_x_forwarded_for" ';

# 向应用服务器传递时使用真实客户端IP
proxy_set_header X-Real-IP $real_client_ip;  # 传递真实客户端IP
proxy_set_header X-Original-Client-IP $real_client_ip;
```

## 应用服务器获取真实IP

### Java应用示例

```java
// 优先级顺序获取真实客户端IP
public String getRealClientIP(HttpServletRequest request) {
    String ip = request.getHeader("X-Original-Client-IP");
    if (ip == null || ip.isEmpty()) {
        ip = request.getHeader("X-Real-IP");
    }
    if (ip == null || ip.isEmpty()) {
        ip = request.getHeader("X-Forwarded-For");
        if (ip != null && ip.contains(",")) {
            ip = ip.split(",")[0].trim();
        }
    }
    if (ip == null || ip.isEmpty()) {
        ip = request.getRemoteAddr();
    }
    return ip;
}
```

### Python Flask示例

```python
from flask import request

def get_real_client_ip():
    # 优先级顺序获取
    ip = request.headers.get('X-Original-Client-IP')
    if not ip:
        ip = request.headers.get('X-Real-IP')
    if not ip:
        forwarded_for = request.headers.get('X-Forwarded-For')
        if forwarded_for:
            ip = forwarded_for.split(',')[0].strip()
    if not ip:
        ip = request.remote_addr
    return ip
```

### Node.js Express示例

```javascript
function getRealClientIP(req) {
    return req.headers['x-original-client-ip'] ||
           req.headers['x-real-ip'] ||
           req.headers['x-forwarded-for']?.split(',')[0]?.trim() ||
           req.connection.remoteAddress ||
           req.socket.remoteAddress ||
           req.ip;
}
```

## 日志分析

### 云端网关日志格式

```
120.132.145.88 - - [15/Dec/2023:10:30:45 +0800] "GET /api/user/info HTTP/1.1" 200 1234 
user_id="12345" upstream="123.125.114.144:443" response_time=0.123
```

### IDC反向代理日志格式

```
120.132.145.88 - - [15/Dec/2023:10:30:45 +0800] "GET /api/user/info HTTP/1.1" 200 1234 
real_client_ip="120.132.145.88" gateway_ip="47.98.123.45" 
forwarded_for="120.132.145.88, 47.98.123.45" user_id="12345"
```

## 安全考虑

### 1. 头部验证

```nginx
# 在IDC侧验证来源，防止伪造头部
geo $gateway_allowed {
    default 0;
    47.98.123.45/32 1;  # 只允许可信的网关IP
}

server {
    if ($gateway_allowed = 0) {
        return 403;
    }
    # ... 其他配置
}
```

### 2. IP地址格式验证

```nginx
# 验证IP格式的正则表达式
map $http_x_original_ip $valid_ip {
    default 0;
    ~^(\d{1,3}\.){3}\d{1,3}$ 1;
}
```

### 3. 防止内网IP泄露

```nginx
# 过滤内网IP，防止内网信息泄露
map $http_x_original_ip $is_private_ip {
    default 0;
    ~^10\. 1;
    ~^172\.(1[6-9]|2[0-9]|3[01])\. 1;
    ~^192\.168\. 1;
}
```

## 监控和告警

### 检查IP传递是否正常

```bash
# 检查日志中是否有异常的IP记录
tail -f /var/log/nginx/idc_access.log | grep -E "^(10\.|172\.|192\.168\.)"

# 统计真实客户端IP的地域分布
awk '{print $1}' /var/log/nginx/idc_access.log | sort | uniq -c | sort -nr | head -10
```

### 告警规则

1. 如果日志中大量出现网关IP而非真实客户端IP，说明头部传递有问题
2. 如果X-Original-IP为空，需要检查云端网关配置
3. 如果出现内网IP作为客户端IP，可能存在配置错误

## 故障排除

### 常见问题

1. **日志中显示的都是网关IP**
   - 检查云端网关是否正确设置X-Original-IP头部
   - 检查IDC侧是否正确读取和使用该头部

2. **X-Forwarded-For丢失**
   - 确保使用`$proxy_add_x_forwarded_for`而不是`$remote_addr`
   - 检查是否有中间代理清除了该头部

3. **应用无法获取真实IP**
   - 检查应用代码是否按正确优先级读取头部
   - 确认IDC反向代理正确传递了头部到应用

### 调试方法

```nginx
# 临时添加调试头部
add_header X-Debug-Original-IP $http_x_original_ip;
add_header X-Debug-Gateway-IP $http_x_gateway_ip;
add_header X-Debug-Real-Client-IP $real_client_ip;
```

通过这套配置，可以确保在整个请求链路中正确传递和记录真实的客户端IP地址。 
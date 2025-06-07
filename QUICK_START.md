# Nginx JSON Hash 模块 - 快速开始指南

## 一分钟体验

如果你想快速体验nginx JSON hash模块的功能，请按照以下步骤：

### 1. 一键安装

```bash
# 克隆或下载代码
git clone <repository-url>
cd nginx-json-hash-module

# 一键安装（包含依赖、编译、安装）
sudo make install-all
```

### 2. 快速测试

```bash
# 运行自动化测试
./test_json_hash.sh
```

## 详细安装步骤

### 前置要求

- Linux系统 (Ubuntu 18.04+, CentOS 7+ 或其他兼容发行版)
- root权限或sudo权限
- 网络连接 (用于下载nginx源码和依赖)

### 步骤1: 安装依赖

```bash
# Ubuntu/Debian
sudo apt-get update
sudo apt-get install -y build-essential libpcre3-dev libssl-dev zlib1g-dev libcjson-dev wget

# CentOS/RHEL
sudo yum groupinstall -y "Development Tools"
sudo yum install -y pcre-devel openssl-devel zlib-devel libcjson-devel wget

# 或者使用Makefile自动安装
make deps
```

### 步骤2: 编译安装

```bash
# 下载并编译nginx（包含JSON hash模块）
make download
make build
sudo make install

# 创建系统服务
sudo make install-service
```

### 步骤3: 配置nginx

```bash
# 创建基本配置
sudo make create-example-config

# 编辑主配置文件
sudo nano /etc/nginx/nginx.conf
```

基本配置示例：
```nginx
user nginx;
worker_processes auto;
error_log /var/log/nginx/error.log;
pid /var/run/nginx.pid;

events {
    worker_connections 1024;
}

http {
    include /etc/nginx/mime.types;
    default_type application/octet-stream;
    
    # 包含JSON hash示例配置
    include /etc/nginx/conf.d/*.conf;
}
```

### 步骤4: 启动nginx

```bash
# 测试配置
sudo nginx -t

# 启动nginx
sudo systemctl start nginx
sudo systemctl enable nginx

# 检查状态
sudo systemctl status nginx
```

## 使用示例

### 基本配置

在 `/etc/nginx/conf.d/api.conf` 中添加：

```nginx
upstream api_backend {
    json_hash user_id;
    
    server 192.168.1.10:8080;
    server 192.168.1.11:8080;
    server 192.168.1.12:8080;
}

server {
    listen 80;
    server_name api.example.com;
    
    location /api/ {
        proxy_pass http://api_backend;
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_request_buffering on;
    }
}
```

### 测试请求

```bash
# 发送JSON请求
curl -X POST http://api.example.com/api/login \
  -H "Content-Type: application/json" \
  -d '{
    "user_id": "12345",
    "username": "john_doe",
    "password": "secret"
  }'
```

相同的 `user_id` 会被路由到相同的后端服务器。

### 一致性Hash配置

如果你需要更好的分布稳定性：

```nginx
upstream api_backend_consistent {
    json_hash user_id consistent;
    
    server 192.168.1.10:8080 weight=3;
    server 192.168.1.11:8080 weight=2;
    server 192.168.1.12:8080 weight=1;
}
```

## 验证安装

### 1. 检查模块是否加载

```bash
nginx -V 2>&1 | grep -o "json_hash"
```

如果输出 `json_hash`，说明模块已正确加载。

### 2. 运行自动化测试

```bash
# 运行完整测试套件
./test_json_hash.sh

# 或者运行单项测试
./test_json_hash.sh --consistency   # 一致性测试
./test_json_hash.sh --distribution  # 分布测试
./test_json_hash.sh --performance   # 性能测试
```

### 3. 手动测试

```bash
# 发送测试请求
for i in {1..5}; do
  curl -X POST http://localhost/api/test \
    -H "Content-Type: application/json" \
    -d "{\"user_id\": \"user_$i\", \"action\": \"test\"}"
  echo ""
done
```

观察响应，相同的user_id应该总是路由到同一台服务器。

## 常见问题

### Q: 编译时找不到cJSON库

A: 确保安装了libcjson开发包：
```bash
# Ubuntu/Debian
sudo apt-get install libcjson-dev

# CentOS/RHEL  
sudo yum install libcjson-devel
```

### Q: nginx启动失败

A: 检查配置文件语法和权限：
```bash
sudo nginx -t                    # 检查配置语法
sudo chown -R nginx:nginx /var/cache/nginx  # 检查权限
```

### Q: JSON解析失败

A: 确保：
1. 请求头包含 `Content-Type: application/json`
2. 请求体是有效的JSON格式
3. 包含配置的hash字段

### Q: 负载不均衡

A: 这是正常的，hash算法会根据字段值分布，相同的值总是路由到同一台服务器。如果需要更均匀的分布，可以：
1. 使用一致性hash: `json_hash user_id consistent`
2. 调整服务器权重
3. 增加服务器数量

## 监控和调试

### 启用调试日志

```nginx
error_log /var/log/nginx/error.log debug;
```

### 查看分流日志

```bash
# 查看最新的调试日志
sudo tail -f /var/log/nginx/error.log | grep "json hash"
```

### 监控后端服务器状态

可以添加nginx status模块来监控upstream状态：

```nginx
location /nginx_status {
    stub_status on;
    access_log off;
    allow 127.0.0.1;
    deny all;
}
```

## 下一步

- 阅读完整的 [README.md](json_hash_module_README.md)
- 查看详细的配置示例 [json_hash_module_example.conf](json_hash_module_example.conf)
- 根据你的业务需求调整hash字段和后端服务器配置

## 获取帮助

如果遇到问题：

1. 检查nginx错误日志: `sudo tail -f /var/log/nginx/error.log`
2. 运行测试脚本: `./test_json_hash.sh`
3. 查看配置示例和文档
4. 提交issue到项目仓库

## 卸载

如果需要卸载：

```bash
# 停止nginx
sudo systemctl stop nginx
sudo systemctl disable nginx

# 删除文件
sudo rm -rf /usr/local/nginx
sudo rm -f /etc/systemd/system/nginx.service
sudo userdel nginx

# 清理编译文件
make distclean
``` 
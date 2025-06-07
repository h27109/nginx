# JSON Hash 模块故障排除指南

## 🔧 常见问题解决

### 1. 编译相关问题

#### 问题：缺少cJSON库
```
error: cjson/cJSON.h: No such file or directory
```

**解决方案：**
```bash
# Ubuntu/Debian
sudo apt-get install libcjson-dev

# CentOS/RHEL
sudo yum install libcjson-devel
# 或
sudo dnf install libcjson-devel
```

#### 问题：模块未被编译进nginx
```
nginx: [emerg] unknown directive "json_hash"
```

**解决方案：**
1. 检查nginx版本信息：
```bash
nginx -V
```
2. 确认模块源码位于正确位置：
```bash
ls -la src/http/modules/ngx_http_upstream_json_hash_module.c
```

### 2. 配置相关问题

#### 问题：JSON解析失败
```
json_hash: failed to parse JSON body
```

**可能原因：**
- Content-Type不是application/json
- JSON格式不正确
- 请求体为空

**解决方案：**
```bash
# 确保设置正确的Content-Type
curl -H "Content-Type: application/json" \
     -d '{"user_id": "12345"}' \
     http://your-server/api

# 检查JSON格式
echo '{"user_id": "12345"}' | jq .
```

#### 问题：指定的JSON字段不存在
```
json_hash: field 'user_id' not found in JSON
```

**解决方案：**
- 配置fallback_key：
```nginx
upstream backend {
    json_hash user_id;
    json_hash_fallback_key "default_key";
    
    server backend1.example.com;
    server backend2.example.com;
}
```

### 3. 性能相关问题

#### 问题：负载均衡不均匀

**解决方案：**
1. 启用一致性哈希：
```nginx
json_hash user_id consistent;
```

2. 调整虚拟节点数：
```nginx
json_hash_virtual_nodes 500;
```

3. 更换哈希算法：
```nginx
json_hash_method murmur3;
```

#### 问题：内存使用过高

**解决方案：**
```nginx
# 限制JSON体大小
json_hash_max_size 1m;

# 限制虚拟节点内存
json_hash_max_virtual_memory 2m;

# 限制JSON嵌套深度
json_hash_max_depth 16;
```

### 4. 调试技巧

#### 启用详细日志
```nginx
# 在http段添加
error_log /var/log/nginx/debug.log debug;

# 在location段添加
access_log /var/log/nginx/json_hash.log combined;
```

#### 查看性能统计
```nginx
# 启用统计信息（每30秒输出一次）
json_hash_stats_interval 30000;
```

#### 测试配置
```bash
# 测试配置语法
nginx -t

# 重新加载配置
nginx -s reload

# 查看错误日志
tail -f /var/log/nginx/error.log
```

## 📊 性能优化建议

### 1. 选择合适的哈希算法
- **CRC32**: 速度快，分布均匀
- **MurmurHash3**: 更好的分布性，稍慢

### 2. 调整虚拟节点数
- **少于100个节点**: 160个虚拟节点
- **100-500个节点**: 500个虚拟节点
- **500+个节点**: 1000个虚拟节点

### 3. 合理设置限制
```nginx
upstream backend {
    json_hash user_id consistent;
    
    # 性能配置
    json_hash_virtual_nodes 500;
    json_hash_method murmur3;
    json_hash_max_size 2m;
    json_hash_max_depth 16;
    
    # 安全配置
    json_hash_check_content_type on;
    json_hash_fallback_key "guest";
    
    # 监控配置
    json_hash_stats_interval 30000;
    
    server backend1.example.com weight=3;
    server backend2.example.com weight=2;
    server backend3.example.com weight=1;
}
```

## 🚨 告警说明

### 统计重置告警
```
json_hash stats[worker:12345]: frequent stats resets detected (11 times)
```
**含义**: 统计数据频繁重置，可能是系统负载过高
**处理**: 检查系统资源使用情况，考虑优化配置

### 内存限制告警
```
json_hash: virtual nodes memory limit exceeded
```
**含义**: 虚拟节点内存使用超限
**处理**: 增加 `json_hash_max_virtual_memory` 限制或减少 `json_hash_virtual_nodes`

## 📞 获取帮助

1. **查看日志**: 检查nginx错误日志和访问日志
2. **配置验证**: 使用 `nginx -t` 验证配置语法
3. **性能监控**: 启用统计信息监控模块性能
4. **测试工具**: 使用提供的测试脚本验证功能

---

**最后更新**: 2025年6月
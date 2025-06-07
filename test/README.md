# nginx upstream json hash 模块测试套件

这是一个完整的测试套件，用于验证 nginx upstream json hash 模块的功能、性能和稳定性。

## 测试文件说明

### 1. 单元测试
- **文件**: `test_ngx_http_upstream_json_hash_module.c`
- **功能**: 测试模块的核心功能和各种边界情况
- **覆盖场景**:
  - 正常JSON字符串/数字字段提取
  - 空请求体和NULL请求体处理
  - 无效JSON格式处理
  - 字段不存在和空值处理
  - 不支持的字段类型（数组、布尔、NULL）
  - 超大请求体限制测试
  - 文件存储的请求体处理
  - 配置验证（空字段名、过长字段名、无效参数）
  - 嵌套JSON和特殊字符处理

### 2. 压力测试
- **文件**: `stress_test.c`
- **功能**: 测试模块在高负载下的性能和稳定性
- **覆盖场景**:
  - 并发请求测试（多线程）
  - 内存压力测试（大量连续请求）
  - 大JSON数据处理测试
  - 异常边界情况批量测试
  - 性能基准测试

### 3. 构建和运行工具
- **文件**: `Makefile`
- **功能**: 自动化编译、运行和分析测试
- **支持操作**:
  - 编译测试程序
  - 运行单元测试和压力测试
  - 内存泄漏检查（Valgrind）
  - 代码覆盖率分析（gcov）
  - 性能测试

## 使用方法

### 1. 环境准备

#### Ubuntu/Debian 系统
```bash
# 安装依赖
make install-deps

# 检查依赖是否正确安装
make check-deps
```

#### 手动安装依赖
```bash
# 安装libcjson开发库
sudo apt-get install libcjson-dev

# 安装测试工具（可选）
sudo apt-get install valgrind gcov
```

### 2. 编译测试程序

```bash
# 编译所有测试
make compile

# 或者分别编译
gcc -Wall -Wextra -std=c99 -g -O0 -I/usr/include/cjson -o test_json_hash_module test_ngx_http_upstream_json_hash_module.c -lcjson -lm
gcc -Wall -Wextra -std=c99 -g -O0 -I/usr/include/cjson -o stress_test stress_test.c -lcjson -lm -lpthread
```

### 3. 运行测试

#### 基本测试
```bash
# 运行单元测试
make test

# 运行压力测试
./stress_test
```

#### 内存检查
```bash
# 使用Valgrind检查内存泄漏
make memtest
```

#### 性能分析
```bash
# 运行性能测试
make perf
```

#### 代码覆盖率
```bash
# 生成代码覆盖率报告
make coverage
```

### 4. 清理
```bash
# 清理生成的文件
make clean
```

## 测试用例详细说明

### 单元测试用例

| 测试用例 | 描述 | 期望结果 |
|---------|------|---------|
| test_normal_json_string_field | 正常JSON字符串字段提取 | 成功提取字符串值 |
| test_normal_json_number_field | 正常JSON数字字段提取 | 成功提取并转换为字符串 |
| test_empty_body | 空请求体处理 | 返回NULL，记录警告日志 |
| test_null_body | NULL请求体处理 | 返回NULL，记录警告日志 |
| test_invalid_json | 无效JSON格式 | 返回NULL，记录错误日志 |
| test_missing_field | 字段不存在 | 返回NULL，记录警告日志 |
| test_empty_field_value | 字段值为空字符串 | 返回NULL，记录警告日志 |
| test_unsupported_field_type | 不支持的字段类型 | 返回NULL，记录警告日志 |
| test_large_body | 超大请求体 | 返回NULL，记录错误日志 |
| test_file_based_body | 文件存储的请求体 | 正确读取文件并提取字段 |
| test_config_validation | 配置参数验证 | 正确验证各种配置参数 |
| test_nested_json | 嵌套JSON处理 | 正确提取顶级字段 |
| test_special_characters | 特殊字符处理 | 正确处理特殊字符和中文 |
| test_boolean_field | 布尔类型字段 | 返回NULL（不支持） |
| test_null_field | NULL字段值 | 返回NULL（不支持） |

### 压力测试用例

| 测试类型 | 描述 | 测试参数 |
|---------|------|---------|
| 并发测试 | 多线程并发请求 | 10个线程，每线程1000请求 |
| 内存压力测试 | 大量连续请求 | 10000个连续请求 |
| 大JSON测试 | 大JSON数据处理 | 10KB JSON，100次测试 |
| 异常边界测试 | 各种异常情况 | 14种边界情况 |
| 性能基准测试 | 性能基准测试 | 50000次标准请求 |

## 预期性能指标

### 正常情况下的性能指标
- **平均响应时间**: < 1ms
- **QPS**: > 10000
- **内存使用**: 稳定，无泄漏
- **成功率**: > 99%

### 异常情况处理
- **无效JSON**: 优雅失败，返回NULL
- **内存不足**: 优雅失败，记录错误日志
- **文件读取失败**: 优雅失败，记录错误日志
- **超大请求**: 拒绝处理，记录错误日志

## 问题排查

### 常见问题

1. **编译错误**
   ```
   error: cjson/cJSON.h: No such file or directory
   ```
   解决：安装libcjson开发库
   ```bash
   sudo apt-get install libcjson-dev
   ```

2. **链接错误**
   ```
   undefined reference to `cJSON_Parse'
   ```
   解决：确保链接了cjson库
   ```bash
   gcc ... -lcjson
   ```

3. **内存泄漏**
   如果Valgrind报告内存泄漏，检查：
   - `cJSON_Delete()` 是否正确调用
   - 测试框架的内存清理是否完整

4. **测试失败**
   检查错误日志，常见原因：
   - JSON格式错误
   - 字段名不匹配
   - 数据类型不支持

### 调试技巧

1. **启用详细日志**
   ```c
   // 在测试代码中添加更多调试信息
   printf("JSON: %s\n", json_data);
   printf("Field: %s\n", field_name);
   ```

2. **使用GDB调试**
   ```bash
   gcc -g -O0 ...
   gdb ./test_json_hash_module
   ```

3. **Valgrind详细输出**
   ```bash
   valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes ./test_json_hash_module
   ```

## 持续集成

### GitHub Actions 示例配置

```yaml
name: Test nginx json hash module

on: [push, pull_request]

jobs:
  test:
    runs-on: ubuntu-latest
    steps:
    - uses: actions/checkout@v2
    - name: Install dependencies
      run: |
        sudo apt-get update
        sudo apt-get install -y libcjson-dev valgrind gcov
    - name: Run tests
      run: |
        cd test
        make test
        make memtest
        make coverage
```

## 扩展测试

### 添加新测试用例

1. 在 `test_ngx_http_upstream_json_hash_module.c` 中添加新的测试函数
2. 在 `main()` 函数中调用新测试
3. 更新此文档

### 性能测试扩展

1. 修改 `stress_test.c` 中的测试参数
2. 添加新的压力测试场景
3. 集成性能监控工具

---

**注意**: 这些测试仅针对模块的核心功能。在实际nginx环境中部署前，还需要进行集成测试和生产环境验证。 
# ngx_http_upstream_json_hash_module 单元测试框架

该测试框架为Nginx的`ngx_http_upstream_json_hash_module`模块提供了单元测试能力。该模块是一个负载均衡模块，基于JSON请求体中特定字段的值进行哈希分流。

## 设计原则

1. **直接测试原始代码**：测试直接访问模块原始代码，而不是复制实现，保证测试的有效性
2. **隔离依赖**：模拟Nginx环境和cJSON库，避免完整Nginx编译环境的依赖
3. **模块化测试**：将测试分为多个独立单元，每个单元专注于特定功能
4. **包装层设计**：通过包装层访问模块内部函数，避免侵入性修改

## 目录结构

```
unit_test/
├── build/                  # 构建输出目录
├── include/                # 测试头文件
│   ├── nginx_stub.h        # Nginx环境模拟
│   ├── cjson_stub.h        # cJSON库模拟
│   ├── test_utils.h        # 测试辅助函数
│   └── ngx_http_upstream_json_hash.h  # 模块头文件
├── src/                    # 测试支持代码
│   ├── module_wrapper.c    # 模块包装层
│   ├── test_utils.c        # 测试辅助函数实现
│   ├── mock_nginx.c        # Nginx函数模拟实现
│   └── mock_cjson.c        # cJSON函数模拟实现
└── tests/                  # 测试用例
    ├── test_hash_calculations.c  # 哈希计算测试
    ├── test_json_extraction.c    # JSON解析测试
    └── test_consistent_hash.c    # 一致性哈希测试
```

## 主要组件

1. **模块包装层**：`module_wrapper.c` 提供了访问模块内部函数的接口，避免直接修改模块代码
2. **Nginx环境模拟**：`nginx_stub.h` 定义了测试所需的Nginx类型和函数
3. **测试用例**：每个测试文件专注于模块的特定功能

## 测试功能

1. **哈希计算测试**：验证模块的哈希计算功能，包括CRC32和MurmurHash3算法
2. **JSON解析测试**：验证从JSON提取字段功能
3. **一致性哈希测试**：验证一致性哈希算法功能

## 构建和运行

### 前提条件

- CMake 3.10+
- C99兼容编译器
- cJSON库
- Internet连接（首次构建时会下载CMocka）

### 构建步骤

```bash
mkdir -p build && cd build
cmake ..
make
```

### 运行测试

```bash
# 运行所有测试
make test

# 运行单个测试
./bin/test_hash_calculations
./bin/test_json_extraction
./bin/test_consistent_hash
```

## 实现细节

1. **CMocka框架**：使用CMocka作为C单元测试框架
2. **模块访问**：通过包装层访问模块内部函数，避免代码复制
3. **内存管理**：测试环境实现简化版的内存分配函数，模拟Nginx的内存池
4. **错误模拟**：测试覆盖各种错误情况和边界条件

## 测试覆盖范围

测试覆盖了模块的主要功能：

1. **哈希计算功能**
   - CRC32 哈希计算
   - MurmurHash3 哈希计算
   - 不同长度和内容的数据哈希计算

2. **JSON 处理功能**
   - JSON 字段提取
   - JSON 深度验证
   - Content-Type 检查
   - 不同 JSON 数据类型的处理
   - Fallback key 机制

3. **一致性哈希功能**
   - 一致性哈希点比较和排序
   - 哈希点查找
   - 请求分布验证

## 添加新测试

1. 在 `tests/` 目录创建新的测试文件
2. 更新 `tests/CMakeLists.txt` 添加新的测试源文件
3. 实现测试用例，使用 CMocka 的 `cmocka_unit_test()` 注册测试函数

## 注意事项

- 测试使用模拟的 Nginx 和 cJSON 环境，而不是实际的库
- 测试框架自动管理内存分配和释放
- 所有测试函数应遵循 CMocka 的签名规范 `void test_func(void **state)` 
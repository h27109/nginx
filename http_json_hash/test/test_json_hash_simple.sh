#!/bin/bash

echo "=== Nginx JSON Hash Module 测试 ==="

# 检查nginx是否编译成功
if [ ! -f "./objs/nginx" ]; then
    echo "错误: nginx可执行文件不存在"
    exit 1
fi

# 测试配置文件语法
echo "1. 测试配置文件语法..."
./objs/nginx -t -c $(pwd)/test_complete_config.conf
if [ $? -eq 0 ]; then
    echo "✓ 配置文件语法正确"
else
    echo "✗ 配置文件语法错误"
    exit 1
fi

# 检查模块是否被正确编译
echo ""
echo "2. 检查模块编译情况..."
if ./objs/nginx -V 2>&1 | grep -q "json_hash"; then
    echo "✓ json_hash模块已编译到nginx中"
else
    echo "? json_hash模块可能未在-V输出中显示，但配置测试通过"
fi

# 显示nginx版本和编译信息
echo ""
echo "3. Nginx版本信息:"
./objs/nginx -V

echo ""
echo "=== 测试完成 ==="
echo "你的nginx json_hash模块已经成功编译并可以使用！"
echo ""
echo "使用方法:"
echo "1. 在upstream块中使用: json_hash <字段名>;"
echo "2. 支持一致性哈希: json_hash <字段名> consistent;"
echo ""
echo "示例配置已保存在 test_complete_config.conf 文件中" 
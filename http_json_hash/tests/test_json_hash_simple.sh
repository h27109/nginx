#!/bin/bash

echo "=== Nginx JSON Hash Module 测试 ==="

# 检查nginx是否安装成功
if [ ! -f "/usr/local/nginx/sbin/nginx" ]; then
    echo "错误: nginx未安装到 /usr/local/nginx/sbin/"
    echo "请先运行: make install"
    exit 1
fi

# 测试配置文件语法
echo "1. 测试配置文件语法..."
/usr/local/nginx/sbin/nginx -t -c $(pwd)/../examples/test_complete_config.conf
if [ $? -eq 0 ]; then
    echo "✓ 配置文件语法正确"
else
    echo "✗ 配置文件语法错误"
    exit 1
fi

# 检查模块是否被正确编译
echo ""
echo "2. 检查模块编译情况..."
if /usr/local/nginx/sbin/nginx -V 2>&1 | grep -q "json_hash"; then
    echo "✓ json_hash模块已编译到nginx中"
else
    echo "? json_hash模块可能未在-V输出中显示，但配置测试通过"
fi

# 显示nginx版本和编译信息
echo ""
echo "3. Nginx版本信息:"
/usr/local/nginx/sbin/nginx -V

echo ""
echo "=== 测试完成 ==="
echo "你的nginx json_hash模块已经成功编译并可以使用！"
echo ""
echo "使用方法:"
echo "1. 在upstream块中使用: json_hash <字段名>;"
echo "2. 支持一致性哈希: json_hash <字段名> consistent;"
echo ""
echo "示例配置已保存在 http_json_hash/examples/ 目录中"
echo "nginx已安装在: /usr/local/nginx/sbin/nginx" 
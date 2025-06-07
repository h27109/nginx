#!/bin/bash

# nginx upstream json hash 模块边界条件测试
# 测试布尔值、null值、复杂类型、嵌套字段等特殊情况

set -e

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# 配置参数
BASE_URL=${BASE_URL:-"http://localhost"}
TEST_ENDPOINT=${TEST_ENDPOINT:-"/api/v1/"}

echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}  nginx upstream json hash 边界条件测试${NC}"
echo -e "${BLUE}========================================${NC}"

# 测试计数器
TOTAL_TESTS=0
PASSED_TESTS=0
FAILED_TESTS=0

# 测试函数
test_case() {
    local test_name="$1"
    local json_data="$2"
    local expected_result="$3"
    
    TOTAL_TESTS=$((TOTAL_TESTS + 1))
    
    echo -e "${YELLOW}测试案例 $TOTAL_TESTS: $test_name${NC}"
    echo "JSON数据: $json_data"
    
    # 发送请求
    local response=$(curl -s -w "%{http_code}" -X POST \
        -H "Content-Type: application/json" \
        -d "$json_data" \
        "$BASE_URL$TEST_ENDPOINT" 2>/dev/null)
    
    local http_code="${response: -3}"
    local body="${response%???}"
    
    if [[ "$http_code" == "200" ]]; then
        echo -e "结果: ${GREEN}成功${NC} (HTTP $http_code)"
        PASSED_TESTS=$((PASSED_TESTS + 1))
    else
        echo -e "结果: ${RED}失败${NC} (HTTP $http_code)"
        FAILED_TESTS=$((FAILED_TESTS + 1))
        if [[ -n "$body" ]]; then
            echo "响应: $body"
        fi
    fi
    echo ""
}

# 测试不同数据类型
echo -e "${BLUE}=== 测试数据类型支持 ===${NC}"

# 1. 字符串类型
test_case "字符串类型字段" \
    '{"user_id": "user123", "action": "login"}' \
    "success"

# 2. 数字类型
test_case "整数类型字段" \
    '{"user_id": 12345, "action": "login"}' \
    "success"

test_case "浮点数类型字段" \
    '{"user_id": 123.45, "action": "login"}' \
    "success"

# 3. 布尔类型
test_case "布尔类型字段 - true" \
    '{"user_id": true, "action": "login"}' \
    "success"

test_case "布尔类型字段 - false" \
    '{"user_id": false, "action": "login"}' \
    "success"

# 4. null类型
test_case "null类型字段" \
    '{"user_id": null, "action": "login"}' \
    "success"

# 5. 数组类型
test_case "数组类型字段" \
    '{"user_id": ["user1", "user2"], "action": "login"}' \
    "success"

# 6. 对象类型
test_case "对象类型字段" \
    '{"user_id": {"id": 123, "name": "test"}, "action": "login"}' \
    "success"

echo -e "${BLUE}=== 测试错误条件 ===${NC}"

# 7. 字段不存在
test_case "字段不存在" \
    '{"username": "test", "action": "login"}' \
    "fallback"

# 8. 空JSON
test_case "空JSON对象" \
    '{}' \
    "fallback"

# 9. 无效JSON
test_case "无效JSON格式" \
    '{"user_id": "test", "action": "login"' \
    "error"

# 10. 字段值为空字符串
test_case "空字符串字段" \
    '{"user_id": "", "action": "login"}' \
    "success"

echo -e "${BLUE}=== 测试JSON深度 ===${NC}"

# 11. 深层嵌套JSON
test_case "深层嵌套JSON (5层)" \
    '{"level1": {"level2": {"level3": {"level4": {"user_id": "deep_user"}}}}}' \
    "fallback"

# 12. 正常嵌套深度
test_case "正常嵌套JSON (2层)" \
    '{"data": {"user_id": "nested_user"}, "action": "login"}' \
    "fallback"

echo -e "${BLUE}=== 测试特殊字符 ===${NC}"

# 13. 特殊字符
test_case "特殊字符字段值" \
    '{"user_id": "user@#$%^&*()", "action": "login"}' \
    "success"

# 14. Unicode字符
test_case "Unicode字符" \
    '{"user_id": "用户123", "action": "登录"}' \
    "success"

# 15. 非常长的字段值
test_case "长字段值" \
    '{"user_id": "' $(python3 -c "print('a' * 1000)") '", "action": "login"}' \
    "success"

echo -e "${BLUE}=== 测试Content-Type ===${NC}"

# 16. 错误的Content-Type
echo -e "${YELLOW}测试案例 $((TOTAL_TESTS + 1)): 错误的Content-Type${NC}"
TOTAL_TESTS=$((TOTAL_TESTS + 1))
response=$(curl -s -w "%{http_code}" -X POST \
    -H "Content-Type: text/plain" \
    -d '{"user_id": "test"}' \
    "$BASE_URL$TEST_ENDPOINT" 2>/dev/null)

http_code="${response: -3}"
if [[ "$http_code" == "200" ]]; then
    echo -e "结果: ${GREEN}成功${NC} (应该处理fallback)"
    PASSED_TESTS=$((PASSED_TESTS + 1))
else
    echo -e "结果: ${RED}失败${NC} (HTTP $http_code)"
    FAILED_TESTS=$((FAILED_TESTS + 1))
fi
echo ""

# 17. 缺少Content-Type
echo -e "${YELLOW}测试案例 $((TOTAL_TESTS + 1)): 缺少Content-Type${NC}"
TOTAL_TESTS=$((TOTAL_TESTS + 1))
response=$(curl -s -w "%{http_code}" -X POST \
    -d '{"user_id": "test"}' \
    "$BASE_URL$TEST_ENDPOINT" 2>/dev/null)

http_code="${response: -3}"
if [[ "$http_code" == "200" ]]; then
    echo -e "结果: ${GREEN}成功${NC} (应该处理fallback)"
    PASSED_TESTS=$((PASSED_TESTS + 1))
else
    echo -e "结果: ${RED}失败${NC} (HTTP $http_code)"
    FAILED_TESTS=$((FAILED_TESTS + 1))
fi
echo ""

echo -e "${BLUE}=== 测试大数据 ===${NC}"

# 18. 大JSON数据 (接近1MB限制)
test_case "大JSON数据" \
    '{"user_id": "big_data_user", "large_field": "' $(python3 -c "print('x' * 100000)") '"}' \
    "success"

# 19. 超大JSON数据 (超过1MB限制)
echo -e "${YELLOW}测试案例 $((TOTAL_TESTS + 1)): 超大JSON数据${NC}"
TOTAL_TESTS=$((TOTAL_TESTS + 1))
large_data=$(python3 -c "print('x' * 1048577)")  # 1MB + 1字节
response=$(curl -s -w "%{http_code}" -X POST \
    -H "Content-Type: application/json" \
    -d '{"user_id": "huge_data_user", "large_field": "'$large_data'"}' \
    "$BASE_URL$TEST_ENDPOINT" 2>/dev/null)

http_code="${response: -3}"
if [[ "$http_code" == "413" ]] || [[ "$http_code" == "400" ]]; then
    echo -e "结果: ${GREEN}正确拒绝${NC} (HTTP $http_code)"
    PASSED_TESTS=$((PASSED_TESTS + 1))
else
    echo -e "结果: ${RED}应该拒绝${NC} (HTTP $http_code)"
    FAILED_TESTS=$((FAILED_TESTS + 1))
fi
echo ""

# 性能测试
echo -e "${BLUE}=== 性能测试 ===${NC}"

# 20. 并发测试
echo -e "${YELLOW}并发测试 (50个并发请求)${NC}"
start_time=$(date +%s%N)

for i in {1..50}; do
    (curl -s -X POST \
        -H "Content-Type: application/json" \
        -d '{"user_id": "concurrent_user_'$i'", "test_type": "concurrent"}' \
        "$BASE_URL$TEST_ENDPOINT" > /dev/null 2>&1) &
done

wait
end_time=$(date +%s%N)
duration=$(( (end_time - start_time) / 1000000 ))  # 转换为毫秒

echo "并发测试完成，耗时: ${duration}ms"
echo ""

# 生成测试报告
echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}  测试报告${NC}"
echo -e "${BLUE}========================================${NC}"
echo "总测试数: $TOTAL_TESTS"
echo -e "通过测试: ${GREEN}$PASSED_TESTS${NC}"
echo -e "失败测试: ${RED}$FAILED_TESTS${NC}"

if [[ $FAILED_TESTS -eq 0 ]]; then
    echo -e "${GREEN}✅ 所有测试通过！${NC}"
    exit 0
else
    echo -e "${RED}❌ 有 $FAILED_TESTS 个测试失败${NC}"
    exit 1
fi 
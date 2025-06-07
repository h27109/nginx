#!/bin/bash

# Nginx JSON Hash Module 综合测试脚本
# 包含功能测试、性能测试、边界测试、错误处理测试

set -e

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# 全局变量
NGINX_HOST="${NGINX_HOST:-localhost}"
NGINX_PORT="${NGINX_PORT:-80}"
TEST_URL="http://${NGINX_HOST}:${NGINX_PORT}/api/test"
BACKEND_SERVERS=("server1" "server2" "server3")
TOTAL_TESTS=0
PASSED_TESTS=0
FAILED_TESTS=0

# 日志函数
log_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

log_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

log_test() {
    echo -e "${BLUE}[TEST]${NC} $1"
}

# 测试结果记录
record_test_result() {
    local test_name="$1"
    local result="$2"
    
    TOTAL_TESTS=$((TOTAL_TESTS + 1))
    
    if [[ "$result" == "PASS" ]]; then
        PASSED_TESTS=$((PASSED_TESTS + 1))
        echo -e "${GREEN}✓${NC} $test_name"
    else
        FAILED_TESTS=$((FAILED_TESTS + 1))
        echo -e "${RED}✗${NC} $test_name - $result"
    fi
}

# 检查依赖
check_dependencies() {
    log_info "Checking dependencies..."
    
    local deps=("curl" "jq" "ab" "timeout")
    for dep in "${deps[@]}"; do
        if ! command -v "$dep" &> /dev/null; then
            log_error "Missing dependency: $dep"
            exit 1
        fi
    done
    
    log_info "All dependencies satisfied"
}

# 检查nginx状态
check_nginx_status() {
    log_info "Checking nginx status..."
    
    if ! curl -s -f "$NGINX_HOST:$NGINX_PORT/health" > /dev/null; then
        log_error "Nginx is not responding at $NGINX_HOST:$NGINX_PORT"
        exit 1
    fi
    
    log_info "Nginx is running and responding"
}

# 发送JSON请求
send_json_request() {
    local json_data="$1"
    local expected_backend="$2"
    local timeout="${3:-5}"
    
    local response=$(timeout "$timeout" curl -s \
        -X POST \
        -H "Content-Type: application/json" \
        -H "X-Test-Request: true" \
        -d "$json_data" \
        "$TEST_URL" 2>/dev/null || echo "TIMEOUT")
    
    echo "$response"
}

# 测试基础JSON哈希功能
test_basic_json_hash() {
    log_test "Testing basic JSON hash functionality"
    
    local test_cases=(
        '{"user_id": "user1", "action": "login"}:user1'
        '{"user_id": "user2", "action": "logout"}:user2'
        '{"user_id": "user3", "action": "update"}:user3'
    )
    
    for case in "${test_cases[@]}"; do
        local json_data="${case%:*}"
        local expected_user="${case#*:}"
        
        local response=$(send_json_request "$json_data")
        
        if [[ "$response" == *"$expected_user"* ]]; then
            record_test_result "Basic hash for $expected_user" "PASS"
        else
            record_test_result "Basic hash for $expected_user" "FAIL: $response"
        fi
        
        sleep 0.1
    done
}

# 测试一致性哈希
test_consistent_hash() {
    log_test "Testing consistent hash distribution"
    
    local user_ids=("user1" "user2" "user3" "user4" "user5")
    local distribution=()
    
    # 初始化分布统计
    for backend in "${BACKEND_SERVERS[@]}"; do
        distribution[$backend]=0
    done
    
    # 发送请求并统计分布
    for user_id in "${user_ids[@]}"; do
        local json_data='{"user_id": "'$user_id'", "action": "test"}'
        local response=$(send_json_request "$json_data")
        
        # 解析响应中的后端服务器信息
        local backend=$(echo "$response" | grep -o 'server[0-9]' | head -1)
        if [[ -n "$backend" ]]; then
            distribution[$backend]=$((distribution[$backend] + 1))
        fi
    done
    
    # 检查分布是否合理（每个后端至少处理一个请求）
    local active_backends=0
    for backend in "${BACKEND_SERVERS[@]}"; do
        if [[ ${distribution[$backend]} -gt 0 ]]; then
            active_backends=$((active_backends + 1))
        fi
    done
    
    if [[ $active_backends -ge 2 ]]; then
        record_test_result "Consistent hash distribution" "PASS"
    else
        record_test_result "Consistent hash distribution" "FAIL: Only $active_backends backends active"
    fi
}

# 测试数据类型支持
test_data_types() {
    log_test "Testing different JSON data types"
    
    local test_cases=(
        '{"user_id": "string_user", "type": "string"}:string'
        '{"user_id": 12345, "type": "number"}:number'
        '{"user_id": true, "type": "boolean"}:boolean'
        '{"user_id": null, "type": "null"}:null'
    )
    
    for case in "${test_cases[@]}"; do
        local json_data="${case%:*}"
        local data_type="${case#*:}"
        
        local response=$(send_json_request "$json_data")
        
        if [[ "$response" != "TIMEOUT" && "$response" != "" ]]; then
            record_test_result "Data type support: $data_type" "PASS"
        else
            record_test_result "Data type support: $data_type" "FAIL: $response"
        fi
        
        sleep 0.1
    done
}

# 测试fallback机制
test_fallback_mechanism() {
    log_test "Testing fallback mechanism"
    
    # 测试缺少字段的情况
    local json_data='{"action": "login", "timestamp": "2024-01-01"}'
    local response=$(send_json_request "$json_data")
    
    if [[ "$response" != "TIMEOUT" && "$response" != "" ]]; then
        record_test_result "Fallback for missing field" "PASS"
    else
        record_test_result "Fallback for missing field" "FAIL: $response"
    fi
    
    # 测试无效JSON
    local invalid_json='{"user_id": "test", "invalid":'
    local response=$(send_json_request "$invalid_json")
    
    if [[ "$response" != "TIMEOUT" && "$response" != "" ]]; then
        record_test_result "Fallback for invalid JSON" "PASS"
    else
        record_test_result "Fallback for invalid JSON" "FAIL: $response"
    fi
}

# 测试Content-Type检查
test_content_type_validation() {
    log_test "Testing Content-Type validation"
    
    # 正确的Content-Type
    local response1=$(curl -s -X POST \
        -H "Content-Type: application/json" \
        -d '{"user_id": "test", "action": "login"}' \
        "$TEST_URL")
    
    if [[ "$response1" != "" ]]; then
        record_test_result "Valid Content-Type" "PASS"
    else
        record_test_result "Valid Content-Type" "FAIL: Empty response"
    fi
    
    # 错误的Content-Type
    local response2=$(curl -s -X POST \
        -H "Content-Type: text/plain" \
        -d '{"user_id": "test", "action": "login"}' \
        "$TEST_URL")
    
    # 应该返回错误或使用fallback
    if [[ "$response2" != "" ]]; then
        record_test_result "Invalid Content-Type handling" "PASS"
    else
        record_test_result "Invalid Content-Type handling" "FAIL: No response"
    fi
}

# 测试JSON深度限制
test_json_depth_limit() {
    log_test "Testing JSON depth limits"
    
    # 正常深度的JSON
    local normal_json='{"user_id": "test", "data": {"level1": {"level2": "value"}}}'
    local response1=$(send_json_request "$normal_json")
    
    if [[ "$response1" != "TIMEOUT" && "$response1" != "" ]]; then
        record_test_result "Normal JSON depth" "PASS"
    else
        record_test_result "Normal JSON depth" "FAIL: $response1"
    fi
    
    # 构造深度嵌套的JSON（超过限制）
    local deep_json='{"user_id": "test", "data": '
    for i in {1..40}; do
        deep_json+='"level'$i'": {'
    done
    deep_json+='"value": "deep"'
    for i in {1..40}; do
        deep_json+='}'
    done
    deep_json+='}'
    
    local response2=$(send_json_request "$deep_json" "" 10)
    
    # 深度过大的JSON应该被拒绝或使用fallback
    if [[ "$response2" != "TIMEOUT" ]]; then
        record_test_result "Deep JSON handling" "PASS"
    else
        record_test_result "Deep JSON handling" "FAIL: Timeout"
    fi
}

# 测试大JSON处理
test_large_json_handling() {
    log_test "Testing large JSON handling"
    
    # 构造大JSON
    local large_json='{"user_id": "test", "data": "'
    large_json+=$(head -c 1048576 /dev/zero | tr '\0' 'a')  # 1MB of 'a'
    large_json+='"}'
    
    local response=$(send_json_request "$large_json" "" 30)
    
    if [[ "$response" != "TIMEOUT" ]]; then
        record_test_result "Large JSON handling" "PASS"
    else
        record_test_result "Large JSON handling" "TIMEOUT"
    fi
}

# 性能测试
test_performance() {
    log_test "Running performance tests"
    
    # 创建测试JSON文件
    local test_json='{"user_id": "perf_test_user", "action": "performance_test", "timestamp": "'$(date -Iseconds)'"}'
    echo "$test_json" > /tmp/test_json.txt
    
    # 使用Apache Bench进行性能测试
    local ab_result=$(ab -n 1000 -c 10 -p /tmp/test_json.txt -T application/json "$TEST_URL" 2>/dev/null | grep "Requests per second" | awk '{print $4}')
    
    if [[ -n "$ab_result" ]] && (( $(echo "$ab_result > 100" | bc -l) )); then
        record_test_result "Performance test (>100 RPS)" "PASS: ${ab_result} RPS"
    else
        record_test_result "Performance test (>100 RPS)" "FAIL: ${ab_result} RPS"
    fi
    
    # 清理
    rm -f /tmp/test_json.txt
}

# 并发测试
test_concurrent_requests() {
    log_test "Testing concurrent requests"
    
    local concurrent_level=20
    local pids=()
    
    # 启动并发请求
    for i in $(seq 1 $concurrent_level); do
        (
            local json_data='{"user_id": "concurrent_user_'$i'", "action": "concurrent_test"}'
            send_json_request "$json_data" >/dev/null 2>&1
        ) &
        pids+=($!)
    done
    
    # 等待所有请求完成
    local completed=0
    for pid in "${pids[@]}"; do
        if wait "$pid" 2>/dev/null; then
            completed=$((completed + 1))
        fi
    done
    
    if [[ $completed -ge $((concurrent_level * 8 / 10)) ]]; then  # 80%成功率
        record_test_result "Concurrent requests (${concurrent_level} threads)" "PASS: ${completed}/${concurrent_level} completed"
    else
        record_test_result "Concurrent requests (${concurrent_level} threads)" "FAIL: ${completed}/${concurrent_level} completed"
    fi
}

# 错误恢复测试
test_error_recovery() {
    log_test "Testing error recovery"
    
    # 发送一些无效请求
    local invalid_requests=(
        ''
        'invalid json'
        '{"incomplete": '
        '{"empty_user_id": "", "action": "test"}'
    )
    
    local recovery_success=0
    
    for invalid_json in "${invalid_requests[@]}"; do
        send_json_request "$invalid_json" >/dev/null 2>&1
        
        # 发送正常请求验证恢复
        local normal_json='{"user_id": "recovery_test", "action": "test"}'
        local response=$(send_json_request "$normal_json")
        
        if [[ "$response" != "TIMEOUT" && "$response" != "" ]]; then
            recovery_success=$((recovery_success + 1))
        fi
        
        sleep 0.1
    done
    
    if [[ $recovery_success -eq ${#invalid_requests[@]} ]]; then
        record_test_result "Error recovery" "PASS"
    else
        record_test_result "Error recovery" "FAIL: ${recovery_success}/${#invalid_requests[@]} recovered"
    fi
}

# 生成测试报告
generate_report() {
    echo
    echo "=========================================="
    echo "           TEST SUMMARY REPORT"
    echo "=========================================="
    echo "Total Tests: $TOTAL_TESTS"
    echo "Passed: $PASSED_TESTS"
    echo "Failed: $FAILED_TESTS"
    
    if [[ $FAILED_TESTS -eq 0 ]]; then
        echo -e "${GREEN}All tests passed! ✓${NC}"
        return 0
    else
        echo -e "${RED}Some tests failed! ✗${NC}"
        return 1
    fi
}

# 主函数
main() {
    echo "============================================"
    echo "   Nginx JSON Hash Module - Comprehensive Test"
    echo "============================================"
    echo "Test URL: $TEST_URL"
    echo "Date: $(date)"
    echo
    
    check_dependencies
    check_nginx_status
    
    echo
    log_info "Starting test suite..."
    echo
    
    # 运行所有测试
    test_basic_json_hash
    test_consistent_hash
    test_data_types
    test_fallback_mechanism
    test_content_type_validation
    test_json_depth_limit
    test_large_json_handling
    test_performance
    test_concurrent_requests
    test_error_recovery
    
    # 生成报告
    generate_report
}

# 解析命令行参数
while [[ $# -gt 0 ]]; do
    case $1 in
        --host)
            NGINX_HOST="$2"
            shift 2
            ;;
        --port)
            NGINX_PORT="$2"
            shift 2
            ;;
        --url)
            TEST_URL="$2"
            shift 2
            ;;
        --help)
            echo "Usage: $0 [OPTIONS]"
            echo "Options:"
            echo "  --host HOST    Nginx host (default: localhost)"
            echo "  --port PORT    Nginx port (default: 80)"
            echo "  --url URL      Test URL (default: http://HOST:PORT/api/test)"
            echo "  --help         Show this help message"
            exit 0
            ;;
        *)
            log_error "Unknown option: $1"
            exit 1
            ;;
    esac
done

# 运行主函数
main "$@" 
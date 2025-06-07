#!/bin/bash

# nginx_upstream_json_hash 模块性能基准测试
# 测试针对DeepSeek建议优化后的各种配置场景

set -e

# 颜色输出
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# 配置参数
TEST_THREADS=${TEST_THREADS:-4}
TEST_DURATION=${TEST_DURATION:-30}
TEST_RATE=${TEST_RATE:-1000}
BASE_URL=${BASE_URL:-"http://localhost"}

echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}  nginx upstream json hash 性能基准测试${NC}"
echo -e "${BLUE}  测试优化后的各种配置场景${NC}"
echo -e "${BLUE}========================================${NC}"

# 确保工具安装
check_tools() {
    local tools=("wrk" "curl" "jq" "bc")
    for tool in "${tools[@]}"; do
        if ! command -v $tool &> /dev/null; then
            echo -e "${RED}错误: 需要安装 $tool${NC}"
            exit 1
        fi
    done
}

# 生成测试JSON数据
generate_test_data() {
    local user_id=$1
    local depth=${2:-1}
    local size=${3:-"small"}
    
    case $size in
        "small")
            cat << EOF
{"user_id": "$user_id", "action": "test", "timestamp": $(date +%s)}
EOF
            ;;
        "medium")
            cat << EOF
{"user_id": "$user_id", "action": "test", "timestamp": $(date +%s), "data": {"level1": {"level2": {"value": "test_data_medium_size_for_performance_testing"}}}, "metadata": {"source": "benchmark", "version": "1.0"}}
EOF
            ;;
        "large")
            local data=""
            for i in {1..50}; do
                data="${data}\"field$i\": \"value$i_$(date +%s%N)\", "
            done
            cat << EOF
{"user_id": "$user_id", "action": "test", "timestamp": $(date +%s), "large_data": {${data%%, }}, "nested": {"level1": {"level2": {"level3": {"level4": {"value": "deep_nested_test"}}}}}}
EOF
            ;;
    esac
}

# 测试单个场景
test_scenario() {
    local scenario_name=$1
    local endpoint=$2
    local data_size=${3:-"small"}
    local max_depth=${4:-16}
    
    echo -e "${YELLOW}测试场景: $scenario_name${NC}"
    echo "端点: $endpoint"
    echo "数据大小: $data_size"
    echo "最大深度: $max_depth"
    
    # 创建临时脚本文件
    local script_file="/tmp/wrk_script_$$_$(date +%s).lua"
    
    cat > "$script_file" << 'EOF'
-- WRK Lua脚本 - 动态生成JSON测试数据
local user_ids = {}
local data_templates = {}

-- 预生成用户ID列表
for i = 1, 10000 do
    user_ids[i] = "user_" .. i
end

-- 预生成数据模板
data_templates.small = function(user_id)
    return '{"user_id": "' .. user_id .. '", "action": "test", "timestamp": ' .. os.time() .. '}'
end

data_templates.medium = function(user_id)
    return '{"user_id": "' .. user_id .. '", "action": "test", "timestamp": ' .. os.time() .. ', "data": {"level1": {"level2": {"value": "test_data_medium"}}}, "metadata": {"source": "benchmark", "version": "1.0"}}'
end

data_templates.large = function(user_id)
    local fields = {}
    for i = 1, 20 do
        table.insert(fields, '"field' .. i .. '": "value' .. i .. '_' .. os.time() .. '"')
    end
    return '{"user_id": "' .. user_id .. '", "action": "test", "timestamp": ' .. os.time() .. ', "large_data": {' .. table.concat(fields, ', ') .. '}, "nested": {"level1": {"level2": {"level3": {"value": "deep_test"}}}}}'
end

local counter = 0
local data_size = "PLACEHOLDER_DATA_SIZE"

request = function()
    counter = counter + 1
    local user_id = user_ids[(counter % 10000) + 1]
    local body = data_templates[data_size](user_id)
    
    return wrk.format("POST", wrk.path, {
        ["Content-Type"] = "application/json",
        ["Content-Length"] = #body
    }, body)
end

function response(status, headers, body)
    -- 可以添加响应验证逻辑
    if status ~= 200 then
        print("Error: HTTP " .. status)
    end
end
EOF
    
    # 替换数据大小占位符
    sed -i "s/PLACEHOLDER_DATA_SIZE/$data_size/g" "$script_file"
    
    # 执行性能测试
    echo "开始测试..."
    local result=$(wrk -t$TEST_THREADS -c100 -d${TEST_DURATION}s -R$TEST_RATE \
        --script="$script_file" \
        "$BASE_URL$endpoint" 2>&1)
    
    # 清理临时文件
    rm -f "$script_file"
    
    # 解析结果
    local rps=$(echo "$result" | grep "Requests/sec:" | awk '{print $2}')
    local latency_avg=$(echo "$result" | grep "Latency" | awk '{print $2}')
    local latency_max=$(echo "$result" | grep "Latency" | awk '{print $4}')
    local errors=$(echo "$result" | grep -o "Non-2xx.*" || echo "0 errors")
    
    echo -e "${GREEN}结果:${NC}"
    echo "  QPS: $rps"
    echo "  平均延迟: $latency_avg"
    echo "  最大延迟: $latency_max"
    echo "  错误: $errors"
    echo ""
    
    # 记录到结果文件
    echo "$scenario_name,$endpoint,$data_size,$rps,$latency_avg,$latency_max,$errors" >> performance_results.csv
}

# 测试哈希分布均匀性
test_hash_distribution() {
    echo -e "${YELLOW}测试哈希分布均匀性...${NC}"
    
    # 生成测试请求
    local test_requests=1000
    local distribution_file="/tmp/hash_distribution_$$"
    
    echo "生成 $test_requests 个测试请求..."
    for i in $(seq 1 $test_requests); do
        local json_data=$(generate_test_data "user_$i" 1 "small")
        echo "$json_data" | curl -s -X POST \
            -H "Content-Type: application/json" \
            -d @- \
            "$BASE_URL/api/v1/" \
            -w "%{remote_ip}\n" >> "$distribution_file" 2>/dev/null || true
    done
    
    # 分析分布
    if [[ -s "$distribution_file" ]]; then
        echo -e "${GREEN}哈希分布分析:${NC}"
        sort "$distribution_file" | uniq -c | sort -nr | head -10
        
        # 计算分布均匀性（变异系数）
        local stats=$(sort "$distribution_file" | uniq -c | awk '{print $1}' | \
            awk '{sum+=$1; sumsq+=$1*$1; n++} END {
                if(n>0) {
                    mean=sum/n; 
                    var=(sumsq-sum*sum/n)/(n-1); 
                    printf "平均: %.2f, 标准差: %.2f, 变异系数: %.2f%%\n", mean, sqrt(var), sqrt(var)/mean*100
                }
            }')
        echo "  $stats"
    fi
    
    rm -f "$distribution_file"
}

# 内存使用分析
test_memory_usage() {
    echo -e "${YELLOW}测试内存使用情况...${NC}"
    
    # 获取nginx进程内存使用
    local nginx_pid=$(pgrep nginx | head -1)
    if [[ -n "$nginx_pid" ]]; then
        local memory_before=$(ps -o rss= -p $nginx_pid)
        echo "测试前内存使用: ${memory_before}KB"
        
        # 执行一组请求
        echo "执行内存压力测试..."
        for i in {1..100}; do
            generate_test_data "user_$i" 3 "large" | \
            curl -s -X POST -H "Content-Type: application/json" \
            -d @- "$BASE_URL/api/v2/" > /dev/null 2>&1 || true
        done
        
        sleep 2
        local memory_after=$(ps -o rss= -p $nginx_pid)
        echo "测试后内存使用: ${memory_after}KB"
        echo "内存增长: $((memory_after - memory_before))KB"
    else
        echo "未找到nginx进程"
    fi
}

# 并发测试
test_concurrency() {
    echo -e "${YELLOW}测试并发性能...${NC}"
    
    local concurrency_levels=(50 100 200 500 1000)
    
    for concurrent in "${concurrency_levels[@]}"; do
        echo "测试并发级别: $concurrent"
        
        local result=$(wrk -t$TEST_THREADS -c$concurrent -d10s \
            --script=<(cat << 'EOF'
request = function()
    local user_id = "user_" .. math.random(1, 1000)
    local body = '{"user_id": "' .. user_id .. '", "action": "concurrent_test"}'
    return wrk.format("POST", wrk.path, {
        ["Content-Type"] = "application/json"
    }, body)
end
EOF
) "$BASE_URL/api/v1/" 2>&1)
        
        local rps=$(echo "$result" | grep "Requests/sec:" | awk '{print $2}')
        local errors=$(echo "$result" | grep -c "Socket errors" || echo "0")
        
        echo "  并发 $concurrent: QPS=$rps, 错误=$errors"
    done
}

# 主测试函数
main() {
    check_tools
    
    echo "开始性能基准测试..."
    echo "测试参数:"
    echo "  线程数: $TEST_THREADS"
    echo "  测试时长: ${TEST_DURATION}s"
    echo "  目标QPS: $TEST_RATE"
    echo ""
    
    # 创建结果文件头
    echo "场景,端点,数据大小,QPS,平均延迟,最大延迟,错误" > performance_results.csv
    
    # 测试各种场景
    test_scenario "高性能配置" "/api/v1/" "small" 16
    test_scenario "高性能配置-中等数据" "/api/v1/" "medium" 16
    test_scenario "高分布均匀性配置" "/api/v2/" "small" 32
    test_scenario "高分布均匀性配置-大数据" "/api/v2/" "large" 32
    test_scenario "高安全配置" "/api/secure/" "small" 8
    test_scenario "大规模集群配置" "/api/scale/" "medium" 20
    test_scenario "开发测试配置" "/api/dev/" "large" 100
    
    # 专项测试
    test_hash_distribution
    test_memory_usage
    test_concurrency
    
    echo -e "${GREEN}========================================${NC}"
    echo -e "${GREEN}  测试完成！${NC}"
    echo -e "${GREEN}========================================${NC}"
    echo "详细结果已保存到 performance_results.csv"
    
    # 生成总结报告
    if command -v python3 &> /dev/null; then
        python3 << 'EOF'
import csv
import sys

try:
    with open('performance_results.csv', 'r') as f:
        reader = csv.DictReader(f)
        results = list(reader)
    
    print("\n性能测试总结:")
    print("=" * 50)
    
    for result in results:
        if result['QPS'].replace('.', '').isdigit():
            qps = float(result['QPS'])
            print(f"{result['场景']:20} | QPS: {qps:8.1f} | 延迟: {result['平均延迟']:>8}")
    
    # 找出最佳性能配置
    best_qps = max((r for r in results if r['QPS'].replace('.', '').isdigit()), 
                   key=lambda x: float(x['QPS']), default=None)
    
    if best_qps:
        print(f"\n最佳性能配置: {best_qps['场景']}")
        print(f"QPS: {best_qps['QPS']}, 延迟: {best_qps['平均延迟']}")

except Exception as e:
    print(f"生成报告时出错: {e}")
EOF
    fi
}

# 脚本选项
case "${1:-}" in
    --help|-h)
        echo "使用方法: $0 [选项]"
        echo "选项:"
        echo "  --threads N    设置测试线程数 (默认: 4)"
        echo "  --duration N   设置测试时长秒数 (默认: 30)"
        echo "  --rate N       设置目标QPS (默认: 1000)"
        echo "  --url URL      设置基础URL (默认: http://localhost)"
        echo "  --quick        快速测试模式"
        echo "  --help         显示此帮助信息"
        exit 0
        ;;
    --threads)
        TEST_THREADS=$2
        shift 2
        ;;
    --duration)
        TEST_DURATION=$2
        shift 2
        ;;
    --rate)
        TEST_RATE=$2
        shift 2
        ;;
    --url)
        BASE_URL=$2
        shift 2
        ;;
    --quick)
        TEST_DURATION=10
        TEST_RATE=500
        echo "启用快速测试模式"
        ;;
esac

main "$@" 
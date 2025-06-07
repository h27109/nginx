#!/bin/bash

# JSON Hash 模块测试脚本
# 该脚本用于测试nginx JSON hash模块的功能

set -e

# 配置变量
NGINX_HOST=${NGINX_HOST:-"localhost"}
NGINX_PORT=${NGINX_PORT:-"80"}
TEST_ENDPOINT="/api/test"
BASE_URL="http://${NGINX_HOST}:${NGINX_PORT}${TEST_ENDPOINT}"

# 颜色输出
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

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

# 检查依赖
check_dependencies() {
    log_info "检查依赖..."
    
    if ! command -v curl &> /dev/null; then
        log_error "curl未安装，请先安装curl"
        exit 1
    fi
    
    if ! command -v jq &> /dev/null; then
        log_warn "jq未安装，部分功能可能无法使用"
    fi
}

# 检查nginx是否运行
check_nginx() {
    log_info "检查nginx状态..."
    
    if ! curl -s --connect-timeout 5 "${NGINX_HOST}:${NGINX_PORT}" > /dev/null; then
        log_error "无法连接到nginx (${NGINX_HOST}:${NGINX_PORT})"
        log_error "请确保nginx已启动并监听正确的端口"
        exit 1
    fi
    
    log_info "nginx连接正常"
}

# 创建测试后端服务器（模拟）
create_test_backend() {
    log_info "创建测试后端配置..."
    
    # 创建简单的测试配置
    cat > /tmp/test_nginx.conf << 'EOF'
worker_processes 1;
error_log /tmp/nginx_test_error.log debug;
pid /tmp/nginx_test.pid;

events {
    worker_connections 1024;
}

http {
    include /etc/nginx/mime.types;
    default_type application/octet-stream;
    
    access_log /tmp/nginx_test_access.log;
    
    # 测试用的upstream
    upstream test_backend {
        json_hash user_id;
        
        server 127.0.0.1:8081;
        server 127.0.0.1:8082;
        server 127.0.0.1:8083;
    }
    
    # 简单的测试后端服务器
    server {
        listen 8081;
        location / {
            return 200 '{"server": "backend-1", "port": 8081}\n';
            add_header Content-Type application/json;
        }
    }
    
    server {
        listen 8082;
        location / {
            return 200 '{"server": "backend-2", "port": 8082}\n';
            add_header Content-Type application/json;
        }
    }
    
    server {
        listen 8083;
        location / {
            return 200 '{"server": "backend-3", "port": 8083}\n';
            add_header Content-Type application/json;
        }
    }
    
    # 主要的代理服务器
    server {
        listen 9999;
        
        location /api/ {
            proxy_pass http://test_backend;
            proxy_set_header Host $host;
            proxy_set_header X-Real-IP $remote_addr;
            proxy_request_buffering on;
        }
    }
}
EOF
}

# 启动测试nginx实例
start_test_nginx() {
    log_info "启动测试nginx实例..."
    
    # 检查测试端口是否已被占用
    for port in 8081 8082 8083 9999; do
        if netstat -ln 2>/dev/null | grep -q ":${port} "; then
            log_warn "端口 ${port} 已被占用，请检查"
        fi
    done
    
    # 启动测试nginx
    nginx -c /tmp/test_nginx.conf -p /tmp/
    
    if [ $? -eq 0 ]; then
        log_info "测试nginx实例启动成功"
        sleep 2
    else
        log_error "测试nginx实例启动失败"
        exit 1
    fi
}

# 停止测试nginx实例
stop_test_nginx() {
    log_info "停止测试nginx实例..."
    
    if [ -f /tmp/nginx_test.pid ]; then
        nginx -s quit -c /tmp/test_nginx.conf -p /tmp/
        rm -f /tmp/nginx_test.pid
    fi
    
    # 清理临时文件
    rm -f /tmp/test_nginx.conf /tmp/nginx_test_*.log
}

# 发送测试请求
send_test_request() {
    local user_id=$1
    local expected_server=$2
    
    local json_data="{\"user_id\": \"${user_id}\", \"action\": \"test\", \"timestamp\": $(date +%s)}"
    
    local response=$(curl -s -X POST \
        -H "Content-Type: application/json" \
        -d "${json_data}" \
        "http://localhost:9999/api/test")
    
    if [ $? -eq 0 ]; then
        echo "$response"
    else
        echo "ERROR"
    fi
}

# 测试一致性hash
test_consistency() {
    log_info "测试hash一致性..."
    
    local test_users=("user001" "user002" "user003" "user004" "user005")
    declare -A user_servers
    
    # 第一轮请求，记录每个用户分配到的服务器
    for user in "${test_users[@]}"; do
        local response=$(send_test_request "$user")
        
        if [[ "$response" != "ERROR" ]]; then
            local server=$(echo "$response" | jq -r '.server' 2>/dev/null || echo "unknown")
            user_servers["$user"]="$server"
            log_info "用户 $user -> 服务器 $server"
        else
            log_error "用户 $user 请求失败"
        fi
        
        sleep 0.1
    done
    
    log_info "等待1秒后进行第二轮测试..."
    sleep 1
    
    # 第二轮请求，验证一致性
    local consistent_count=0
    local total_count=${#test_users[@]}
    
    for user in "${test_users[@]}"; do
        local response=$(send_test_request "$user")
        
        if [[ "$response" != "ERROR" ]]; then
            local server=$(echo "$response" | jq -r '.server' 2>/dev/null || echo "unknown")
            local expected_server="${user_servers[$user]}"
            
            if [[ "$server" == "$expected_server" ]]; then
                log_info "✓ 用户 $user 一致性验证通过: $server"
                ((consistent_count++))
            else
                log_error "✗ 用户 $user 一致性验证失败: 期望 $expected_server, 实际 $server"
            fi
        else
            log_error "用户 $user 第二轮请求失败"
        fi
        
        sleep 0.1
    done
    
    log_info "一致性测试结果: $consistent_count/$total_count"
    
    if [ $consistent_count -eq $total_count ]; then
        log_info "✓ 一致性测试通过"
        return 0
    else
        log_error "✗ 一致性测试失败"
        return 1
    fi
}

# 测试分布性
test_distribution() {
    log_info "测试负载分布..."
    
    declare -A server_counts
    local total_requests=30
    
    for i in $(seq 1 $total_requests); do
        local user_id="user$(printf "%03d" $i)"
        local response=$(send_test_request "$user_id")
        
        if [[ "$response" != "ERROR" ]]; then
            local server=$(echo "$response" | jq -r '.server' 2>/dev/null || echo "unknown")
            ((server_counts["$server"]++))
        fi
        
        sleep 0.05
    done
    
    log_info "负载分布结果:"
    for server in "${!server_counts[@]}"; do
        local count=${server_counts[$server]}
        local percentage=$(( count * 100 / total_requests ))
        log_info "  $server: $count 请求 (${percentage}%)"
    done
    
    # 检查是否所有服务器都收到了请求
    local active_servers=${#server_counts[@]}
    if [ $active_servers -ge 2 ]; then
        log_info "✓ 负载分布测试通过 ($active_servers 台服务器参与负载均衡)"
        return 0
    else
        log_error "✗ 负载分布测试失败 (只有 $active_servers 台服务器参与负载均衡)"
        return 1
    fi
}

# 测试错误处理
test_error_handling() {
    log_info "测试错误处理..."
    
    # 测试无效JSON
    log_info "测试无效JSON..."
    local response=$(curl -s -X POST \
        -H "Content-Type: application/json" \
        -d "invalid json" \
        "http://localhost:9999/api/test")
    
    if [[ "$response" != "" ]]; then
        log_info "✓ 无效JSON处理正常"
    else
        log_warn "无效JSON处理可能有问题"
    fi
    
    # 测试缺少字段
    log_info "测试缺少hash字段..."
    local response=$(curl -s -X POST \
        -H "Content-Type: application/json" \
        -d '{"action": "test", "timestamp": 1234567890}' \
        "http://localhost:9999/api/test")
    
    if [[ "$response" != "" ]]; then
        log_info "✓ 缺少字段处理正常"
    else
        log_warn "缺少字段处理可能有问题"
    fi
}

# 性能测试
performance_test() {
    log_info "进行简单性能测试..."
    
    local requests=100
    local start_time=$(date +%s.%N)
    
    for i in $(seq 1 $requests); do
        local user_id="perf_user_$i"
        send_test_request "$user_id" > /dev/null &
        
        # 限制并发数
        if (( i % 10 == 0 )); then
            wait
        fi
    done
    
    wait  # 等待所有后台任务完成
    
    local end_time=$(date +%s.%N)
    local duration=$(echo "$end_time - $start_time" | bc)
    local rps=$(echo "scale=2; $requests / $duration" | bc)
    
    log_info "性能测试结果: $requests 请求耗时 ${duration}s, RPS: $rps"
}

# 查看nginx日志
show_logs() {
    log_info "显示nginx测试日志..."
    
    if [ -f /tmp/nginx_test_error.log ]; then
        echo "=== Error Log ==="
        tail -20 /tmp/nginx_test_error.log
        echo ""
    fi
    
    if [ -f /tmp/nginx_test_access.log ]; then
        echo "=== Access Log ==="
        tail -10 /tmp/nginx_test_access.log
        echo ""
    fi
}

# 主函数
main() {
    log_info "开始JSON Hash模块测试..."
    
    # 设置清理函数
    trap 'stop_test_nginx' EXIT
    
    check_dependencies
    create_test_backend
    start_test_nginx
    
    # 运行测试
    local test_results=0
    
    if test_consistency; then
        ((test_results++))
    fi
    
    if test_distribution; then
        ((test_results++))
    fi
    
    test_error_handling
    
    if command -v bc &> /dev/null; then
        performance_test
    else
        log_warn "bc未安装，跳过性能测试"
    fi
    
    show_logs
    
    log_info "测试完成！"
    
    if [ $test_results -eq 2 ]; then
        log_info "✓ 所有核心测试通过"
        exit 0
    else
        log_error "✗ 部分测试失败"
        exit 1
    fi
}

# 显示帮助信息
show_help() {
    echo "JSON Hash模块测试脚本"
    echo ""
    echo "用法: $0 [选项]"
    echo ""
    echo "选项:"
    echo "  -h, --help          显示帮助信息"
    echo "  -c, --consistency   只运行一致性测试"
    echo "  -d, --distribution  只运行分布测试"
    echo "  -e, --error         只运行错误处理测试"
    echo "  -p, --performance   只运行性能测试"
    echo "  -l, --logs          只显示日志"
    echo ""
    echo "环境变量:"
    echo "  NGINX_HOST         nginx主机地址 (默认: localhost)"
    echo "  NGINX_PORT         nginx端口 (默认: 80)"
    echo ""
}

# 命令行参数处理
case "${1:-}" in
    -h|--help)
        show_help
        exit 0
        ;;
    -c|--consistency)
        check_dependencies
        create_test_backend
        start_test_nginx
        test_consistency
        exit $?
        ;;
    -d|--distribution)
        check_dependencies
        create_test_backend
        start_test_nginx
        test_distribution
        exit $?
        ;;
    -e|--error)
        check_dependencies
        create_test_backend
        start_test_nginx
        test_error_handling
        exit 0
        ;;
    -p|--performance)
        check_dependencies
        create_test_backend
        start_test_nginx
        performance_test
        exit 0
        ;;
    -l|--logs)
        show_logs
        exit 0
        ;;
    "")
        main
        ;;
    *)
        echo "未知选项: $1"
        show_help
        exit 1
        ;;
esac 
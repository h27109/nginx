#!/bin/bash

# nginx upstream json hash 模块统计监控脚本
# 实时监控模块性能和统计数据

set -e

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# 配置参数
NGINX_LOG_PATH=${NGINX_LOG_PATH:-"/var/log/nginx/error.log"}
MONITOR_INTERVAL=${MONITOR_INTERVAL:-5}
ALERT_THRESHOLD_FAILURE_RATE=${ALERT_THRESHOLD_FAILURE_RATE:-10.0}
ALERT_THRESHOLD_AVG_PARSE_TIME=${ALERT_THRESHOLD_AVG_PARSE_TIME:-50}

echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}  nginx upstream json hash 统计监控${NC}"
echo -e "${BLUE}========================================${NC}"

# 检查nginx日志文件是否存在
check_log_file() {
    if [[ ! -f "$NGINX_LOG_PATH" ]]; then
        echo -e "${RED}错误: nginx日志文件不存在: $NGINX_LOG_PATH${NC}"
        echo "请设置正确的日志路径: export NGINX_LOG_PATH=/path/to/nginx/error.log"
        exit 1
    fi
    
    if [[ ! -r "$NGINX_LOG_PATH" ]]; then
        echo -e "${RED}错误: 无法读取nginx日志文件: $NGINX_LOG_PATH${NC}"
        exit 1
    fi
}

# 解析统计信息
parse_stats() {
    local log_line="$1"
    local requests=$(echo "$log_line" | grep -oP 'requests=\K\d+' || echo "0")
    local failures=$(echo "$log_line" | grep -oP 'failures=\K\d+' || echo "0")
    local content_type_checks=$(echo "$log_line" | grep -oP 'content_type_checks=\K\d+' || echo "0")
    local avg_parse_time=$(echo "$log_line" | grep -oP 'avg_parse_time=\K\d+' || echo "0")
    local failure_rate=$(echo "$log_line" | grep -oP 'failure_rate=\K[\d.]+' || echo "0.0")
    
    echo "$requests|$failures|$content_type_checks|$avg_parse_time|$failure_rate"
}

# 显示实时统计
show_stats() {
    local stats="$1"
    local timestamp="$2"
    
    IFS='|' read -r requests failures content_type_checks avg_parse_time failure_rate <<< "$stats"
    
    echo -e "${BLUE}[$timestamp]${NC}"
    echo -e "  请求总数:     ${GREEN}$requests${NC}"
    echo -e "  失败次数:     ${failures}"
    echo -e "  Content-Type检查: ${content_type_checks}"
    echo -e "  平均解析时间: ${avg_parse_time}ms"
    
    # 失败率警告
    if (( $(echo "$failure_rate > $ALERT_THRESHOLD_FAILURE_RATE" | bc -l) )); then
        echo -e "  失败率:       ${RED}${failure_rate}%${NC} ⚠️  警告：失败率过高！"
    else
        echo -e "  失败率:       ${GREEN}${failure_rate}%${NC}"
    fi
    
    # 解析时间警告
    if (( avg_parse_time > ALERT_THRESHOLD_AVG_PARSE_TIME )); then
        echo -e "                ${RED}⚠️  警告：JSON解析时间过长！${NC}"
    fi
    
    echo ""
}

# 计算QPS和TPS
calculate_rates() {
    local current_requests="$1"
    local current_failures="$2"
    local prev_requests="$3"
    local prev_failures="$4"
    local time_diff="$5"
    
    if [[ "$prev_requests" != "0" && "$time_diff" -gt 0 ]]; then
        local qps=$(( (current_requests - prev_requests) / time_diff ))
        local fps=$(( (current_failures - prev_failures) / time_diff ))
        echo -e "  QPS:          ${GREEN}$qps${NC} req/s"
        echo -e "  失败率:       ${fps} failures/s"
    fi
}

# 内存使用监控
monitor_memory() {
    local nginx_pids=$(pgrep nginx | tr '\n' ' ')
    if [[ -n "$nginx_pids" ]]; then
        local total_memory=$(ps -o rss= -p $nginx_pids | awk '{sum+=$1} END {print sum/1024}')
        echo -e "  内存使用:     ${YELLOW}${total_memory}MB${NC}"
    fi
}

# 主监控循环
monitor_loop() {
    local prev_timestamp=""
    local prev_requests=0
    local prev_failures=0
    local stats_count=0
    
    echo "开始监控 nginx upstream json hash 模块..."
    echo "日志文件: $NGINX_LOG_PATH"
    echo "监控间隔: ${MONITOR_INTERVAL}秒"
    echo "失败率告警阈值: ${ALERT_THRESHOLD_FAILURE_RATE}%"
    echo "解析时间告警阈值: ${ALERT_THRESHOLD_AVG_PARSE_TIME}ms"
    echo ""
    
    while true; do
        # 获取最新的统计日志
        local latest_stats=$(tail -100 "$NGINX_LOG_PATH" | grep "json_hash stats:" | tail -1)
        
        if [[ -n "$latest_stats" ]]; then
            local timestamp=$(echo "$latest_stats" | awk '{print $1" "$2}')
            local stats=$(parse_stats "$latest_stats")
            
            # 检查是否有新的统计数据
            if [[ "$timestamp" != "$prev_timestamp" ]]; then
                show_stats "$stats" "$timestamp"
                monitor_memory
                
                # 计算速率
                IFS='|' read -r requests failures content_type_checks avg_parse_time failure_rate <<< "$stats"
                if [[ "$prev_timestamp" != "" ]]; then
                    local time_diff=$MONITOR_INTERVAL
                    calculate_rates "$requests" "$failures" "$prev_requests" "$prev_failures" "$time_diff"
                fi
                
                prev_timestamp="$timestamp"
                prev_requests="$requests"
                prev_failures="$failures"
                stats_count=$((stats_count + 1))
                
                echo -e "${BLUE}================================${NC}"
            fi
        else
            if [[ $stats_count -eq 0 ]]; then
                echo -e "${YELLOW}等待统计数据...${NC}"
                echo "提示：确保nginx配置正确且有请求流量"
            fi
        fi
        
        sleep $MONITOR_INTERVAL
    done
}

# 生成统计报告
generate_report() {
    echo -e "${BLUE}生成统计报告...${NC}"
    
    local report_file="json_hash_stats_report_$(date +%Y%m%d_%H%M%S).txt"
    
    echo "nginx upstream json hash 模块统计报告" > "$report_file"
    echo "生成时间: $(date)" >> "$report_file"
    echo "=======================================" >> "$report_file"
    echo "" >> "$report_file"
    
    # 获取最近的统计数据
    tail -1000 "$NGINX_LOG_PATH" | grep "json_hash stats:" | tail -20 >> "$report_file"
    
    echo "" >> "$report_file"
    echo "内存使用情况:" >> "$report_file"
    ps aux | grep nginx >> "$report_file"
    
    echo -e "${GREEN}报告已生成: $report_file${NC}"
}

# 性能分析
analyze_performance() {
    echo -e "${BLUE}性能分析...${NC}"
    
    local stats_lines=$(tail -1000 "$NGINX_LOG_PATH" | grep "json_hash stats:")
    
    if [[ -z "$stats_lines" ]]; then
        echo -e "${YELLOW}未找到统计数据${NC}"
        return
    fi
    
    echo "最近统计数据分析:"
    echo "$stats_lines" | while read -r line; do
        local stats=$(parse_stats "$line")
        IFS='|' read -r requests failures content_type_checks avg_parse_time failure_rate <<< "$stats"
        
        echo "请求: $requests, 失败率: ${failure_rate}%, 平均解析时间: ${avg_parse_time}ms"
    done | tail -10
    
    # 计算平均值
    local avg_failure_rate=$(echo "$stats_lines" | while read -r line; do
        parse_stats "$line" | cut -d'|' -f5
    done | awk '{sum+=$1; count++} END {if(count>0) print sum/count; else print 0}')
    
    local avg_parse_time_overall=$(echo "$stats_lines" | while read -r line; do
        parse_stats "$line" | cut -d'|' -f4
    done | awk '{sum+=$1; count++} END {if(count>0) print sum/count; else print 0}')
    
    echo ""
    echo -e "平均失败率: ${GREEN}${avg_failure_rate}%${NC}"
    echo -e "平均解析时间: ${GREEN}${avg_parse_time_overall}ms${NC}"
}

# 脚本选项处理
case "${1:-}" in
    --help|-h)
        echo "使用方法: $0 [选项]"
        echo "选项:"
        echo "  --monitor      实时监控模式 (默认)"
        echo "  --report       生成统计报告"
        echo "  --analyze      性能分析"
        echo "  --interval N   设置监控间隔秒数 (默认: 5)"
        echo "  --log PATH     设置nginx日志路径"
        echo "  --help         显示此帮助信息"
        echo ""
        echo "环境变量:"
        echo "  NGINX_LOG_PATH              nginx错误日志路径"
        echo "  MONITOR_INTERVAL            监控间隔秒数"
        echo "  ALERT_THRESHOLD_FAILURE_RATE 失败率告警阈值(%)"
        echo "  ALERT_THRESHOLD_AVG_PARSE_TIME 解析时间告警阈值(ms)"
        exit 0
        ;;
    --monitor)
        check_log_file
        monitor_loop
        ;;
    --report)
        check_log_file
        generate_report
        ;;
    --analyze)
        check_log_file
        analyze_performance
        ;;
    --interval)
        MONITOR_INTERVAL=$2
        check_log_file
        monitor_loop
        ;;
    --log)
        NGINX_LOG_PATH=$2
        check_log_file
        monitor_loop
        ;;
    *)
        check_log_file
        monitor_loop
        ;;
esac 
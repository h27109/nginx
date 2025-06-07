#!/bin/bash

# 多IDC网络监控和故障切换脚本
set -e

# 配置文件
CONFIG_FILE="/opt/nginx-gateway/conf/nginx.conf"
BACKUP_CONFIG="/opt/nginx-gateway/conf/nginx.conf.backup"
LOG_FILE="/var/log/idc_monitoring.log"

# IDC信息配置
declare -A IDC_INFO=(
    ["beijing"]="10.1.1.100:80"
    ["shanghai"]="10.2.1.100:80" 
    ["shenzhen"]="10.3.1.100:80"
)

# 告警配置
WEBHOOK_URL="${WEBHOOK_URL:-https://hooks.slack.com/your-webhook}"
ALERT_THRESHOLD=3  # 连续失败次数阈值

# 健康检查函数
check_idc_health() {
    local idc_name=$1
    local idc_endpoint=$2
    local timeout=5
    
    # HTTP健康检查
    if curl -f -s --max-time $timeout "http://$idc_endpoint/health" > /dev/null 2>&1; then
        return 0
    else
        return 1
    fi
}

# 网络延迟检测
check_network_latency() {
    local idc_name=$1
    local idc_endpoint=$2
    local host=$(echo $idc_endpoint | cut -d: -f1)
    
    # 使用ping检测延迟
    local latency=$(ping -c 3 -W 1 $host 2>/dev/null | tail -1 | awk -F '/' '{print $5}' 2>/dev/null || echo "999")
    echo $latency
}

# 发送告警
send_alert() {
    local message="$1"
    local timestamp=$(date '+%Y-%m-%d %H:%M:%S')
    
    echo "[$timestamp] ALERT: $message" >> $LOG_FILE
    
    # 发送到Slack/钉钉/企业微信等
    if [ -n "$WEBHOOK_URL" ]; then
        curl -X POST "$WEBHOOK_URL" \
            -H 'Content-Type: application/json' \
            -d "{\"text\": \"[$timestamp] IDC Gateway Alert: $message\"}" \
            > /dev/null 2>&1 || true
    fi
    
    # 发送邮件告警（如果配置了）
    if command -v mail > /dev/null; then
        echo "$message" | mail -s "IDC Gateway Alert" admin@yourcompany.com || true
    fi
}

# 更新nginx配置
update_nginx_config() {
    local failed_idc=$1
    local action=$2  # disable or enable
    
    # 备份当前配置
    cp $CONFIG_FILE $BACKUP_CONFIG
    
    if [ "$action" = "disable" ]; then
        # 将故障IDC的服务器标记为down
        sed -i "/server.*$failed_idc.*weight/s/;/ down;/" $CONFIG_FILE
        echo "[$timestamp] 禁用IDC: $failed_idc" >> $LOG_FILE
    elif [ "$action" = "enable" ]; then
        # 恢复IDC的服务器
        sed -i "/server.*$failed_idc.*down/s/ down;/;/" $CONFIG_FILE
        echo "[$timestamp] 恢复IDC: $failed_idc" >> $LOG_FILE
    fi
    
    # 测试配置
    if /opt/nginx-gateway/sbin/nginx -t -c $CONFIG_FILE > /dev/null 2>&1; then
        # 重载配置
        systemctl reload nginx-gateway
        echo "[$timestamp] nginx配置重载成功" >> $LOG_FILE
    else
        # 配置错误，恢复备份
        cp $BACKUP_CONFIG $CONFIG_FILE
        echo "[$timestamp] 配置错误，已恢复备份配置" >> $LOG_FILE
        send_alert "nginx配置更新失败，已恢复备份配置"
    fi
}

# 故障切换逻辑
handle_failover() {
    local failed_idc=$1
    
    send_alert "IDC $failed_idc 检测到故障，正在执行故障切换"
    
    # 更新nginx配置，禁用故障IDC
    update_nginx_config $failed_idc "disable"
    
    # 记录故障时间
    echo "$failed_idc:$(date +%s)" >> /tmp/idc_failures
}

# 故障恢复逻辑
handle_recovery() {
    local recovered_idc=$1
    
    send_alert "IDC $recovered_idc 已恢复，正在启用"
    
    # 更新nginx配置，启用恢复的IDC
    update_nginx_config $recovered_idc "enable"
    
    # 清除故障记录
    sed -i "/^$recovered_idc:/d" /tmp/idc_failures 2>/dev/null || true
}

# 主监控循环
monitor_idcs() {
    local timestamp=$(date '+%Y-%m-%d %H:%M:%S')
    echo "[$timestamp] 开始IDC健康检查" >> $LOG_FILE
    
    for idc_name in "${!IDC_INFO[@]}"; do
        local idc_endpoint="${IDC_INFO[$idc_name]}"
        local status_file="/tmp/idc_status_$idc_name"
        
        # 检查IDC健康状态
        if check_idc_health "$idc_name" "$idc_endpoint"; then
            # IDC健康
            local latency=$(check_network_latency "$idc_name" "$idc_endpoint")
            echo "[$timestamp] IDC $idc_name: OK (延迟: ${latency}ms)" >> $LOG_FILE
            
            # 如果之前是故障状态，则恢复
            if [ -f "${status_file}_failed" ]; then
                handle_recovery "$idc_name"
                rm -f "${status_file}_failed"
            fi
            
            # 更新状态
            echo "healthy" > "$status_file"
            
        else
            # IDC故障
            echo "[$timestamp] IDC $idc_name: FAILED" >> $LOG_FILE
            
            # 检查连续失败次数
            local fail_count=1
            if [ -f "${status_file}_failed" ]; then
                fail_count=$(cat "${status_file}_failed")
                fail_count=$((fail_count + 1))
            fi
            
            echo $fail_count > "${status_file}_failed"
            
            # 达到阈值时执行故障切换
            if [ $fail_count -ge $ALERT_THRESHOLD ]; then
                if [ ! -f "${status_file}_disabled" ]; then
                    handle_failover "$idc_name"
                    touch "${status_file}_disabled"
                fi
            fi
        fi
    done
}

# 生成监控报告
generate_report() {
    local report_file="/var/log/idc_report_$(date +%Y%m%d).log"
    local timestamp=$(date '+%Y-%m-%d %H:%M:%S')
    
    echo "=== IDC监控报告 - $timestamp ===" >> $report_file
    
    for idc_name in "${!IDC_INFO[@]}"; do
        local idc_endpoint="${IDC_INFO[$idc_name]}"
        local status_file="/tmp/idc_status_$idc_name"
        
        if [ -f "$status_file" ]; then
            local status=$(cat "$status_file")
            local latency=$(check_network_latency "$idc_name" "$idc_endpoint")
            echo "IDC $idc_name: $status (延迟: ${latency}ms)" >> $report_file
        else
            echo "IDC $idc_name: 未知状态" >> $report_file
        fi
    done
    
    echo "" >> $report_file
}

# 清理函数
cleanup() {
    echo "清理临时文件..."
    rm -f /tmp/idc_status_* 2>/dev/null || true
    rm -f /tmp/idc_failures 2>/dev/null || true
}

# 显示帮助信息
show_help() {
    cat << EOF
多IDC网络监控脚本

用法: $0 [选项]

选项:
    -m, --monitor       运行监控检查
    -r, --report        生成监控报告
    -c, --cleanup       清理临时文件
    -t, --test <idc>    测试指定IDC连接
    -h, --help          显示帮助信息

示例:
    $0 --monitor                # 运行一次监控检查
    $0 --test beijing          # 测试北京IDC连接
    $0 --report                 # 生成监控报告

定时任务配置:
    # 每分钟检查一次
    */1 * * * * /opt/scripts/network_monitoring.sh --monitor
    
    # 每小时生成报告
    0 * * * * /opt/scripts/network_monitoring.sh --report
EOF
}

# 测试单个IDC
test_idc() {
    local idc_name=$1
    
    if [ -z "$idc_name" ]; then
        echo "错误: 请指定要测试的IDC名称"
        echo "可用的IDC: ${!IDC_INFO[*]}"
        exit 1
    fi
    
    if [ -z "${IDC_INFO[$idc_name]}" ]; then
        echo "错误: 未知的IDC名称: $idc_name"
        echo "可用的IDC: ${!IDC_INFO[*]}"
        exit 1
    fi
    
    local idc_endpoint="${IDC_INFO[$idc_name]}"
    echo "测试IDC: $idc_name ($idc_endpoint)"
    
    if check_idc_health "$idc_name" "$idc_endpoint"; then
        local latency=$(check_network_latency "$idc_name" "$idc_endpoint")
        echo "✓ IDC $idc_name 健康 (延迟: ${latency}ms)"
    else
        echo "✗ IDC $idc_name 故障"
    fi
}

# 主函数
main() {
    case "$1" in
        -m|--monitor)
            monitor_idcs
            ;;
        -r|--report)
            generate_report
            ;;
        -c|--cleanup)
            cleanup
            ;;
        -t|--test)
            test_idc "$2"
            ;;
        -h|--help|"")
            show_help
            ;;
        *)
            echo "未知选项: $1"
            show_help
            exit 1
            ;;
    esac
}

# 创建日志目录
mkdir -p /var/log
touch $LOG_FILE

# 执行主函数
main "$@" 
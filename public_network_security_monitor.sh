#!/bin/bash

# 公网环境安全监控脚本
# 专门针对无VPN/专线的公网连接场景

set -e

# 配置变量
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LOG_DIR="/var/log/security_monitor"
CONFIG_FILE="/etc/security_monitor.conf"
ALERT_LOG="$LOG_DIR/security_alerts.log"
NGINX_LOG="/var/log/nginx/gateway_access.log"
FAIL2BAN_LOG="/var/log/fail2ban.log"
IDC_CONNECTIVITY_LOG="/var/log/idc_connectivity.log"

# 告警配置
SLACK_WEBHOOK_URL=""  # 需要配置Slack Webhook URL
EMAIL_RECIPIENTS="admin@yourcompany.com"
ALERT_COOLDOWN=300  # 5分钟告警冷却期

# 威胁检测阈值
MAX_4XX_PER_MINUTE=100
MAX_5XX_PER_MINUTE=50
MAX_FAILED_LOGIN_PER_MINUTE=20
MAX_CONNECTIONS_PER_IP=50
SUSPICIOUS_PATHS=("/admin" "/wp-admin" "/.env" "/config" "/backup")

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

# 初始化
init_monitor() {
    log "初始化安全监控系统..."
    
    # 创建日志目录
    mkdir -p $LOG_DIR
    chmod 750 $LOG_DIR
    
    # 创建配置文件（如果不存在）
    if [ ! -f "$CONFIG_FILE" ]; then
        cat > "$CONFIG_FILE" << 'EOF'
# 安全监控配置文件
MONITORING_ENABLED=true
ALERT_EMAIL=true
ALERT_SLACK=false
LOG_RETENTION_DAYS=30
THREAT_DETECTION_ENABLED=true
AUTO_BLOCK_SUSPICIOUS_IPS=true
GEO_BLOCKING_ENABLED=true
BLOCKED_COUNTRIES="CN,RU,KP"  # 示例：阻止某些国家的IP
EOF
    fi
    
    source "$CONFIG_FILE"
    
    # 创建威胁情报数据库
    if [ ! -f "$LOG_DIR/threat_intelligence.db" ]; then
        touch "$LOG_DIR/threat_intelligence.db"
        log "创建威胁情报数据库"
    fi
    
    log "安全监控系统初始化完成"
}

# 日志函数
log() {
    echo -e "${GREEN}[INFO]${NC} $(date '+%Y-%m-%d %H:%M:%S') - $1"
    echo "$(date '+%Y-%m-%d %H:%M:%S') [INFO] $1" >> "$LOG_DIR/monitor.log"
}

warn() {
    echo -e "${YELLOW}[WARN]${NC} $(date '+%Y-%m-%d %H:%M:%S') - $1"
    echo "$(date '+%Y-%m-%d %H:%M:%S') [WARN] $1" >> "$LOG_DIR/monitor.log"
}

error() {
    echo -e "${RED}[ERROR]${NC} $(date '+%Y-%m-%d %H:%M:%S') - $1"
    echo "$(date '+%Y-%m-%d %H:%M:%S') [ERROR] $1" >> "$LOG_DIR/monitor.log"
}

# 发送告警
send_alert() {
    local alert_type="$1"
    local alert_message="$2"
    local severity="$3"
    
    # 记录告警
    local timestamp=$(date '+%Y-%m-%d %H:%M:%S')
    echo "$timestamp [$severity] $alert_type: $alert_message" >> "$ALERT_LOG"
    
    # 检查告警冷却期
    local last_alert_file="$LOG_DIR/.last_alert_${alert_type}"
    if [ -f "$last_alert_file" ]; then
        local last_alert_time=$(cat "$last_alert_file")
        local current_time=$(date +%s)
        if [ $((current_time - last_alert_time)) -lt $ALERT_COOLDOWN ]; then
            return 0  # 在冷却期内，跳过告警
        fi
    fi
    
    # 更新最后告警时间
    echo $(date +%s) > "$last_alert_file"
    
    # 发送邮件告警
    if [ "$ALERT_EMAIL" = "true" ]; then
        send_email_alert "$alert_type" "$alert_message" "$severity"
    fi
    
    # 发送Slack告警
    if [ "$ALERT_SLACK" = "true" ] && [ -n "$SLACK_WEBHOOK_URL" ]; then
        send_slack_alert "$alert_type" "$alert_message" "$severity"
    fi
    
    warn "告警已发送: $alert_type - $alert_message"
}

# 邮件告警
send_email_alert() {
    local alert_type="$1"
    local alert_message="$2"
    local severity="$3"
    
    local subject="[$severity] 网关安全告警: $alert_type"
    local body="时间: $(date)
服务器: $(hostname)
告警类型: $alert_type
严重程度: $severity
详细信息: $alert_message

请立即检查网关状态和安全日志。"

    echo "$body" | mail -s "$subject" "$EMAIL_RECIPIENTS" 2>/dev/null || true
}

# Slack告警
send_slack_alert() {
    local alert_type="$1"
    local alert_message="$2"
    local severity="$3"
    
    local color="danger"
    case $severity in
        "INFO") color="good" ;;
        "WARNING") color="warning" ;;
        "CRITICAL") color="danger" ;;
    esac
    
    local payload="{
        \"username\": \"Gateway Security Monitor\",
        \"channel\": \"#security-alerts\",
        \"attachments\": [{
            \"color\": \"$color\",
            \"title\": \"[$severity] $alert_type\",
            \"text\": \"$alert_message\",
            \"fields\": [
                {\"title\": \"服务器\", \"value\": \"$(hostname)\", \"short\": true},
                {\"title\": \"时间\", \"value\": \"$(date)\", \"short\": true}
            ]
        }]
    }"
    
    curl -X POST -H 'Content-type: application/json' \
         --data "$payload" \
         "$SLACK_WEBHOOK_URL" &>/dev/null || true
}

# 实时威胁检测
detect_realtime_threats() {
    log "开始实时威胁检测..."
    
    # 检测最近1分钟的异常访问
    local current_time=$(date +%s)
    local one_minute_ago=$(date -d '1 minute ago' '+%d/%b/%Y:%H:%M')
    
    if [ ! -f "$NGINX_LOG" ]; then
        warn "Nginx访问日志不存在: $NGINX_LOG"
        return 1
    fi
    
    # 检测4xx错误过多
    local count_4xx=$(tail -n 10000 "$NGINX_LOG" | grep "$one_minute_ago" | grep -c ' 4[0-9][0-9] ' || true)
    if [ $count_4xx -gt $MAX_4XX_PER_MINUTE ]; then
        send_alert "HIGH_4XX_ERRORS" "最近1分钟内检测到 $count_4xx 个4xx错误" "WARNING"
    fi
    
    # 检测5xx错误过多
    local count_5xx=$(tail -n 10000 "$NGINX_LOG" | grep "$one_minute_ago" | grep -c ' 5[0-9][0-9] ' || true)
    if [ $count_5xx -gt $MAX_5XX_PER_MINUTE ]; then
        send_alert "HIGH_5XX_ERRORS" "最近1分钟内检测到 $count_5xx 个5xx错误" "CRITICAL"
    fi
    
    # 检测可疑路径访问
    for path in "${SUSPICIOUS_PATHS[@]}"; do
        local count_suspicious=$(tail -n 10000 "$NGINX_LOG" | grep "$one_minute_ago" | grep -c "$path" || true)
        if [ $count_suspicious -gt 0 ]; then
            send_alert "SUSPICIOUS_PATH_ACCESS" "检测到 $count_suspicious 次对可疑路径 $path 的访问" "WARNING"
        fi
    done
    
    # 检测单IP异常连接数
    local high_conn_ips=$(tail -n 10000 "$NGINX_LOG" | grep "$one_minute_ago" | \
                         awk '{print $1}' | sort | uniq -c | \
                         awk -v max=$MAX_CONNECTIONS_PER_IP '$1 > max {print $2 ":" $1}')
    
    if [ -n "$high_conn_ips" ]; then
        send_alert "HIGH_CONNECTION_COUNT" "检测到高频访问IP: $high_conn_ips" "WARNING"
        
        # 自动阻止可疑IP
        if [ "$AUTO_BLOCK_SUSPICIOUS_IPS" = "true" ]; then
            echo "$high_conn_ips" | while IFS=':' read ip count; do
                block_suspicious_ip "$ip" "高频访问(${count}次/分钟)"
            done
        fi
    fi
    
    log "实时威胁检测完成"
}

# 阻止可疑IP
block_suspicious_ip() {
    local ip="$1"
    local reason="$2"
    
    # 检查IP是否已被阻止
    if iptables -L INPUT | grep -q "$ip"; then
        return 0
    fi
    
    # 验证IP格式
    if [[ ! $ip =~ ^[0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3}$ ]]; then
        warn "无效IP格式: $ip"
        return 1
    fi
    
    # 检查是否为内网IP（不阻止内网IP）
    if [[ $ip =~ ^10\. ]] || [[ $ip =~ ^172\.1[6-9]\. ]] || [[ $ip =~ ^172\.2[0-9]\. ]] || [[ $ip =~ ^172\.3[0-1]\. ]] || [[ $ip =~ ^192\.168\. ]]; then
        return 0
    fi
    
    # 阻止IP
    iptables -I INPUT -s "$ip" -j DROP
    
    # 记录阻止操作
    echo "$(date '+%Y-%m-%d %H:%M:%S') BLOCKED $ip REASON: $reason" >> "$LOG_DIR/blocked_ips.log"
    
    # 添加到nginx黑名单
    echo "$ip 1;" >> /etc/nginx/blocked_ips.conf
    
    # 重载nginx配置
    nginx -s reload 2>/dev/null || true
    
    send_alert "IP_BLOCKED" "已自动阻止可疑IP: $ip (原因: $reason)" "INFO"
    
    log "已阻止可疑IP: $ip (原因: $reason)"
}

# 检查SSL证书安全
check_ssl_security() {
    log "检查SSL证书安全性..."
    
    local cert_file="/etc/nginx/ssl/gateway.crt"
    local key_file="/etc/nginx/ssl/gateway.key"
    
    if [ ! -f "$cert_file" ]; then
        send_alert "SSL_CERT_MISSING" "SSL证书文件不存在: $cert_file" "CRITICAL"
        return 1
    fi
    
    # 检查证书有效期
    local expire_date=$(openssl x509 -in "$cert_file" -noout -enddate | cut -d= -f2)
    local expire_timestamp=$(date -d "$expire_date" +%s)
    local current_timestamp=$(date +%s)
    local days_left=$(( (expire_timestamp - current_timestamp) / 86400 ))
    
    if [ $days_left -lt 30 ]; then
        send_alert "SSL_CERT_EXPIRING" "SSL证书将在 $days_left 天后过期" "WARNING"
    elif [ $days_left -lt 7 ]; then
        send_alert "SSL_CERT_EXPIRING_SOON" "SSL证书将在 $days_left 天后过期" "CRITICAL"
    fi
    
    # 检查证书强度
    local key_size=$(openssl x509 -in "$cert_file" -noout -text | grep "Public-Key:" | grep -o '[0-9]*')
    if [ $key_size -lt 2048 ]; then
        send_alert "WEAK_SSL_KEY" "SSL证书密钥强度不足: ${key_size}位" "WARNING"
    fi
    
    # 检查证书是否被撤销（如果有OCSP）
    local ocsp_uri=$(openssl x509 -in "$cert_file" -noout -ocsp_uri 2>/dev/null)
    if [ -n "$ocsp_uri" ]; then
        local ocsp_status=$(openssl ocsp -issuer "$cert_file" -cert "$cert_file" -text -url "$ocsp_uri" 2>/dev/null | grep "Response verify OK" || true)
        if [ -z "$ocsp_status" ]; then
            send_alert "SSL_OCSP_CHECK_FAILED" "SSL证书OCSP检查失败" "WARNING"
        fi
    fi
    
    log "SSL证书安全检查完成"
}

# IDC连接监控
monitor_idc_connectivity() {
    log "监控IDC连接状态..."
    
    local idc_ips=("123.125.114.144" "140.207.54.76" "183.232.231.172")  # 北京、上海、深圳IDC IP
    local idc_names=("Beijing" "Shanghai" "Shenzhen")
    local failed_idcs=()
    
    for i in "${!idc_ips[@]}"; do
        local ip="${idc_ips[i]}"
        local name="${idc_names[i]}"
        
        # 测试HTTPS连接
        if ! timeout 10 openssl s_client -connect "$ip:443" -verify_return_error < /dev/null &>/dev/null; then
            failed_idcs+=("$name($ip)")
            warn "IDC连接失败: $name($ip)"
            
            # 测试普通TCP连接
            if ! timeout 5 nc -z "$ip" 443 &>/dev/null; then
                send_alert "IDC_CONNECTION_FAILURE" "IDC连接完全失败: $name($ip)" "CRITICAL"
            else
                send_alert "IDC_SSL_FAILURE" "IDC SSL连接失败: $name($ip)" "CRITICAL"
            fi
        else
            log "IDC连接正常: $name($ip)"
        fi
    done
    
    # 如果所有IDC都失败，这是严重问题
    if [ ${#failed_idcs[@]} -eq ${#idc_hosts[@]} ]; then
        send_alert "ALL_IDC_DOWN" "所有IDC连接失败，请立即检查" "CRITICAL"
    elif [ ${#failed_idcs[@]} -gt 0 ]; then
        send_alert "PARTIAL_IDC_DOWN" "${#failed_idcs[@]} 个IDC连接失败: ${failed_idcs[*]}" "WARNING"
    fi
    
    log "IDC连接监控完成"
}

# 系统资源监控
monitor_system_resources() {
    log "监控系统资源使用情况..."
    
    # 检查CPU使用率
    local cpu_usage=$(top -bn1 | grep "Cpu(s)" | awk '{print $2}' | sed 's/%us,//')
    local cpu_threshold=80
    if (( $(echo "$cpu_usage > $cpu_threshold" | bc -l) )); then
        send_alert "HIGH_CPU_USAGE" "CPU使用率过高: ${cpu_usage}%" "WARNING"
    fi
    
    # 检查内存使用率
    local memory_usage=$(free | grep Mem | awk '{printf("%.0f", $3/$2 * 100.0)}')
    if [ $memory_usage -gt 80 ]; then
        send_alert "HIGH_MEMORY_USAGE" "内存使用率过高: ${memory_usage}%" "WARNING"
    fi
    
    # 检查磁盘使用率
    local disk_usage=$(df / | tail -1 | awk '{print $5}' | sed 's/%//')
    if [ $disk_usage -gt 80 ]; then
        send_alert "HIGH_DISK_USAGE" "磁盘使用率过高: ${disk_usage}%" "WARNING"
    fi
    
    # 检查网络连接数
    local connection_count=$(netstat -ant | grep ESTABLISHED | wc -l)
    if [ $connection_count -gt 10000 ]; then
        send_alert "HIGH_CONNECTION_COUNT" "网络连接数过高: $connection_count" "WARNING"
    fi
    
    # 检查Nginx进程状态
    if ! pgrep nginx > /dev/null; then
        send_alert "NGINX_DOWN" "Nginx进程未运行" "CRITICAL"
    fi
    
    # 检查端口监听状态
    local required_ports=(80 443 8080 8081)
    for port in "${required_ports[@]}"; do
        if ! netstat -tuln | grep -q ":$port "; then
            send_alert "PORT_NOT_LISTENING" "端口 $port 未监听" "CRITICAL"
        fi
    done
    
    log "系统资源监控完成"
}

# 分析攻击模式
analyze_attack_patterns() {
    log "分析攻击模式..."
    
    # 分析最近1小时的访问日志
    local one_hour_ago=$(date -d '1 hour ago' '+%d/%b/%Y:%H')
    
    # 检测SQL注入尝试
    local sql_injection_count=$(tail -n 50000 "$NGINX_LOG" | grep "$one_hour_ago" | \
                               grep -iE "(union|select|insert|update|delete|drop|exec|script)" | wc -l)
    if [ $sql_injection_count -gt 10 ]; then
        send_alert "SQL_INJECTION_ATTEMPTS" "检测到 $sql_injection_count 次SQL注入尝试" "WARNING"
    fi
    
    # 检测XSS尝试
    local xss_count=$(tail -n 50000 "$NGINX_LOG" | grep "$one_hour_ago" | \
                     grep -iE "(<script|javascript:|onload=|onerror=)" | wc -l)
    if [ $xss_count -gt 5 ]; then
        send_alert "XSS_ATTEMPTS" "检测到 $xss_count 次XSS攻击尝试" "WARNING"
    fi
    
    # 检测目录遍历尝试
    local dir_traversal_count=$(tail -n 50000 "$NGINX_LOG" | grep "$one_hour_ago" | \
                               grep -E "\.\./|\.\.\\" | wc -l)
    if [ $dir_traversal_count -gt 5 ]; then
        send_alert "DIRECTORY_TRAVERSAL_ATTEMPTS" "检测到 $dir_traversal_count 次目录遍历尝试" "WARNING"
    fi
    
    # 检测暴力破解尝试
    local brute_force_count=$(tail -n 50000 "$NGINX_LOG" | grep "$one_hour_ago" | \
                             grep -iE "(login|admin|wp-admin)" | grep " 401 " | wc -l)
    if [ $brute_force_count -gt 20 ]; then
        send_alert "BRUTE_FORCE_ATTEMPTS" "检测到 $brute_force_count 次暴力破解尝试" "WARNING"
    fi
    
    log "攻击模式分析完成"
}

# 地理位置阻止
geo_blocking_check() {
    if [ "$GEO_BLOCKING_ENABLED" != "true" ]; then
        return 0
    fi
    
    log "检查地理位置阻止..."
    
    # 获取最近访问的IP列表
    local recent_ips=$(tail -n 1000 "$NGINX_LOG" | awk '{print $1}' | sort | uniq)
    
    # 检查每个IP的地理位置（需要安装geoiplookup工具）
    if command -v geoiplookup &> /dev/null; then
        echo "$recent_ips" | while read ip; do
            if [[ $ip =~ ^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
                local country=$(geoiplookup "$ip" | awk -F': ' '{print $2}' | awk -F',' '{print $1}')
                
                # 检查是否为被阻止的国家
                if [[ "$BLOCKED_COUNTRIES" == *"$country"* ]]; then
                    block_suspicious_ip "$ip" "来自被阻止国家: $country"
                fi
            fi
        done
    else
        warn "geoiplookup工具未安装，跳过地理位置检查"
    fi
    
    log "地理位置阻止检查完成"
}

# 生成安全报告
generate_security_report() {
    log "生成安全报告..."
    
    local report_file="$LOG_DIR/security_report_$(date +%Y%m%d_%H%M%S).txt"
    local current_time=$(date)
    
    cat > "$report_file" << EOF
================================================================================
网关安全监控报告
生成时间: $current_time
服务器: $(hostname)
================================================================================

=== 系统状态 ===
CPU使用率: $(top -bn1 | grep "Cpu(s)" | awk '{print $2}')
内存使用率: $(free | grep Mem | awk '{printf("%.1f%%", $3/$2 * 100.0)}')
磁盘使用率: $(df / | tail -1 | awk '{print $5}')
网络连接数: $(netstat -ant | grep ESTABLISHED | wc -l)
Nginx进程状态: $(pgrep nginx > /dev/null && echo "运行中" || echo "已停止")

=== SSL证书状态 ===
证书有效期: $(openssl x509 -in /etc/nginx/ssl/gateway.crt -noout -enddate 2>/dev/null | cut -d= -f2 || echo "证书不存在")
证书密钥强度: $(openssl x509 -in /etc/nginx/ssl/gateway.crt -noout -text 2>/dev/null | grep "Public-Key:" | grep -o '[0-9]*' || echo "未知")位

=== 最近24小时访问统计 ===
总请求数: $(tail -n 100000 "$NGINX_LOG" | grep "$(date -d '1 day ago' '+%d/%b/%Y')" | wc -l)
4xx错误数: $(tail -n 100000 "$NGINX_LOG" | grep "$(date -d '1 day ago' '+%d/%b/%Y')" | grep ' 4[0-9][0-9] ' | wc -l)
5xx错误数: $(tail -n 100000 "$NGINX_LOG" | grep "$(date -d '1 day ago' '+%d/%b/%Y')" | grep ' 5[0-9][0-9] ' | wc -l)

=== 最近被阻止的IP ===
$(tail -n 20 "$LOG_DIR/blocked_ips.log" 2>/dev/null || echo "无记录")

=== 最近安全告警 ===
$(tail -n 20 "$ALERT_LOG" 2>/dev/null || echo "无告警")

=== IDC连接状态 ===
EOF
    
    # 添加IDC连接状态
    local idc_ips=("123.125.114.144" "140.207.54.76" "183.232.231.172")
    local idc_names=("Beijing" "Shanghai" "Shenzhen")
    for i in "${!idc_ips[@]}"; do
        local ip="${idc_ips[i]}"
        local name="${idc_names[i]}"
        if timeout 5 nc -z "$ip" 443 2>/dev/null; then
            echo "$name IDC ($ip): 连接正常" >> "$report_file"
        else
            echo "$name IDC ($ip): 连接失败" >> "$report_file"
        fi
    done
    
    echo "" >> "$report_file"
    echo "报告生成完成: $report_file" >> "$report_file"
    
    log "安全报告已生成: $report_file"
    
    # 发送报告（如果配置了邮件）
    if [ "$ALERT_EMAIL" = "true" ]; then
        mail -s "网关安全监控报告 - $(date +%Y%m%d)" "$EMAIL_RECIPIENTS" < "$report_file" 2>/dev/null || true
    fi
}

# 清理过期日志
cleanup_old_logs() {
    log "清理过期日志..."
    
    # 删除超过保留期的日志文件
    find "$LOG_DIR" -name "*.log" -type f -mtime +${LOG_RETENTION_DAYS:-30} -delete 2>/dev/null || true
    find "$LOG_DIR" -name "security_report_*.txt" -type f -mtime +7 -delete 2>/dev/null || true
    
    # 清理被阻止IP日志（保留最近1000条）
    if [ -f "$LOG_DIR/blocked_ips.log" ]; then
        tail -n 1000 "$LOG_DIR/blocked_ips.log" > "$LOG_DIR/blocked_ips.log.tmp"
        mv "$LOG_DIR/blocked_ips.log.tmp" "$LOG_DIR/blocked_ips.log"
    fi
    
    log "日志清理完成"
}

# 主监控循环
main_monitor_loop() {
    log "启动主监控循环..."
    
    while true; do
        case "${1:-continuous}" in
            "threats")
                detect_realtime_threats
                ;;
            "ssl")
                check_ssl_security
                ;;
            "idc")
                monitor_idc_connectivity
                ;;
            "resources")
                monitor_system_resources
                ;;
            "attacks")
                analyze_attack_patterns
                ;;
            "geo")
                geo_blocking_check
                ;;
            "report")
                generate_security_report
                exit 0
                ;;
            "cleanup")
                cleanup_old_logs
                exit 0
                ;;
            "continuous")
                # 完整监控循环
                detect_realtime_threats
                check_ssl_security
                monitor_idc_connectivity
                monitor_system_resources
                analyze_attack_patterns
                geo_blocking_check
                
                # 每小时生成一次报告
                local current_minute=$(date +%M)
                if [ "$current_minute" = "00" ]; then
                    generate_security_report
                fi
                
                # 每天清理一次日志
                local current_hour=$(date +%H)
                if [ "$current_hour" = "02" ] && [ "$current_minute" = "00" ]; then
                    cleanup_old_logs
                fi
                
                sleep 60  # 每分钟执行一次
                ;;
            *)
                echo "用法: $0 [threats|ssl|idc|resources|attacks|geo|report|cleanup|continuous]"
                exit 1
                ;;
        esac
        
        if [ "${1:-continuous}" != "continuous" ]; then
            break
        fi
    done
}

# 主函数
main() {
    init_monitor
    
    if [ "$MONITORING_ENABLED" = "true" ]; then
        main_monitor_loop "$@"
    else
        warn "安全监控已禁用，请在配置文件中启用"
        exit 1
    fi
}

# 信号处理
trap 'log "安全监控脚本已停止"; exit 0' SIGTERM SIGINT

# 执行主函数
main "$@" 
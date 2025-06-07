#!/bin/bash

# 多IDC网关部署脚本
set -e

NGINX_VERSION="1.29.0"
DEPLOY_ENV=${1:-production}
GATEWAY_HOME="/opt/nginx-gateway"

echo "=== 多IDC网关部署脚本 ==="
echo "部署环境: $DEPLOY_ENV"

# 1. 系统依赖安装
install_dependencies() {
    echo "安装系统依赖..."
    
    # CentOS/RHEL
    if [ -f /etc/redhat-release ]; then
        yum update -y
        yum install -y epel-release
        yum install -y gcc gcc-c++ make pcre-devel zlib-devel openssl-devel cjson-devel
        yum install -y lua lua-devel luajit luajit-devel
    fi
    
    # Ubuntu/Debian
    if [ -f /etc/debian_version ]; then
        apt-get update
        apt-get install -y build-essential libpcre3-dev zlib1g-dev libssl-dev libcjson-dev
        apt-get install -y lua5.3 lua5.3-dev luajit luajit-5.1-dev
    fi
}

# 2. 编译nginx
compile_nginx() {
    echo "编译nginx with json_hash模块..."
    
    cd /tmp
    
    # 下载nginx源码（如果没有）
    if [ ! -d "nginx-$NGINX_VERSION" ]; then
        wget http://nginx.org/download/nginx-$NGINX_VERSION.tar.gz
        tar -xzf nginx-$NGINX_VERSION.tar.gz
    fi
    
    cd nginx-$NGINX_VERSION
    
    # 复制json_hash模块
    cp -r /path/to/your/json_hash_module src/http/modules/
    
    # 配置编译选项
    ./configure \
        --prefix=$GATEWAY_HOME \
        --sbin-path=$GATEWAY_HOME/sbin/nginx \
        --conf-path=$GATEWAY_HOME/conf/nginx.conf \
        --error-log-path=/var/log/nginx/error.log \
        --http-log-path=/var/log/nginx/access.log \
        --pid-path=/var/run/nginx.pid \
        --lock-path=/var/run/nginx.lock \
        --with-http_ssl_module \
        --with-http_v2_module \
        --with-http_realip_module \
        --with-http_stub_status_module \
        --with-http_gzip_static_module \
        --with-http_secure_link_module \
        --with-http_sub_module \
        --with-stream \
        --with-stream_ssl_module \
        --with-stream_ssl_preread_module \
        --with-http_upstream_json_hash_module \
        --with-threads \
        --with-file-aio
    
    make -j$(nproc)
    make install
    
    echo "nginx编译完成"
}

# 3. 配置部署
deploy_config() {
    echo "部署配置文件..."
    
    # 创建目录结构
    mkdir -p $GATEWAY_HOME/{conf,ssl,logs}
    mkdir -p /var/log/nginx
    
    # 复制配置文件
    cp multi_idc_gateway.conf $GATEWAY_HOME/conf/nginx.conf
    
    # 生成SSL证书（自签名，生产环境应使用正式证书）
    if [ ! -f "$GATEWAY_HOME/ssl/gateway.crt" ]; then
        echo "生成自签名SSL证书..."
        openssl req -x509 -nodes -days 365 -newkey rsa:2048 \
            -keyout $GATEWAY_HOME/ssl/gateway.key \
            -out $GATEWAY_HOME/ssl/gateway.crt \
            -subj "/C=CN/ST=Beijing/L=Beijing/O=YourCompany/CN=gateway.yourcompany.com"
    fi
    
    # 设置权限
    chmod 600 $GATEWAY_HOME/ssl/gateway.key
    chmod 644 $GATEWAY_HOME/ssl/gateway.crt
}

# 4. 系统服务配置
setup_systemd() {
    echo "配置systemd服务..."
    
    cat > /etc/systemd/system/nginx-gateway.service << EOF
[Unit]
Description=The nginx HTTP and reverse proxy server (Multi-IDC Gateway)
After=network.target remote-fs.target nss-lookup.target

[Service]
Type=forking
PIDFile=/var/run/nginx.pid
ExecStartPre=$GATEWAY_HOME/sbin/nginx -t -c $GATEWAY_HOME/conf/nginx.conf
ExecStart=$GATEWAY_HOME/sbin/nginx -c $GATEWAY_HOME/conf/nginx.conf
ExecReload=/bin/kill -s HUP \$MAINPID
KillSignal=SIGQUIT
TimeoutStopSec=5
KillMode=process
PrivateTmp=true

[Install]
WantedBy=multi-user.target
EOF

    systemctl daemon-reload
    systemctl enable nginx-gateway
}

# 5. 日志轮转配置
setup_logrotate() {
    echo "配置日志轮转..."
    
    cat > /etc/logrotate.d/nginx-gateway << EOF
/var/log/nginx/*.log {
    daily
    missingok
    rotate 52
    compress
    delaycompress
    notifempty
    create 644 nginx nginx
    sharedscripts
    postrotate
        if [ -f /var/run/nginx.pid ]; then
            kill -USR1 \$(cat /var/run/nginx.pid)
        fi
    endscript
}
EOF
}

# 6. 监控脚本
setup_monitoring() {
    echo "配置监控脚本..."
    
    mkdir -p /opt/scripts
    
    cat > /opt/scripts/gateway_health_check.sh << 'EOF'
#!/bin/bash

# 网关健康检查脚本
GATEWAY_URL="http://localhost:8080/health"
LOG_FILE="/var/log/gateway_health.log"

check_health() {
    local timestamp=$(date '+%Y-%m-%d %H:%M:%S')
    
    # 检查nginx进程
    if ! pgrep nginx > /dev/null; then
        echo "[$timestamp] ERROR: Nginx process not running" >> $LOG_FILE
        return 1
    fi
    
    # 检查健康检查接口
    if ! curl -f -s $GATEWAY_URL > /dev/null; then
        echo "[$timestamp] ERROR: Health check failed" >> $LOG_FILE
        return 1
    fi
    
    echo "[$timestamp] INFO: Gateway healthy" >> $LOG_FILE
    return 0
}

# 执行检查
if ! check_health; then
    # 尝试重启nginx
    systemctl restart nginx-gateway
    sleep 5
    
    if check_health; then
        echo "[$timestamp] INFO: Gateway restarted successfully" >> $LOG_FILE
    else
        echo "[$timestamp] CRITICAL: Gateway restart failed" >> $LOG_FILE
        # 发送告警通知
        # curl -X POST "your-alert-webhook" -d "Gateway critical failure"
    fi
fi
EOF
    
    chmod +x /opt/scripts/gateway_health_check.sh
    
    # 添加到crontab
    (crontab -l 2>/dev/null; echo "*/1 * * * * /opt/scripts/gateway_health_check.sh") | crontab -
}

# 7. 防火墙配置
setup_firewall() {
    echo "配置防火墙..."
    
    # firewalld
    if systemctl is-active firewalld > /dev/null; then
        firewall-cmd --permanent --add-port=80/tcp
        firewall-cmd --permanent --add-port=443/tcp
        firewall-cmd --permanent --add-port=8080/tcp
        firewall-cmd --reload
    fi
    
    # iptables (如果使用)
    # iptables -A INPUT -p tcp --dport 80 -j ACCEPT
    # iptables -A INPUT -p tcp --dport 443 -j ACCEPT
    # iptables -A INPUT -p tcp --dport 8080 -j ACCEPT
}

# 主部署流程
main() {
    echo "开始部署多IDC网关..."
    
    install_dependencies
    compile_nginx
    deploy_config
    setup_systemd
    setup_logrotate
    setup_monitoring
    setup_firewall
    
    echo "测试配置文件..."
    $GATEWAY_HOME/sbin/nginx -t -c $GATEWAY_HOME/conf/nginx.conf
    
    if [ $? -eq 0 ]; then
        echo "启动nginx网关..."
        systemctl start nginx-gateway
        systemctl status nginx-gateway
        
        echo "=== 部署完成 ==="
        echo "网关地址: http://$(hostname):80"
        echo "健康检查: http://$(hostname):8080/health"
        echo "状态监控: http://$(hostname):8080/nginx_status"
        echo ""
        echo "日志文件:"
        echo "  访问日志: /var/log/nginx/gateway_access.log"
        echo "  错误日志: /var/log/nginx/error.log"
        echo "  健康日志: /var/log/gateway_health.log"
    else
        echo "配置文件测试失败，请检查配置"
        exit 1
    fi
}

# 执行主函数
main "$@" 
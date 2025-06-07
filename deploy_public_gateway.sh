#!/bin/bash

# 公网多IDC网关部署脚本
# 适用于没有VPN/专线的公网连接场景

set -e
set -o pipefail

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# 配置变量
NGINX_VERSION="1.25.3"
OPENSSL_VERSION="3.1.4"
CJSON_VERSION="1.7.16"
NGINX_USER="nginx"
NGINX_GROUP="nginx"
INSTALL_PREFIX="/usr/local/nginx"
LOG_DIR="/var/log/nginx"
SSL_DIR="/etc/nginx/ssl"
CONF_DIR="/etc/nginx"

# 域名配置（需要修改为实际域名）
GATEWAY_DOMAIN="gateway.yourcompany.com"
IDC_IPS=(
    "123.125.114.144"  # 北京IDC主IP
    "140.207.54.76"    # 上海IDC主IP
    "183.232.231.172"  # 深圳IDC主IP
)
IDC_BACKUP_IPS=(
    "124.126.115.145"  # 北京IDC备IP
    "141.208.55.77"    # 上海IDC备IP
    "184.233.232.173"  # 深圳IDC备IP
)

# 日志函数
log() {
    echo -e "${GREEN}[INFO]${NC} $(date '+%Y-%m-%d %H:%M:%S') - $1"
}

warn() {
    echo -e "${YELLOW}[WARN]${NC} $(date '+%Y-%m-%d %H:%M:%S') - $1"
}

error() {
    echo -e "${RED}[ERROR]${NC} $(date '+%Y-%m-%d %H:%M:%S') - $1"
    exit 1
}

# 检查系统
check_system() {
    log "检查系统环境..."
    
    if [[ $EUID -ne 0 ]]; then
        error "请使用root用户运行此脚本"
    fi
    
    # 检查Linux发行版
    if [ -f /etc/redhat-release ]; then
        DISTRO="centos"
        PKG_MANAGER="yum"
    elif [ -f /etc/debian_version ]; then
        DISTRO="ubuntu"
        PKG_MANAGER="apt"
    else
        error "不支持的Linux发行版"
    fi
    
    log "检测到系统: $DISTRO"
    
    # 检查内存（公网网关建议至少4GB）
    MEMORY_KB=$(grep MemTotal /proc/meminfo | awk '{print $2}')
    MEMORY_GB=$((MEMORY_KB / 1024 / 1024))
    
    if [ $MEMORY_GB -lt 4 ]; then
        warn "内存不足4GB ($MEMORY_GB GB)，可能影响性能"
    fi
    
    # 检查磁盘空间
    DISK_AVAIL=$(df / | tail -1 | awk '{print $4}')
    DISK_AVAIL_GB=$((DISK_AVAIL / 1024 / 1024))
    
    if [ $DISK_AVAIL_GB -lt 10 ]; then
        error "磁盘空间不足10GB"
    fi
    
    log "系统检查完成 - 内存: ${MEMORY_GB}GB, 可用磁盘: ${DISK_AVAIL_GB}GB"
}

# 安装系统依赖
install_dependencies() {
    log "安装系统依赖..."
    
    if [ "$DISTRO" = "centos" ]; then
        # CentOS/RHEL
        $PKG_MANAGER update -y
        $PKG_MANAGER groupinstall -y "Development Tools"
        $PKG_MANAGER install -y \
            pcre-devel zlib-devel openssl-devel \
            wget curl git vim htop iftop \
            firewalld fail2ban \
            logrotate rsyslog \
            bind-utils telnet nc \
            lua-devel libcjson-devel
            
        # 启用EPEL仓库获取更多工具
        $PKG_MANAGER install -y epel-release
        $PKG_MANAGER install -y jq tree
        
    elif [ "$DISTRO" = "ubuntu" ]; then
        # Ubuntu/Debian
        $PKG_MANAGER update -y
        $PKG_MANAGER install -y \
            build-essential \
            libpcre3-dev zlib1g-dev libssl-dev \
            wget curl git vim htop iftop \
            ufw fail2ban \
            logrotate rsyslog \
            dnsutils telnet netcat \
            liblua5.1-dev libcjson-dev \
            jq tree
    fi
    
    log "系统依赖安装完成"
}

# 创建用户和目录
setup_users_and_dirs() {
    log "创建nginx用户和目录..."
    
    # 创建nginx用户
    if ! id "$NGINX_USER" &>/dev/null; then
        useradd -r -s /sbin/nologin -d /var/cache/nginx -c "nginx user" $NGINX_USER
        log "已创建nginx用户"
    fi
    
    # 创建必要目录
    mkdir -p $INSTALL_PREFIX/{sbin,conf,logs}
    mkdir -p $LOG_DIR
    mkdir -p $SSL_DIR
    mkdir -p $CONF_DIR/{conf.d,sites-available,sites-enabled}
    mkdir -p /var/cache/nginx
    mkdir -p /etc/nginx/blocked_ips.conf
    
    # 设置权限
    chown -R $NGINX_USER:$NGINX_GROUP $INSTALL_PREFIX
    chown -R $NGINX_USER:$NGINX_GROUP $LOG_DIR
    chown -R $NGINX_USER:$NGINX_GROUP /var/cache/nginx
    
    # 创建空的IP黑名单文件
    touch /etc/nginx/blocked_ips.conf
    echo "# 动态IP黑名单" > /etc/nginx/blocked_ips.conf
    
    log "用户和目录创建完成"
}

# 编译安装Nginx
compile_nginx() {
    log "开始编译安装Nginx..."
    
    cd /tmp
    
    # 下载Nginx源码
    if [ ! -f "nginx-${NGINX_VERSION}.tar.gz" ]; then
        log "下载Nginx源码..."
        wget -O nginx-${NGINX_VERSION}.tar.gz \
            http://nginx.org/download/nginx-${NGINX_VERSION}.tar.gz
    fi
    
    # 下载OpenSSL源码（获得最新SSL支持）
    if [ ! -f "openssl-${OPENSSL_VERSION}.tar.gz" ]; then
        log "下载OpenSSL源码..."
        wget -O openssl-${OPENSSL_VERSION}.tar.gz \
            https://www.openssl.org/source/openssl-${OPENSSL_VERSION}.tar.gz
    fi
    
    # 解压
    tar -zxf nginx-${NGINX_VERSION}.tar.gz
    tar -zxf openssl-${OPENSSL_VERSION}.tar.gz
    
    cd nginx-${NGINX_VERSION}
    
    # 复制我们的自定义模块
    if [ -f "../ngx_http_upstream_json_hash_module.c" ]; then
        mkdir -p src/http/modules/json_hash/
        cp ../ngx_http_upstream_json_hash_module.c src/http/modules/json_hash/
        log "已复制json_hash模块"
    else
        warn "未找到json_hash模块文件，将跳过"
    fi
    
    # 配置编译选项（公网优化）
    log "配置编译选项..."
    ./configure \
        --prefix=$INSTALL_PREFIX \
        --sbin-path=$INSTALL_PREFIX/sbin/nginx \
        --conf-path=$CONF_DIR/nginx.conf \
        --error-log-path=$LOG_DIR/error.log \
        --access-log-path=$LOG_DIR/access.log \
        --pid-path=/var/run/nginx.pid \
        --lock-path=/var/run/nginx.lock \
        --http-client-body-temp-path=/var/cache/nginx/client_temp \
        --http-proxy-temp-path=/var/cache/nginx/proxy_temp \
        --http-fastcgi-temp-path=/var/cache/nginx/fastcgi_temp \
        --http-uwsgi-temp-path=/var/cache/nginx/uwsgi_temp \
        --http-scgi-temp-path=/var/cache/nginx/scgi_temp \
        --user=$NGINX_USER \
        --group=$NGINX_GROUP \
        --with-openssl=../openssl-${OPENSSL_VERSION} \
        --with-http_ssl_module \
        --with-http_realip_module \
        --with-http_addition_module \
        --with-http_sub_module \
        --with-http_dav_module \
        --with-http_flv_module \
        --with-http_mp4_module \
        --with-http_gunzip_module \
        --with-http_gzip_static_module \
        --with-http_random_index_module \
        --with-http_secure_link_module \
        --with-http_stub_status_module \
        --with-http_auth_request_module \
        --with-http_xslt_module=dynamic \
        --with-http_image_filter_module=dynamic \
        --with-http_geoip_module=dynamic \
        --with-threads \
        --with-stream \
        --with-stream_ssl_module \
        --with-stream_ssl_preread_module \
        --with-stream_realip_module \
        --with-stream_geoip_module=dynamic \
        --with-http_slice_module \
        --with-http_v2_module \
        --with-http_v3_module \
        --with-file-aio \
        --with-http_secure_link_module \
        --add-module=src/http/modules/json_hash \
        --with-cc-opt='-O2 -g -pipe -Wall -Wp,-D_FORTIFY_SOURCE=2 -fexceptions -fstack-protector --param=ssp-buffer-size=4 -m64 -mtune=generic' \
        --with-ld-opt='-lcjson'
    
    # 编译
    log "开始编译..."
    make -j$(nproc)
    
    # 安装
    log "安装Nginx..."
    make install
    
    # 创建软链接
    ln -sf $INSTALL_PREFIX/sbin/nginx /usr/local/bin/nginx
    
    log "Nginx编译安装完成"
}

# 生成SSL证书
generate_ssl_certificates() {
    log "生成SSL证书..."
    
    cd $SSL_DIR
    
    # 生成CA私钥
    openssl genrsa -out ca.key 4096
    
    # 生成CA证书
    openssl req -new -x509 -key ca.key -sha256 -subj "/C=CN/ST=Beijing/L=Beijing/O=YourCompany/OU=IT/CN=YourCompany-CA" -days 3650 -out ca.crt
    
    # 生成网关服务器私钥
    openssl genrsa -out gateway.key 4096
    
    # 生成网关服务器证书请求
    openssl req -new -key gateway.key -subj "/C=CN/ST=Beijing/L=Beijing/O=YourCompany/OU=Gateway/CN=$GATEWAY_DOMAIN" -out gateway.csr
    
    # 创建扩展文件
    cat > gateway.ext << EOF
authorityKeyIdentifier=keyid,issuer
basicConstraints=CA:FALSE
keyUsage = digitalSignature, nonRepudiation, keyEncipherment, dataEncipherment
subjectAltName = @alt_names

[alt_names]
DNS.1 = $GATEWAY_DOMAIN
DNS.2 = *.yourcompany.com
IP.1 = 127.0.0.1
EOF
    
    # 生成网关服务器证书
    openssl x509 -req -in gateway.csr -CA ca.crt -CAkey ca.key -CAcreateserial -out gateway.crt -days 365 -sha256 -extfile gateway.ext
    
    # 生成IDC服务器证书
    local idc_names=("beijing" "shanghai" "shenzhen")
    for i in "${!IDC_IPS[@]}"; do
        local idc_name="${idc_names[i]}"
        local idc_ip="${IDC_IPS[i]}"
        local backup_ip="${IDC_BACKUP_IPS[i]}"
        
        log "为 ${idc_name} IDC (${idc_ip}) 生成证书..."
        
        openssl genrsa -out idc_${idc_name}_server.key 4096
        openssl req -new -key idc_${idc_name}_server.key -subj "/C=CN/ST=Beijing/L=Beijing/O=YourCompany/OU=IDC/CN=${idc_name}-idc" -out idc_${idc_name}_server.csr
        
        cat > idc_${idc_name}_server.ext << EOF
authorityKeyIdentifier=keyid,issuer
basicConstraints=CA:FALSE
keyUsage = digitalSignature, nonRepudiation, keyEncipherment, dataEncipherment
subjectAltName = @alt_names

[alt_names]
IP.1 = ${idc_ip}
IP.2 = ${backup_ip}
DNS.1 = ${idc_name}-idc
DNS.2 = ${idc_name}-backup
EOF
        
        openssl x509 -req -in idc_${idc_name}_server.csr -CA ca.crt -CAkey ca.key -CAcreateserial -out idc_${idc_name}_server.crt -days 365 -sha256 -extfile idc_${idc_name}_server.ext
        
        # 为每个IDC复制证书（实际部署时需要分发到各IDC）
        mkdir -p ${idc_name}_idc_certs
        cp idc_${idc_name}_server.crt ${idc_name}_idc_certs/idc_server.crt
        cp idc_${idc_name}_server.key ${idc_name}_idc_certs/idc_server.key
        cp ca.crt ${idc_name}_idc_certs/gateway_ca.crt
    done
    
    # 生成客户端证书（用于双向认证）
    openssl genrsa -out client.key 4096
    openssl req -new -key client.key -subj "/C=CN/ST=Beijing/L=Beijing/O=YourCompany/OU=Client/CN=gateway-client" -out client.csr
    openssl x509 -req -in client.csr -CA ca.crt -CAkey ca.key -CAcreateserial -out client.crt -days 365 -sha256
    
    # 为IDC使用的CA证书
    cp ca.crt idc_ca.crt
    cp ca.crt client_ca.crt
    
    # 设置权限
    chmod 600 *.key
    chmod 644 *.crt
    chown -R $NGINX_USER:$NGINX_GROUP $SSL_DIR
    
    log "SSL证书生成完成"
    
    # 显示证书信息
    log "网关证书信息:"
    openssl x509 -in gateway.crt -text -noout | grep -E "(Subject:|DNS:|IP Address:|Not After)"
}

# 配置防火墙
setup_firewall() {
    log "配置防火墙..."
    
    if [ "$DISTRO" = "centos" ]; then
        # CentOS使用firewalld
        systemctl enable firewalld
        systemctl start firewalld
        
        # 开放必要端口
        firewall-cmd --permanent --add-port=80/tcp
        firewall-cmd --permanent --add-port=443/tcp
        firewall-cmd --permanent --add-port=8080/tcp  # 健康检查
        firewall-cmd --permanent --add-port=8081/tcp  # 监控
        
        # 限制访问监控端口（仅内网）
        firewall-cmd --permanent --add-rich-rule='rule family="ipv4" source address="10.0.0.0/8" port protocol="tcp" port="8080" accept'
        firewall-cmd --permanent --add-rich-rule='rule family="ipv4" source address="172.16.0.0/12" port protocol="tcp" port="8080" accept'
        firewall-cmd --permanent --add-rich-rule='rule family="ipv4" source address="192.168.0.0/16" port protocol="tcp" port="8080" accept'
        
        firewall-cmd --reload
        
    elif [ "$DISTRO" = "ubuntu" ]; then
        # Ubuntu使用ufw
        ufw --force enable
        
        # 开放必要端口
        ufw allow 80/tcp
        ufw allow 443/tcp
        ufw allow from 10.0.0.0/8 to any port 8080
        ufw allow from 172.16.0.0/12 to any port 8080
        ufw allow from 192.168.0.0/16 to any port 8080
        ufw allow from 10.0.0.0/8 to any port 8081
        ufw allow from 172.16.0.0/12 to any port 8081
        ufw allow from 192.168.0.0/16 to any port 8081
        
        ufw --force reload
    fi
    
    log "防火墙配置完成"
}

# 配置安全防护
setup_security() {
    log "配置安全防护..."
    
    # 配置fail2ban
    if [ "$DISTRO" = "centos" ]; then
        systemctl enable fail2ban
        systemctl start fail2ban
    elif [ "$DISTRO" = "ubuntu" ]; then
        systemctl enable fail2ban
        systemctl start fail2ban
    fi
    
    # 创建nginx专用的fail2ban配置
    cat > /etc/fail2ban/filter.d/nginx-public-gateway.conf << 'EOF'
[Definition]
failregex = ^<HOST> -.*"(GET|POST|HEAD).*" (4\d\d|5\d\d) .*$
            ^<HOST> -.*"(GET|POST|HEAD).*" 403 .*$
ignoreregex =
EOF
    
    cat > /etc/fail2ban/jail.d/nginx-public-gateway.conf << 'EOF'
[nginx-public-gateway]
enabled = true
port = http,https
filter = nginx-public-gateway
logpath = /var/log/nginx/gateway_access.log
maxretry = 10
bantime = 3600
findtime = 600
action = iptables-multiport[name=nginx-public-gateway, port="http,https", protocol=tcp]
EOF
    
    systemctl restart fail2ban
    
    # 系统内核参数优化（公网高并发）
    cat >> /etc/sysctl.conf << 'EOF'

# 公网网关优化参数
net.core.somaxconn = 65535
net.core.netdev_max_backlog = 5000
net.ipv4.tcp_max_syn_backlog = 65535
net.ipv4.tcp_fin_timeout = 10
net.ipv4.tcp_keepalive_time = 1200
net.ipv4.tcp_max_tw_buckets = 6000
net.ipv4.ip_local_port_range = 1024 65535
net.ipv4.tcp_rmem = 4096 32768 134217728
net.ipv4.tcp_wmem = 4096 32768 134217728
net.core.rmem_default = 262144
net.core.rmem_max = 134217728
net.core.wmem_default = 262144
net.core.wmem_max = 134217728
fs.file-max = 6815744
net.ipv4.tcp_congestion_control = bbr
EOF
    
    sysctl -p
    
    # 设置文件描述符限制
    cat >> /etc/security/limits.conf << 'EOF'
* soft nofile 65535
* hard nofile 65535
nginx soft nofile 65535
nginx hard nofile 65535
EOF
    
    log "安全防护配置完成"
}

# 创建Nginx配置文件
create_nginx_config() {
    log "创建Nginx配置文件..."
    
    # 复制主配置文件
    cp multi_idc_public_gateway.conf $CONF_DIR/nginx.conf
    
    # 替换域名占位符为实际域名
    sed -i "s/gateway.yourcompany.com/$GATEWAY_DOMAIN/g" $CONF_DIR/nginx.conf
    sed -i "s/beijing.yourdomain.com/${IDC_DOMAINS[0]}/g" $CONF_DIR/nginx.conf
    sed -i "s/shanghai.yourdomain.com/${IDC_DOMAINS[1]}/g" $CONF_DIR/nginx.conf
    sed -i "s/shenzhen.yourdomain.com/${IDC_DOMAINS[2]}/g" $CONF_DIR/nginx.conf
    
    # 创建systemd服务文件
    cat > /etc/systemd/system/nginx.service << 'EOF'
[Unit]
Description=The nginx HTTP and reverse proxy server
After=network.target remote-fs.target nss-lookup.target

[Service]
Type=forking
PIDFile=/var/run/nginx.pid
ExecStartPre=/usr/local/bin/nginx -t
ExecStart=/usr/local/bin/nginx
ExecReload=/bin/kill -s HUP $MAINPID
KillSignal=SIGQUIT
TimeoutStopSec=5
KillMode=process
PrivateTmp=true
RestartSec=5
Restart=on-failure

[Install]
WantedBy=multi-user.target
EOF
    
    # 创建日志轮转配置
    cat > /etc/logrotate.d/nginx << 'EOF'
/var/log/nginx/*.log {
    daily
    missingok
    rotate 30
    compress
    delaycompress
    notifempty
    create 0640 nginx nginx
    sharedscripts
    postrotate
        if [ -f /var/run/nginx.pid ]; then
            kill -USR1 `cat /var/run/nginx.pid`
        fi
    endscript
}
EOF
    
    # 验证配置文件
    log "验证Nginx配置..."
    if $INSTALL_PREFIX/sbin/nginx -t; then
        log "配置文件验证成功"
    else
        error "配置文件验证失败"
    fi
    
    log "Nginx配置文件创建完成"
}

# 创建监控脚本
create_monitoring_scripts() {
    log "创建监控脚本..."
    
    # 健康检查脚本
    cat > /usr/local/bin/gateway_health_check.sh << 'EOF'
#!/bin/bash

LOGFILE="/var/log/gateway_health.log"
NGINX_PID_FILE="/var/run/nginx.pid"

log_message() {
    echo "$(date '+%Y-%m-%d %H:%M:%S') - $1" >> $LOGFILE
}

# 检查Nginx进程
if ! pgrep nginx > /dev/null; then
    log_message "ERROR: Nginx process not running, attempting restart..."
    systemctl restart nginx
    if [ $? -eq 0 ]; then
        log_message "INFO: Nginx restarted successfully"
    else
        log_message "ERROR: Failed to restart Nginx"
        exit 1
    fi
fi

# 检查端口监听
if ! netstat -tuln | grep -q ":443 "; then
    log_message "ERROR: Port 443 not listening"
    exit 1
fi

# 检查SSL证书有效期
CERT_FILE="/etc/nginx/ssl/gateway.crt"
if [ -f "$CERT_FILE" ]; then
    EXPIRE_DATE=$(openssl x509 -in $CERT_FILE -noout -enddate | cut -d= -f2)
    EXPIRE_TIMESTAMP=$(date -d "$EXPIRE_DATE" +%s)
    CURRENT_TIMESTAMP=$(date +%s)
    DAYS_LEFT=$(( (EXPIRE_TIMESTAMP - CURRENT_TIMESTAMP) / 86400 ))
    
    if [ $DAYS_LEFT -lt 30 ]; then
        log_message "WARNING: SSL certificate expires in $DAYS_LEFT days"
    fi
fi

# 检查磁盘空间
DISK_USAGE=$(df / | tail -1 | awk '{print $5}' | sed 's/%//')
if [ $DISK_USAGE -gt 80 ]; then
    log_message "WARNING: Disk usage is ${DISK_USAGE}%"
fi

# 检查内存使用
MEMORY_USAGE=$(free | grep Mem | awk '{printf("%.0f", $3/$2 * 100.0)}')
if [ $MEMORY_USAGE -gt 80 ]; then
    log_message "WARNING: Memory usage is ${MEMORY_USAGE}%"
fi

log_message "INFO: Health check completed successfully"
EOF
    
    chmod +x /usr/local/bin/gateway_health_check.sh
    
    # 创建crontab任务
    (crontab -l 2>/dev/null; echo "*/5 * * * * /usr/local/bin/gateway_health_check.sh") | crontab -
    
    # 网络连通性检查脚本
    cat > /usr/local/bin/idc_connectivity_check.sh << 'EOF'
#!/bin/bash

LOGFILE="/var/log/idc_connectivity.log"
IDC_HOSTS=("beijing.yourdomain.com" "shanghai.yourdomain.com" "shenzhen.yourdomain.com")

log_message() {
    echo "$(date '+%Y-%m-%d %H:%M:%S') - $1" >> $LOGFILE
}

for host in "${IDC_HOSTS[@]}"; do
    if timeout 10 openssl s_client -connect $host:443 -verify_return_error < /dev/null > /dev/null 2>&1; then
        log_message "INFO: $host is reachable via HTTPS"
    else
        log_message "ERROR: $host is not reachable or SSL verification failed"
        
        # 尝试普通TCP连接
        if timeout 5 nc -z $host 443; then
            log_message "INFO: $host TCP connection OK, but SSL verification failed"
        else
            log_message "ERROR: $host TCP connection failed"
        fi
    fi
done
EOF
    
    chmod +x /usr/local/bin/idc_connectivity_check.sh
    
    # 每10分钟检查一次连通性
    (crontab -l 2>/dev/null; echo "*/10 * * * * /usr/local/bin/idc_connectivity_check.sh") | crontab -
    
    log "监控脚本创建完成"
}

# 优化系统性能
optimize_system() {
    log "优化系统性能..."
    
    # 设置CPU性能模式
    if [ -f /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor ]; then
        echo performance > /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor
    fi
    
    # 禁用不必要的服务
    services_to_disable=("postfix" "cups" "avahi-daemon" "bluetooth")
    for service in "${services_to_disable[@]}"; do
        if systemctl is-enabled $service &>/dev/null; then
            systemctl disable $service
            log "已禁用服务: $service"
        fi
    done
    
    # 清理系统缓存
    sync
    echo 1 > /proc/sys/vm/drop_caches
    
    log "系统性能优化完成"
}

# 启动服务
start_services() {
    log "启动服务..."
    
    # 重新加载systemd
    systemctl daemon-reload
    
    # 启动并启用nginx
    systemctl enable nginx
    systemctl start nginx
    
    # 检查nginx状态
    if systemctl is-active nginx &>/dev/null; then
        log "Nginx服务启动成功"
    else
        error "Nginx服务启动失败"
    fi
    
    # 启动监控和安全服务
    systemctl enable fail2ban
    systemctl restart fail2ban
    
    log "所有服务启动完成"
}

# 测试网关功能
test_gateway() {
    log "测试网关功能..."
    
    # 等待服务完全启动
    sleep 5
    
    # 测试HTTP重定向到HTTPS
    log "测试HTTP到HTTPS重定向..."
    http_response=$(curl -s -o /dev/null -w "%{http_code}" http://localhost/api/test || echo "000")
    if [ "$http_response" = "301" ] || [ "$http_response" = "302" ]; then
        log "✓ HTTP重定向功能正常"
    else
        warn "HTTP重定向测试失败 (响应码: $http_response)"
    fi
    
    # 测试HTTPS健康检查
    log "测试HTTPS健康检查..."
    https_response=$(curl -k -s -o /dev/null -w "%{http_code}" https://localhost/gateway/health || echo "000")
    if [ "$https_response" = "200" ]; then
        log "✓ HTTPS健康检查正常"
    else
        warn "HTTPS健康检查失败 (响应码: $https_response)"
    fi
    
    # 测试SSL证书
    log "测试SSL证书..."
    if openssl s_client -connect localhost:443 -servername $GATEWAY_DOMAIN < /dev/null 2>/dev/null | grep -q "Verify return code: 0"; then
        log "✓ SSL证书验证正常"
    else
        warn "SSL证书验证可能存在问题"
    fi
    
    # 显示服务状态
    log "Nginx进程状态:"
    ps aux | grep nginx | grep -v grep
    
    log "端口监听状态:"
    netstat -tuln | grep -E ":(80|443|8080|8081) "
    
    log "网关功能测试完成"
}

# 显示部署信息
show_deployment_info() {
    log "==============================================="
    log "公网多IDC网关部署完成！"
    log "==============================================="
    
    echo ""
    echo -e "${BLUE}部署信息:${NC}"
    echo "  网关域名: $GATEWAY_DOMAIN"
    echo "  HTTPS端口: 443"
    echo "  健康检查: https://$GATEWAY_DOMAIN/gateway/health"
    echo "  监控端口: 8081 (仅内网访问)"
    echo ""
    
    echo -e "${BLUE}SSL证书信息:${NC}"
    openssl x509 -in $SSL_DIR/gateway.crt -noout -subject -dates
    echo ""
    
    echo -e "${BLUE}IDC证书分发:${NC}"
    echo "  需要将以下证书文件部署到各IDC:"
    for domain in "${IDC_DOMAINS[@]}"; do
        echo "    - ${SSL_DIR}/${domain}_certs/ -> $domain"
    done
    echo ""
    
    echo -e "${BLUE}配置文件位置:${NC}"
    echo "  主配置: $CONF_DIR/nginx.conf"
    echo "  SSL证书: $SSL_DIR/"
    echo "  日志目录: $LOG_DIR/"
    echo ""
    
    echo -e "${BLUE}管理命令:${NC}"
    echo "  启动: systemctl start nginx"
    echo "  停止: systemctl stop nginx"
    echo "  重启: systemctl restart nginx"
    echo "  重载配置: systemctl reload nginx"
    echo "  查看状态: systemctl status nginx"
    echo "  查看日志: tail -f $LOG_DIR/gateway_access.log"
    echo ""
    
    echo -e "${BLUE}监控和维护:${NC}"
    echo "  健康检查日志: tail -f /var/log/gateway_health.log"
    echo "  IDC连通性日志: tail -f /var/log/idc_connectivity.log"
    echo "  fail2ban状态: fail2ban-client status"
    echo "  动态封禁IP: curl 'http://localhost:8080/admin/block_ip?ip=1.2.3.4'"
    echo ""
    
    echo -e "${YELLOW}重要提醒:${NC}"
    echo "1. 请修改配置文件中的域名为实际域名"
    echo "2. 请将IDC证书部署到各个机房"
    echo "3. 请配置DNS解析指向此网关"
    echo "4. 请在各IDC部署 idc_reverse_proxy.conf"
    echo "5. 请定期更新SSL证书"
    echo "6. 请监控网关性能和安全日志"
    echo ""
    
    log "部署信息显示完成"
}

# 主函数
main() {
    log "开始公网多IDC网关部署..."
    
    check_system
    install_dependencies
    setup_users_and_dirs
    compile_nginx
    generate_ssl_certificates
    setup_firewall
    setup_security
    create_nginx_config
    create_monitoring_scripts
    optimize_system
    start_services
    test_gateway
    show_deployment_info
    
    log "公网多IDC网关部署完成！"
}

# 执行主函数
main "$@" 
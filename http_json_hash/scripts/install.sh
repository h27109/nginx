#!/bin/bash

# Nginx JSON Hash Module Installation Script
# 支持自动检测操作系统并安装依赖

set -e

# 颜色定义
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

# 检查是否为root用户
check_root() {
    if [[ $EUID -eq 0 ]]; then
        log_warn "Running as root. Consider using a non-root user with sudo."
    fi
}

# 检测操作系统
detect_os() {
    if [[ -f /etc/os-release ]]; then
        source /etc/os-release
        OS_ID=$ID
        OS_VERSION=$VERSION_ID
    elif [[ -f /etc/redhat-release ]]; then
        OS_ID="centos"
    elif [[ -f /etc/debian_version ]]; then
        OS_ID="debian"
    else
        log_error "Unsupported operating system"
        exit 1
    fi
    
    log_info "Detected OS: $OS_ID $OS_VERSION"
}

# 安装依赖
install_dependencies() {
    log_info "Installing dependencies..."
    
    case $OS_ID in
        ubuntu|debian)
            sudo apt-get update
            sudo apt-get install -y \
                build-essential \
                libcjson-dev \
                libpcre3-dev \
                libssl-dev \
                zlib1g-dev \
                wget \
                curl
            ;;
        centos|rhel|fedora)
            if command -v dnf &> /dev/null; then
                sudo dnf install -y \
                    gcc \
                    gcc-c++ \
                    make \
                    libcjson-devel \
                    pcre-devel \
                    openssl-devel \
                    zlib-devel \
                    wget \
                    curl
            else
                sudo yum install -y \
                    gcc \
                    gcc-c++ \
                    make \
                    libcjson-devel \
                    pcre-devel \
                    openssl-devel \
                    zlib-devel \
                    wget \
                    curl
            fi
            ;;
        *)
            log_error "Unsupported OS: $OS_ID"
            exit 1
            ;;
    esac
    
    log_info "Dependencies installed successfully"
}

# 下载nginx源码
download_nginx() {
    local nginx_version=${1:-"1.20.2"}
    local download_dir=${2:-"/tmp"}
    
    log_info "Downloading nginx-$nginx_version..."
    
    cd $download_dir
    wget -q "http://nginx.org/download/nginx-$nginx_version.tar.gz"
    tar -xzf "nginx-$nginx_version.tar.gz"
    
    log_info "Nginx source downloaded to $download_dir/nginx-$nginx_version"
    echo "$download_dir/nginx-$nginx_version"
}

# 编译nginx
compile_nginx() {
    local nginx_dir=$1
    local module_dir=$2
    local prefix=${3:-"/usr/local/nginx"}
    
    log_info "Compiling nginx with JSON hash module..."
    
    # 复制模块源码
    cp "$module_dir/../src/http/modules/ngx_http_upstream_json_hash_module.c" "$nginx_dir/src/http/modules/"
    
    cd $nginx_dir
    
    # 配置编译参数
    ./configure \
        --prefix=$prefix \
        --sbin-path=$prefix/sbin/nginx \
        --conf-path=$prefix/conf/nginx.conf \
        --error-log-path=/var/log/nginx/error.log \
        --access-log-path=/var/log/nginx/access.log \
        --pid-path=/var/run/nginx.pid \
        --lock-path=/var/run/nginx.lock \
        --http-client-body-temp-path=/var/cache/nginx/client_temp \
        --http-proxy-temp-path=/var/cache/nginx/proxy_temp \
        --http-fastcgi-temp-path=/var/cache/nginx/fastcgi_temp \
        --http-uwsgi-temp-path=/var/cache/nginx/uwsgi_temp \
        --http-scgi-temp-path=/var/cache/nginx/scgi_temp \
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
        --with-file-aio \
        --with-http_v2_module
    
    # 编译
    make -j$(nproc)
    
    log_info "Nginx compiled successfully"
}

# 安装nginx
install_nginx() {
    local nginx_dir=$1
    local prefix=${2:-"/usr/local/nginx"}
    
    log_info "Installing nginx..."
    
    cd $nginx_dir
    sudo make install
    
    # 创建必要的目录
    sudo mkdir -p /var/cache/nginx/{client_temp,proxy_temp,fastcgi_temp,uwsgi_temp,scgi_temp}
    sudo mkdir -p /var/log/nginx
    
    # 创建nginx用户
    if ! id "nginx" &>/dev/null; then
        sudo useradd -r -s /bin/false nginx
    fi
    
    # 设置权限
    sudo chown -R nginx:nginx /var/cache/nginx
    sudo chown -R nginx:nginx /var/log/nginx
    
    log_info "Nginx installed to $prefix"
}

# 创建systemd服务文件
create_systemd_service() {
    local prefix=${1:-"/usr/local/nginx"}
    
    log_info "Creating systemd service file..."
    
    sudo tee /etc/systemd/system/nginx.service > /dev/null <<EOF
[Unit]
Description=The nginx HTTP and reverse proxy server
Documentation=http://nginx.org/en/docs/
After=network.target remote-fs.target nss-lookup.target

[Service]
Type=forking
PIDFile=/var/run/nginx.pid
ExecStartPre=$prefix/sbin/nginx -t
ExecStart=$prefix/sbin/nginx
ExecReload=/bin/kill -s HUP \$MAINPID
KillSignal=SIGQUIT
TimeoutStopSec=5
KillMode=process
PrivateTmp=true

[Install]
WantedBy=multi-user.target
EOF
    
    sudo systemctl daemon-reload
    sudo systemctl enable nginx
    
    log_info "Systemd service created and enabled"
}

# 验证安装
verify_installation() {
    local prefix=${1:-"/usr/local/nginx"}
    
    log_info "Verifying installation..."
    
    # 检查nginx二进制文件
    if [[ ! -f "$prefix/sbin/nginx" ]]; then
        log_error "Nginx binary not found at $prefix/sbin/nginx"
        return 1
    fi
    
    # 检查模块是否加载
    if $prefix/sbin/nginx -V 2>&1 | grep -q "json_hash"; then
        log_info "JSON hash module loaded successfully"
    else
        log_warn "JSON hash module not found in nginx build"
    fi
    
    # 测试配置
    if $prefix/sbin/nginx -t; then
        log_info "Nginx configuration test passed"
    else
        log_error "Nginx configuration test failed"
        return 1
    fi
    
    log_info "Installation verified successfully"
}

# 安装配置示例
install_config_examples() {
    local module_dir=$1
    local prefix=${2:-"/usr/local/nginx"}
    
    log_info "Installing configuration examples..."
    
    sudo mkdir -p "$prefix/conf/examples"
    sudo cp "$module_dir/config/"*.conf "$prefix/conf/examples/"
    
    log_info "Configuration examples installed to $prefix/conf/examples/"
}

# 主函数
main() {
    local nginx_version="1.20.2"
    local prefix="/usr/local/nginx"
    local module_dir=""
    local temp_dir="/tmp"
    local skip_deps=false
    
    # 解析命令行参数
    while [[ $# -gt 0 ]]; do
        case $1 in
            --nginx-version)
                nginx_version="$2"
                shift 2
                ;;
            --prefix)
                prefix="$2"
                shift 2
                ;;
            --module-dir)
                module_dir="$2"
                shift 2
                ;;
            --temp-dir)
                temp_dir="$2"
                shift 2
                ;;
            --skip-deps)
                skip_deps=true
                shift
                ;;
            --help)
                echo "Usage: $0 [OPTIONS]"
                echo "Options:"
                echo "  --nginx-version VERSION   Nginx version to download (default: 1.20.2)"
                echo "  --prefix PATH            Installation prefix (default: /usr/local/nginx)"
                echo "  --module-dir PATH        Path to module directory (default: current dir)"
                echo "  --temp-dir PATH          Temporary directory (default: /tmp)"
                echo "  --skip-deps              Skip dependency installation"
                echo "  --help                   Show this help message"
                exit 0
                ;;
            *)
                log_error "Unknown option: $1"
                exit 1
                ;;
        esac
    done
    
    # 设置默认模块目录
    if [[ -z "$module_dir" ]]; then
        module_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
    fi
    
    log_info "Starting nginx JSON hash module installation"
    log_info "Nginx version: $nginx_version"
    log_info "Installation prefix: $prefix"
    log_info "Module directory: $module_dir"
    
    check_root
    detect_os
    
    if [[ "$skip_deps" != "true" ]]; then
        install_dependencies
    fi
    
    nginx_dir=$(download_nginx "$nginx_version" "$temp_dir")
    compile_nginx "$nginx_dir" "$module_dir" "$prefix"
    install_nginx "$nginx_dir" "$prefix"
    create_systemd_service "$prefix"
    install_config_examples "$module_dir" "$prefix"
    verify_installation "$prefix"
    
    log_info "Installation completed successfully!"
    log_info "You can now start nginx with: sudo systemctl start nginx"
    log_info "Configuration examples are available in: $prefix/conf/examples/"
}

# 运行主函数
main "$@" 
# Nginx JSON Hash Module Makefile
# 支持编译、测试、安装和清理操作

# 默认值
NGINX_VERSION ?= 1.20.2
NGINX_PREFIX ?= /usr/local/nginx
TEMP_DIR ?= /tmp
MODULE_DIR := $(PWD)

# 颜色定义
GREEN := \033[0;32m
YELLOW := \033[1;33m
RED := \033[0;31m
NC := \033[0m # No Color

# 检查依赖
.PHONY: check-deps
check-deps:
	@echo -e "$(GREEN)[INFO]$(NC) Checking dependencies..."
	@command -v curl >/dev/null 2>&1 || { echo -e "$(RED)[ERROR]$(NC) curl is required"; exit 1; }
	@command -v wget >/dev/null 2>&1 || { echo -e "$(RED)[ERROR]$(NC) wget is required"; exit 1; }
	@command -v gcc >/dev/null 2>&1 || { echo -e "$(RED)[ERROR]$(NC) gcc is required"; exit 1; }
	@command -v make >/dev/null 2>&1 || { echo -e "$(RED)[ERROR]$(NC) make is required"; exit 1; }
	@echo -e "$(GREEN)[INFO]$(NC) All dependencies satisfied"

# 安装系统依赖
.PHONY: install-deps
install-deps:
	@echo -e "$(GREEN)[INFO]$(NC) Installing system dependencies..."
	@if command -v apt-get >/dev/null 2>&1; then \
		sudo apt-get update && sudo apt-get install -y build-essential libcjson-dev libpcre3-dev libssl-dev zlib1g-dev wget curl; \
	elif command -v yum >/dev/null 2>&1; then \
		sudo yum install -y gcc gcc-c++ make libcjson-devel pcre-devel openssl-devel zlib-devel wget curl; \
	elif command -v dnf >/dev/null 2>&1; then \
		sudo dnf install -y gcc gcc-c++ make libcjson-devel pcre-devel openssl-devel zlib-devel wget curl; \
	else \
		echo -e "$(RED)[ERROR]$(NC) Unsupported package manager"; exit 1; \
	fi
	@echo -e "$(GREEN)[INFO]$(NC) Dependencies installed successfully"

# 下载nginx源码
.PHONY: download-nginx
download-nginx:
	@echo -e "$(GREEN)[INFO]$(NC) Downloading nginx-$(NGINX_VERSION)..."
	@cd $(TEMP_DIR) && \
		wget -q "http://nginx.org/download/nginx-$(NGINX_VERSION).tar.gz" && \
		tar -xzf "nginx-$(NGINX_VERSION).tar.gz"
	@echo -e "$(GREEN)[INFO]$(NC) Nginx source downloaded to $(TEMP_DIR)/nginx-$(NGINX_VERSION)"

# 编译nginx
.PHONY: compile
compile: check-deps download-nginx
	@echo -e "$(GREEN)[INFO]$(NC) Compiling nginx with JSON hash module..."
	@cp $(MODULE_DIR)/../src/http/modules/ngx_http_upstream_json_hash_module.c $(TEMP_DIR)/nginx-$(NGINX_VERSION)/src/http/modules/
	@cd $(TEMP_DIR)/nginx-$(NGINX_VERSION) && \
		./configure \
			--prefix=$(NGINX_PREFIX) \
			--sbin-path=$(NGINX_PREFIX)/sbin/nginx \
			--conf-path=$(NGINX_PREFIX)/conf/nginx.conf \
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
			--with-http_stub_status_module \
			--with-http_v2_module \
			--with-threads && \
		make -j$$(nproc)
	@echo -e "$(GREEN)[INFO]$(NC) Compilation completed successfully"

# 安装nginx
.PHONY: install
install: compile
	@echo -e "$(GREEN)[INFO]$(NC) Installing nginx..."
	@cd $(TEMP_DIR)/nginx-$(NGINX_VERSION) && sudo make install
	@sudo mkdir -p /var/cache/nginx/{client_temp,proxy_temp,fastcgi_temp,uwsgi_temp,scgi_temp}
	@sudo mkdir -p /var/log/nginx
	@if ! id "nginx" >/dev/null 2>&1; then sudo useradd -r -s /bin/false nginx; fi
	@sudo chown -R nginx:nginx /var/cache/nginx /var/log/nginx
	@sudo mkdir -p $(NGINX_PREFIX)/conf/examples
	@sudo cp $(MODULE_DIR)/config/*.conf $(NGINX_PREFIX)/conf/examples/
	@echo -e "$(GREEN)[INFO]$(NC) Installation completed successfully"

# 创建systemd服务
.PHONY: install-service
install-service:
	@echo -e "$(GREEN)[INFO]$(NC) Creating systemd service..."
	@sudo tee /etc/systemd/system/nginx.service > /dev/null <<'EOF'
[Unit]
Description=The nginx HTTP and reverse proxy server
Documentation=http://nginx.org/en/docs/
After=network.target remote-fs.target nss-lookup.target

[Service]
Type=forking
PIDFile=/var/run/nginx.pid
ExecStartPre=$(NGINX_PREFIX)/sbin/nginx -t
ExecStart=$(NGINX_PREFIX)/sbin/nginx
ExecReload=/bin/kill -s HUP $$MAINPID
KillSignal=SIGQUIT
TimeoutStopSec=5
KillMode=process
PrivateTmp=true

[Install]
WantedBy=multi-user.target
EOF
	@sudo systemctl daemon-reload
	@sudo systemctl enable nginx
	@echo -e "$(GREEN)[INFO]$(NC) Systemd service created and enabled"

# 验证安装
.PHONY: verify
verify:
	@echo -e "$(GREEN)[INFO]$(NC) Verifying installation..."
	@if [[ ! -f "$(NGINX_PREFIX)/sbin/nginx" ]]; then \
		echo -e "$(RED)[ERROR]$(NC) Nginx binary not found"; exit 1; \
	fi
	@if $(NGINX_PREFIX)/sbin/nginx -V 2>&1 | grep -q "json_hash"; then \
		echo -e "$(GREEN)[INFO]$(NC) JSON hash module loaded successfully"; \
	else \
		echo -e "$(YELLOW)[WARN]$(NC) JSON hash module not found in nginx build"; \
	fi
	@if $(NGINX_PREFIX)/sbin/nginx -t; then \
		echo -e "$(GREEN)[INFO]$(NC) Nginx configuration test passed"; \
	else \
		echo -e "$(RED)[ERROR]$(NC) Nginx configuration test failed"; exit 1; \
	fi
	@echo -e "$(GREEN)[INFO]$(NC) Installation verified successfully"

# 运行基础测试
.PHONY: test
test:
	@echo -e "$(GREEN)[INFO]$(NC) Running basic tests..."
	@chmod +x $(MODULE_DIR)/test/test_json_hash_simple.sh
	@$(MODULE_DIR)/test/test_json_hash_simple.sh

# 运行完整测试
.PHONY: test-full
test-full:
	@echo -e "$(GREEN)[INFO]$(NC) Running comprehensive tests..."
	@chmod +x $(MODULE_DIR)/test/comprehensive_test.sh
	@$(MODULE_DIR)/test/comprehensive_test.sh

# 运行性能测试
.PHONY: test-perf
test-perf:
	@echo -e "$(GREEN)[INFO]$(NC) Running performance tests..."
	@chmod +x $(MODULE_DIR)/test/test_json_hash.sh
	@$(MODULE_DIR)/test/test_json_hash.sh

# 代码格式检查
.PHONY: lint
lint:
	@echo -e "$(GREEN)[INFO]$(NC) Running code format check..."
	@if command -v clang-format >/dev/null 2>&1; then \
		clang-format --dry-run --Werror $(MODULE_DIR)/../src/http/modules/ngx_http_upstream_json_hash_module.c; \
		echo -e "$(GREEN)[INFO]$(NC) Code format check passed"; \
	else \
		echo -e "$(YELLOW)[WARN]$(NC) clang-format not found, skipping lint"; \
	fi

# 代码格式化
.PHONY: format
format:
	@echo -e "$(GREEN)[INFO]$(NC) Formatting code..."
	@if command -v clang-format >/dev/null 2>&1; then \
		clang-format -i $(MODULE_DIR)/../src/http/modules/ngx_http_upstream_json_hash_module.c; \
		echo -e "$(GREEN)[INFO]$(NC) Code formatted successfully"; \
	else \
		echo -e "$(YELLOW)[WARN]$(NC) clang-format not found, skipping format"; \
	fi

# 启动nginx
.PHONY: start
start:
	@echo -e "$(GREEN)[INFO]$(NC) Starting nginx..."
	@sudo systemctl start nginx
	@echo -e "$(GREEN)[INFO]$(NC) Nginx started successfully"

# 停止nginx
.PHONY: stop
stop:
	@echo -e "$(GREEN)[INFO]$(NC) Stopping nginx..."
	@sudo systemctl stop nginx
	@echo -e "$(GREEN)[INFO]$(NC) Nginx stopped successfully"

# 重启nginx
.PHONY: restart
restart:
	@echo -e "$(GREEN)[INFO]$(NC) Restarting nginx..."
	@sudo systemctl restart nginx
	@echo -e "$(GREEN)[INFO]$(NC) Nginx restarted successfully"

# 重新加载配置
.PHONY: reload
reload:
	@echo -e "$(GREEN)[INFO]$(NC) Reloading nginx configuration..."
	@sudo systemctl reload nginx
	@echo -e "$(GREEN)[INFO]$(NC) Nginx configuration reloaded successfully"

# 查看nginx状态
.PHONY: status
status:
	@echo -e "$(GREEN)[INFO]$(NC) Nginx status:"
	@sudo systemctl status nginx --no-pager -l

# 查看nginx日志
.PHONY: logs
logs:
	@echo -e "$(GREEN)[INFO]$(NC) Recent nginx logs:"
	@sudo tail -f /var/log/nginx/error.log

# 查看访问日志
.PHONY: access-logs
access-logs:
	@echo -e "$(GREEN)[INFO]$(NC) Recent nginx access logs:"
	@sudo tail -f /var/log/nginx/access.log

# 清理临时文件
.PHONY: clean
clean:
	@echo -e "$(GREEN)[INFO]$(NC) Cleaning temporary files..."
	@rm -rf $(TEMP_DIR)/nginx-$(NGINX_VERSION)*
	@echo -e "$(GREEN)[INFO]$(NC) Cleanup completed"

# 完全卸载
.PHONY: uninstall
uninstall:
	@echo -e "$(YELLOW)[WARN]$(NC) This will completely remove nginx installation"
	@read -p "Are you sure? [y/N] " -n 1 -r; \
	if [[ $$REPLY =~ ^[Yy]$$ ]]; then \
		echo -e "\n$(GREEN)[INFO]$(NC) Uninstalling nginx..."; \
		sudo systemctl stop nginx || true; \
		sudo systemctl disable nginx || true; \
		sudo rm -f /etc/systemd/system/nginx.service; \
		sudo systemctl daemon-reload; \
		sudo rm -rf $(NGINX_PREFIX); \
		sudo rm -rf /var/cache/nginx; \
		sudo rm -rf /var/log/nginx; \
		sudo userdel nginx || true; \
		echo -e "$(GREEN)[INFO]$(NC) Uninstallation completed"; \
	else \
		echo -e "\n$(GREEN)[INFO]$(NC) Uninstallation cancelled"; \
	fi

# 打包发布
.PHONY: package
package:
	@echo -e "$(GREEN)[INFO]$(NC) Creating package..."
	@tar -czf nginx-json-hash-module-$$(date +%Y%m%d).tar.gz \
		--exclude='*.tar.gz' \
		--exclude='.git*' \
		--exclude='*.swp' \
		--exclude='*.tmp' \
		$(MODULE_DIR)
	@echo -e "$(GREEN)[INFO]$(NC) Package created: nginx-json-hash-module-$$(date +%Y%m%d).tar.gz"

# 显示帮助信息
.PHONY: help
help:
	@echo "Nginx JSON Hash Module - Makefile"
	@echo ""
	@echo "Available targets:"
	@echo "  install-deps     - Install system dependencies"
	@echo "  compile          - Compile nginx with JSON hash module"
	@echo "  install          - Install nginx"
	@echo "  install-service  - Create systemd service"
	@echo "  verify           - Verify installation"
	@echo ""
	@echo "Testing:"
	@echo "  test             - Run basic tests"
	@echo "  test-full        - Run comprehensive tests"
	@echo "  test-perf        - Run performance tests"
	@echo "  lint             - Check code format"
	@echo "  format           - Format code"
	@echo ""
	@echo "Service Management:"
	@echo "  start            - Start nginx"
	@echo "  stop             - Stop nginx"
	@echo "  restart          - Restart nginx"
	@echo "  reload           - Reload configuration"
	@echo "  status           - Show nginx status"
	@echo "  logs             - Show error logs"
	@echo "  access-logs      - Show access logs"
	@echo ""
	@echo "Maintenance:"
	@echo "  clean            - Clean temporary files"
	@echo "  uninstall        - Uninstall nginx completely"
	@echo "  package          - Create distribution package"
	@echo ""
	@echo "Variables:"
	@echo "  NGINX_VERSION    - Nginx version (default: 1.20.2)"
	@echo "  NGINX_PREFIX     - Installation prefix (default: /usr/local/nginx)"
	@echo "  TEMP_DIR         - Temporary directory (default: /tmp)"
	@echo ""
	@echo "Example usage:"
	@echo "  make install-deps"
	@echo "  make install"
	@echo "  make install-service"
	@echo "  make test"

# 默认目标
.DEFAULT_GOAL := help 
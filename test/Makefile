# Makefile for nginx upstream json hash module unit tests

CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -g -O0
INCLUDES = -I/usr/include/cjson -I../src/http/modules
LIBS = -lcjson -lm
STRESS_LIBS = -lcjson -lm -lpthread
TEST_SRC = test_ngx_http_upstream_json_hash_module.c
STRESS_SRC = stress_test.c
TEST_BIN = test_json_hash_module
STRESS_BIN = stress_test

# 默认目标
all: test

# 编译测试
compile: $(TEST_BIN) $(STRESS_BIN)

$(TEST_BIN): $(TEST_SRC)
	$(CC) $(CFLAGS) $(INCLUDES) -o $(TEST_BIN) $(TEST_SRC) $(LIBS)

$(STRESS_BIN): $(STRESS_SRC)
	$(CC) $(CFLAGS) $(INCLUDES) -o $(STRESS_BIN) $(STRESS_SRC) $(STRESS_LIBS)

# 运行测试
test: $(TEST_BIN)
	@echo "运行单元测试..."
	@./$(TEST_BIN)

# 运行压力测试
stress: $(STRESS_BIN)
	@echo "运行压力测试..."
	@./$(STRESS_BIN)

# 内存检查测试（需要安装valgrind）
memtest: $(TEST_BIN)
	@echo "使用Valgrind进行内存检查测试..."
	@valgrind --leak-check=full --show-leak-kinds=all --error-exitcode=1 ./$(TEST_BIN)

# 覆盖率测试
coverage: $(TEST_SRC)
	@echo "生成覆盖率报告..."
	$(CC) $(CFLAGS) $(INCLUDES) --coverage -o $(TEST_BIN)_cov $(TEST_SRC) $(LIBS)
	./$(TEST_BIN)_cov
	gcov $(TEST_SRC)
	@echo "覆盖率报告已生成: $(TEST_SRC).gcov"

# 性能测试
perf: $(TEST_BIN)
	@echo "运行性能测试..."
	@time ./$(TEST_BIN)

# 清理
clean:
	rm -f $(TEST_BIN) $(STRESS_BIN) $(TEST_BIN)_cov
	rm -f *.gcno *.gcda *.gcov
	rm -f /tmp/test_body_*

# 安装依赖（Ubuntu/Debian）
install-deps:
	@echo "安装测试依赖..."
	sudo apt-get update
	sudo apt-get install -y libcjson-dev valgrind gcov

# 检查依赖
check-deps:
	@echo "检查依赖库..."
	@pkg-config --exists libcjson && echo "✓ libcjson 已安装" || echo "✗ libcjson 未安装，请运行 make install-deps"
	@which valgrind > /dev/null && echo "✓ valgrind 已安装" || echo "✗ valgrind 未安装（内存测试需要）"
	@which gcov > /dev/null && echo "✓ gcov 已安装" || echo "✗ gcov 未安装（覆盖率测试需要）"

# 帮助
help:
	@echo "可用目标："
	@echo "  all        - 编译并运行测试（默认）"
	@echo "  compile    - 仅编译测试程序"
	@echo "  test       - 运行单元测试"
	@echo "  stress     - 运行压力测试"
	@echo "  memtest    - 使用Valgrind进行内存检查"
	@echo "  coverage   - 生成代码覆盖率报告"
	@echo "  perf       - 运行性能测试"
	@echo "  clean      - 清理生成的文件"
	@echo "  install-deps - 安装测试依赖（Ubuntu/Debian）"
	@echo "  check-deps - 检查依赖是否安装"
	@echo "  help       - 显示此帮助信息"

.PHONY: all compile test stress memtest coverage perf clean install-deps check-deps help 
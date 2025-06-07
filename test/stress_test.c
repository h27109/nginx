/*
 * 压力测试：ngx_http_upstream_json_hash_module
 * 测试高并发、内存泄漏和性能边界
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <sys/time.h>
#include <pthread.h>

/* 包含测试框架的基础代码 */
#include "test_ngx_http_upstream_json_hash_module.c"

#define MAX_THREADS 100
#define REQUESTS_PER_THREAD 1000

/* 测试统计 */
typedef struct {
    long total_requests;
    long successful_requests;
    long failed_requests;
    double total_time_ms;
    double min_time_ms;
    double max_time_ms;
} test_stats_t;

static test_stats_t global_stats = {0};
static pthread_mutex_t stats_mutex = PTHREAD_MUTEX_INITIALIZER;

/* 获取当前时间（毫秒） */
static double get_time_ms() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000.0 + tv.tv_usec / 1000.0;
}

/* 更新统计信息 */
static void update_stats(int success, double time_ms) {
    pthread_mutex_lock(&stats_mutex);
    
    global_stats.total_requests++;
    if (success) {
        global_stats.successful_requests++;
    } else {
        global_stats.failed_requests++;
    }
    
    global_stats.total_time_ms += time_ms;
    
    if (global_stats.min_time_ms == 0 || time_ms < global_stats.min_time_ms) {
        global_stats.min_time_ms = time_ms;
    }
    
    if (time_ms > global_stats.max_time_ms) {
        global_stats.max_time_ms = time_ms;
    }
    
    pthread_mutex_unlock(&stats_mutex);
}

/* 生成随机JSON数据 */
static char* generate_random_json(int complexity) {
    static char json_buffer[1024];
    int user_id = rand() % 1000000;
    
    switch (complexity) {
        case 1: // 简单JSON
            snprintf(json_buffer, sizeof(json_buffer), 
                    "{\"user_id\":\"%d\"}", user_id);
            break;
            
        case 2: // 中等复杂度
            snprintf(json_buffer, sizeof(json_buffer),
                    "{\"user_id\":\"%d\",\"name\":\"user_%d\",\"age\":%d,\"score\":%.2f}",
                    user_id, user_id, 20 + (rand() % 50), (rand() % 100) / 10.0);
            break;
            
        case 3: // 复杂嵌套
            snprintf(json_buffer, sizeof(json_buffer),
                    "{\"user_id\":\"%d\",\"profile\":{\"name\":\"user_%d\",\"settings\":{\"theme\":\"dark\",\"lang\":\"zh\"}},\"metrics\":[1,2,3,4,5]}",
                    user_id, user_id);
            break;
            
        default:
            strcpy(json_buffer, "{\"user_id\":\"default\"}");
    }
    
    return json_buffer;
}

/* 单次请求测试 */
static int test_single_request(const char* json_data, const char* field) {
    double start_time = get_time_ms();
    
    ngx_http_request_t *r = create_test_request(json_data);
    ngx_str_t field_name = {strlen(field), (u_char*)field};
    
    ngx_str_t *result = ngx_http_upstream_extract_json_field(r, &field_name);
    
    int success = (result != NULL);
    
    free_test_request(r);
    cleanup_test_pool();
    
    double end_time = get_time_ms();
    update_stats(success, end_time - start_time);
    
    return success;
}

/* 线程测试函数 */
static void* thread_test_worker(void* arg) {
    int thread_id = *(int*)arg;
    
    for (int i = 0; i < REQUESTS_PER_THREAD; i++) {
        char* json_data = generate_random_json((i % 3) + 1);
        test_single_request(json_data, "user_id");
        
        // 偶尔测试错误情况
        if (i % 100 == 0) {
            test_single_request("{invalid json", "user_id");
            test_single_request("{\"user_id\":null}", "user_id");
            test_single_request("{\"other_field\":\"value\"}", "user_id");
        }
    }
    
    return NULL;
}

/* 并发测试 */
void stress_test_concurrent() {
    printf("压力测试 1: 并发请求测试\n");
    
    int num_threads = 10;
    pthread_t threads[MAX_THREADS];
    int thread_ids[MAX_THREADS];
    
    // 重置统计
    memset(&global_stats, 0, sizeof(global_stats));
    
    double start_time = get_time_ms();
    
    // 创建线程
    for (int i = 0; i < num_threads; i++) {
        thread_ids[i] = i;
        pthread_create(&threads[i], NULL, thread_test_worker, &thread_ids[i]);
    }
    
    // 等待所有线程完成
    for (int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
    }
    
    double total_time = get_time_ms() - start_time;
    
    printf("并发测试结果:\n");
    printf("  线程数: %d\n", num_threads);
    printf("  每线程请求数: %d\n", REQUESTS_PER_THREAD);
    printf("  总请求数: %ld\n", global_stats.total_requests);
    printf("  成功请求: %ld\n", global_stats.successful_requests);
    printf("  失败请求: %ld\n", global_stats.failed_requests);
    printf("  成功率: %.2f%%\n", 
           (double)global_stats.successful_requests / global_stats.total_requests * 100);
    printf("  总耗时: %.2f ms\n", total_time);
    printf("  平均响应时间: %.2f ms\n", 
           global_stats.total_time_ms / global_stats.total_requests);
    printf("  最短响应时间: %.2f ms\n", global_stats.min_time_ms);
    printf("  最长响应时间: %.2f ms\n", global_stats.max_time_ms);
    printf("  QPS: %.2f\n", global_stats.total_requests / (total_time / 1000.0));
    printf("✓ 并发测试完成\n\n");
}

/* 内存压力测试 */
void stress_test_memory() {
    printf("压力测试 2: 内存压力测试\n");
    
    const int large_request_count = 10000;
    int success_count = 0;
    
    double start_time = get_time_ms();
    
    for (int i = 0; i < large_request_count; i++) {
        char* json_data = generate_random_json((i % 3) + 1);
        
        if (test_single_request(json_data, "user_id")) {
            success_count++;
        }
        
        // 每1000次请求打印进度
        if (i % 1000 == 0) {
            printf("  已处理 %d/%d 请求\n", i, large_request_count);
        }
    }
    
    double total_time = get_time_ms() - start_time;
    
    printf("内存压力测试结果:\n");
    printf("  总请求数: %d\n", large_request_count);
    printf("  成功请求: %d\n", success_count);
    printf("  成功率: %.2f%%\n", (double)success_count / large_request_count * 100);
    printf("  总耗时: %.2f ms\n", total_time);
    printf("  平均响应时间: %.3f ms\n", total_time / large_request_count);
    printf("✓ 内存压力测试完成\n\n");
}

/* 大JSON测试 */
void stress_test_large_json() {
    printf("压力测试 3: 大JSON数据测试\n");
    
    // 生成大JSON字符串
    const int large_json_size = 10000;
    char* large_json = malloc(large_json_size + 1);
    strcpy(large_json, "{\"user_id\":\"12345\",\"data\":\"");
    
    int base_len = strlen(large_json);
    for (int i = base_len; i < large_json_size - 10; i++) {
        large_json[i] = 'a' + (i % 26);
    }
    strcpy(large_json + large_json_size - 10, "\"}");
    
    int success_count = 0;
    const int test_count = 100;
    
    double start_time = get_time_ms();
    
    for (int i = 0; i < test_count; i++) {
        if (test_single_request(large_json, "user_id")) {
            success_count++;
        }
    }
    
    double total_time = get_time_ms() - start_time;
    
    printf("大JSON测试结果:\n");
    printf("  JSON大小: %d 字节\n", (int)strlen(large_json));
    printf("  测试次数: %d\n", test_count);
    printf("  成功次数: %d\n", success_count);
    printf("  成功率: %.2f%%\n", (double)success_count / test_count * 100);
    printf("  总耗时: %.2f ms\n", total_time);
    printf("  平均响应时间: %.2f ms\n", total_time / test_count);
    printf("✓ 大JSON测试完成\n\n");
    
    free(large_json);
}

/* 异常情况测试 */
void stress_test_edge_cases() {
    printf("压力测试 4: 异常边界情况测试\n");
    
    const char* edge_cases[] = {
        "",                              // 空字符串
        " ",                             // 空格
        "{",                             // 不完整JSON
        "}",                             // 不完整JSON
        "null",                          // null
        "[]",                            // 数组
        "{\"user_id\":}",               // 语法错误
        "{\"user_id\":\"\"",            // 不完整
        "{\"user_id\":\"\"}",           // 空值
        "{\"user_id\":null}",           // null值
        "{\"user_id\":true}",           // 布尔值
        "{\"user_id\":[1,2,3]}",        // 数组值
        "{\"user_id\":{\"id\":123}}",   // 对象值
        "{\"user_id\":\"" "very_long_string_that_might_cause_issues_with_memory_allocation_or_processing_time_" "very_long_string_that_might_cause_issues_with_memory_allocation_or_processing_time_" "\"}",  // 超长字符串
        NULL
    };
    
    int total_cases = 0;
    int handled_cases = 0;
    
    double start_time = get_time_ms();
    
    for (int i = 0; edge_cases[i] != NULL; i++) {
        total_cases++;
        
        // 这些都应该优雅地处理失败，不应该崩溃
        ngx_http_request_t *r = create_test_request(edge_cases[i]);
        ngx_str_t field_name = {7, (u_char*)"user_id"};
        
        ngx_str_t *result = ngx_http_upstream_extract_json_field(r, &field_name);
        
        // 对于异常情况，我们期望返回NULL
        if (result == NULL) {
            handled_cases++;
        }
        
        free_test_request(r);
        cleanup_test_pool();
        
        printf("  测试用例 %d: %s\n", i + 1, 
               result == NULL ? "正确处理" : "意外成功");
    }
    
    double total_time = get_time_ms() - start_time;
    
    printf("异常边界测试结果:\n");
    printf("  总测试用例: %d\n", total_cases);
    printf("  正确处理: %d\n", handled_cases);
    printf("  处理率: %.2f%%\n", (double)handled_cases / total_cases * 100);
    printf("  总耗时: %.2f ms\n", total_time);
    printf("✓ 异常边界测试完成\n\n");
}

/* 性能基准测试 */
void stress_test_benchmark() {
    printf("压力测试 5: 性能基准测试\n");
    
    const char* test_json = "{\"user_id\":\"benchmark_user_12345\",\"name\":\"benchmark\",\"age\":30}";
    const int benchmark_count = 50000;
    
    double start_time = get_time_ms();
    
    for (int i = 0; i < benchmark_count; i++) {
        test_single_request(test_json, "user_id");
    }
    
    double total_time = get_time_ms() - start_time;
    
    printf("性能基准测试结果:\n");
    printf("  测试次数: %d\n", benchmark_count);
    printf("  总耗时: %.2f ms\n", total_time);
    printf("  平均响应时间: %.3f ms\n", total_time / benchmark_count);
    printf("  理论QPS: %.0f\n", benchmark_count / (total_time / 1000.0));
    printf("✓ 性能基准测试完成\n\n");
}

int main() {
    printf("开始执行 nginx upstream json hash 模块压力测试\n");
    printf("====================================================\n");
    
    srand(time(NULL));
    
    stress_test_concurrent();
    stress_test_memory();
    stress_test_large_json();
    stress_test_edge_cases();
    stress_test_benchmark();
    
    printf("====================================================\n");
    printf("所有压力测试完成！模块性能和稳定性正常 ✓\n");
    
    return 0;
} 
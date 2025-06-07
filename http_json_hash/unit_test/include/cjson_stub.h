#ifndef CJSON_STUB_H
#define CJSON_STUB_H

#include <stddef.h>

/* 模拟cJSON类型 */
typedef enum {
    cJSON_Invalid = 0,
    cJSON_False,
    cJSON_True,
    cJSON_NULL,
    cJSON_Number,
    cJSON_String,
    cJSON_Array,
    cJSON_Object,
    cJSON_Raw
} cJSON_Type;

/* 模拟cJSON结构 */
typedef struct cJSON {
    struct cJSON *next;
    struct cJSON *prev;
    struct cJSON *child;

    cJSON_Type type;

    char *valuestring;
    int valueint;
    double valuedouble;

    char *string;
} cJSON;

/* cJSON函数声明 */
cJSON *cJSON_Parse(const char *value);
cJSON *cJSON_GetObjectItem(const cJSON *object, const char *string);
char *cJSON_GetStringValue(const cJSON *item);
double cJSON_GetNumberValue(const cJSON *item);
int cJSON_IsString(const cJSON *item);
int cJSON_IsNumber(const cJSON *item);
int cJSON_IsBool(const cJSON *item);
int cJSON_IsNull(const cJSON *item);
int cJSON_IsTrue(const cJSON *item);
int cJSON_IsArray(const cJSON *item);
int cJSON_IsObject(const cJSON *item);
char *cJSON_Print(const cJSON *item);
void cJSON_Delete(cJSON *item);
const char *cJSON_GetErrorPtr(void);

#endif /* CJSON_STUB_H */ 
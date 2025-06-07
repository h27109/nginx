#include "cjson_stub.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>

/* 模拟cJSON解析错误 */
static const char *mock_error_ptr = NULL;

/* 模拟函数实现 */
cJSON *cJSON_Parse(const char *value) {
    if (value == NULL) {
        mock_error_ptr = "NULL input";
        return NULL;
    }

    /* 检查是否是有效的JSON字符串 */
    if (value[0] != '{' && value[0] != '[') {
        mock_error_ptr = "Not a JSON object";
        return NULL;
    }

    /* 创建一个模拟的cJSON对象 */
    cJSON *obj = calloc(1, sizeof(cJSON));
    if (obj == NULL) {
        mock_error_ptr = "Memory allocation failed";
        return NULL;
    }

    obj->type = cJSON_Object;
    obj->string = NULL;
    obj->valuestring = NULL;
    obj->valueint = 0;
    obj->valuedouble = 0.0;

    /* 如果是一个简单的键值对，我们可以解析它 */
    if (strstr(value, "\"user_id\":") != NULL) {
        cJSON *child = calloc(1, sizeof(cJSON));
        if (child == NULL) {
            free(obj);
            mock_error_ptr = "Memory allocation failed";
            return NULL;
        }

        child->type = cJSON_String;
        child->string = strdup("user_id");
        
        /* 提取值 */
        const char *start = strstr(value, "\"user_id\":") + 10;
        while (*start && (*start == ' ' || *start == '\t' || *start == '\n' || *start == '\r')) start++;
        
        if (*start == '"') {
            /* 字符串值 */
            start++;
            const char *end = strchr(start, '"');
            if (end) {
                size_t len = end - start;
                child->valuestring = malloc(len + 1);
                if (child->valuestring) {
                    memcpy(child->valuestring, start, len);
                    child->valuestring[len] = '\0';
                }
            }
        } else if (*start >= '0' && *start <= '9') {
            /* 数字值 */
            child->valueint = atoi(start);
            child->valuedouble = atof(start);
            child->type = cJSON_Number;
        } else if (strncmp(start, "true", 4) == 0) {
            /* 布尔值 true */
            child->type = cJSON_True;
        } else if (strncmp(start, "false", 5) == 0) {
            /* 布尔值 false */
            child->type = cJSON_False;
        } else if (strncmp(start, "null", 4) == 0) {
            /* null值 */
            child->type = cJSON_NULL;
        }

        obj->child = child;
    }

    return obj;
}

cJSON *cJSON_GetObjectItem(const cJSON *object, const char *string) {
    if (!object || !string || !object->child) {
        return NULL;
    }

    cJSON *child = object->child;
    while (child) {
        if (child->string && strcmp(child->string, string) == 0) {
            return child;
        }
        child = child->next;
    }

    return NULL;
}

char *cJSON_GetStringValue(const cJSON *item) {
    if (!item || item->type != cJSON_String) {
        return NULL;
    }
    return item->valuestring;
}

double cJSON_GetNumberValue(const cJSON *item) {
    if (!item || item->type != cJSON_Number) {
        return 0.0;
    }
    return item->valuedouble;
}

int cJSON_IsString(const cJSON *item) {
    return (item && item->type == cJSON_String);
}

int cJSON_IsNumber(const cJSON *item) {
    return (item && item->type == cJSON_Number);
}

int cJSON_IsBool(const cJSON *item) {
    return (item && (item->type == cJSON_True || item->type == cJSON_False));
}

int cJSON_IsNull(const cJSON *item) {
    return (item && item->type == cJSON_NULL);
}

int cJSON_IsTrue(const cJSON *item) {
    return (item && item->type == cJSON_True);
}

int cJSON_IsArray(const cJSON *item) {
    return (item && item->type == cJSON_Array);
}

int cJSON_IsObject(const cJSON *item) {
    return (item && item->type == cJSON_Object);
}

char *cJSON_Print(const cJSON *item) {
    if (!item) {
        return NULL;
    }

    /* 为简化起见，我们只返回一个简单的字符串 */
    char *result = NULL;
    
    if (item->type == cJSON_String && item->valuestring) {
        result = malloc(strlen(item->valuestring) + 3);
        if (result) {
            sprintf(result, "\"%s\"", item->valuestring);
        }
    } else if (item->type == cJSON_Number) {
        result = malloc(32);
        if (result) {
            if (item->valuedouble == (double)item->valueint) {
                sprintf(result, "%d", item->valueint);
            } else {
                sprintf(result, "%f", item->valuedouble);
            }
        }
    } else if (item->type == cJSON_True) {
        result = strdup("true");
    } else if (item->type == cJSON_False) {
        result = strdup("false");
    } else if (item->type == cJSON_NULL) {
        result = strdup("null");
    } else if (item->type == cJSON_Object) {
        result = strdup("{}");
    } else if (item->type == cJSON_Array) {
        result = strdup("[]");
    }

    return result;
}

void cJSON_Delete(cJSON *item) {
    if (!item) {
        return;
    }

    /* 释放子节点 */
    cJSON *child = item->child;
    while (child) {
        cJSON *next = child->next;
        cJSON_Delete(child);
        child = next;
    }

    /* 释放字符串 */
    if (item->string) {
        free(item->string);
    }
    if (item->valuestring) {
        free(item->valuestring);
    }

    /* 释放节点本身 */
    free(item);
}

const char *cJSON_GetErrorPtr(void) {
    return mock_error_ptr;
} 
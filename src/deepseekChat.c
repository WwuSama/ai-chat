#include <stdio.h>
#include <stdlib.h>
#include <curl/curl.h>
#include <json-c/json.h>
#include <string.h>
#include <ctype.h>

struct buffer {
    char *data;
    size_t length;
};

// 异常退出函数
static void fatal(const char *message)
{
    fprintf(stderr, "%s\n", message);
    exit(EXIT_FAILURE);
}

// 获取config.json 中的apikey
static void get_apikey(char **apikey)
{
    // 读取 config.json 到json对象
    json_object *json = json_object_from_file("config.json");
    if(json == NULL) {
        fprintf(stderr, "没有找到config.json\n");
        exit(EXIT_FAILURE);
    }

    // 从json对象中取出 deepseek-api 的值
    json_object *key = NULL;
    if ( json_object_object_get_ex(json, "deepseek-api", &key) 
    &&  json_object_is_type(key, json_type_string) ) {
        *apikey = strdup(json_object_get_string(key));
        json_object_put(json);
        return ;
    }

    fprintf(stderr, "没有找到\"deepseek-api\"字段\n");
    exit(EXIT_FAILURE);
}

// 获取config.json 中的模型角色
static void get_systemPrompt(char **system_prompt)
{
    // 读取 config.json 到json对象
    json_object *json = json_object_from_file("config.json");
    if(json == NULL) {
        fprintf(stderr, "没有找到config.json\n");
        exit(EXIT_FAILURE);
    }

    // 从json对象中取出 deepseek-api 的值
    json_object *key = NULL;
    if ( json_object_object_get_ex(json, "system-prompt", &key)
    &&  json_object_is_type(key, json_type_string) ) {
        *system_prompt = strdup(json_object_get_string(key));
        json_object_put(json);
        return ;
    }

    fprintf(stderr, "没有找到\"system-prompt\"字段\n");
    exit(EXIT_FAILURE);
}

// 给message添加字段
static void message_add(struct json_object *message, const char *role, const char *content)
{
    struct json_object *node = json_object_new_object();
    json_object_object_add(node, "role", json_object_new_string(role));
    json_object_object_add(node, "content", json_object_new_string(content));
    json_object_array_add(message, node);
}

// CURLOPT_WRITEFUNCTION 回调函数覆盖
static size_t receive (const char *data, size_t size, size_t count, void *private_data)
{
    struct buffer *buf = (struct buffer *)private_data;
    size_t bytes = size * count;
    char *next = realloc(buf->data, buf->length + bytes + 1);
    if(!next)
        return 0;
    buf->data = next;
    memcpy(buf->data + buf->length, data, bytes);
    buf->length += bytes;
    buf->data[buf->length] = '\0';
    return bytes;
}

static char *apikey;
static char *system_prompt;

int main(void)
{
    get_apikey(&apikey);
    get_systemPrompt(&system_prompt);

    // 初始化libcurl
    CURLcode rc = curl_global_init(CURL_GLOBAL_DEFAULT);
    if(rc != CURLE_OK)
        fatal(curl_easy_strerror(rc));
    
    // 初始化一个easy handle
    CURL *curl = curl_easy_init();
    if(!curl)
        fatal("Cannot create CURL handle.");

    // 构造一个请求头 添加一个字段
    struct curl_slist *headers = curl_slist_append(NULL, "Content-Type: application/json");
    if(!headers)
        fatal("Cannot allocate headers.");

    // 拼接apikey的请求头 需要动态申请内存 然后添加到上一个请求头下面
    char *auth = malloc(sizeof("Authorization: Bearer ") + strlen(apikey));
    sprintf(auth, "Authorization: Bearer %s", apikey);
    struct curl_slist *next = curl_slist_append(headers, auth);
    if(!next)
        fatal("Cannot allocate authorization headers.");
    headers = next;

    // 设置URL
    rc = curl_easy_setopt(curl, CURLOPT_URL, "https://api.deepseek.com/chat/completions");
    if(rc != CURLE_OK)
        fatal(curl_easy_strerror(rc));
    
    // 设置请求头
    rc = curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    if(rc != CURLE_OK)
        fatal(curl_easy_strerror(rc));

    // 设置连接超时时间 10s
    rc = curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    if(rc != CURLE_OK)
        fatal(curl_easy_strerror(rc));

    // 设置超时时间 180s
    rc = curl_easy_setopt(curl, CURLOPT_TIMEOUT, 180L);
    if(rc != CURLE_OK)
        fatal(curl_easy_strerror(rc));

    // 重新映射接收输出
    rc = curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, receive);
    if(rc != CURLE_OK)
        fatal(curl_easy_strerror(rc));

    // 传递给接收输出函数的私有数据
    struct buffer response = {NULL, 0};
    rc = curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    if(rc != CURLE_OK)
        fatal(curl_easy_strerror(rc));

    // 创建message数组
    struct json_object *message = json_object_new_array();
    
    // 先添加一个System message
    message_add(message, "system", system_prompt);

    // 添加用户输入(先写死做测试)
    message_add(message, "user", "你好");

    // 把数组添加到body成员
    struct json_object *body = json_object_new_object();
    json_object_object_add(body, "messages", message);

    // 添加model键值对到body
    json_object_object_add(body, "model", json_object_new_string("deepseek-flash"));
    
    // 添加思考模式到body
    struct json_object *thinking = json_object_new_object();
    json_object_object_add(thinking, "type", json_object_new_string("disabled"));
    json_object_object_add(body, "thinking", thinking);

    // 将json对象转换为字符串
    const char *bodystr = json_object_to_json_string_ext(body, JSON_C_TO_STRING_PLAIN);

    // 提供POST json数据 设置 CURLOPT_POSTFIELDS 会启用 POST
    rc = curl_easy_setopt(curl, CURLOPT_POSTFIELDS, bodystr);
    if(rc != CURLE_OK)
        fatal(curl_easy_strerror(rc));
    
    // curl 发起请求
    rc = curl_easy_perform(curl);
    if (rc != CURLE_OK)
        fatal(curl_easy_strerror(rc));

    // 获取curl接收状态
    long status = 0;
    rc = curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    if(rc == CURLE_OK) {
        if(status < 200 || status >= 300) {
            fprintf(stderr, "\nHTTP status: %ld\n", status);
            return 1;
        }
    }

    struct json_tokener *tok = json_tokener_new();
    if(!tok)
        return 1;

    struct json_object *root = json_tokener_parse_ex(tok, response.data, response.length + 1);
    if( json_tokener_get_error(tok) != json_tokener_success 
    || !json_object_is_type(root, json_type_object) ) {
        fprintf(stderr, "Invalid response JSON: %s\n",
                json_tokener_error_desc(json_tokener_get_error(tok)));
        return 1;
    }
    size_t end = json_tokener_get_parse_end(tok);
    while(end > response.length && isspace(response.data[end]))
        ++end;
    if(end < response.length) {
        fprintf(stderr, "Unexpected data after response JSON.\n");
        return 1;
    }

    const char *resstr = json_object_to_json_string_ext(root, JSON_C_TO_STRING_PRETTY);
    printf("%s\n", resstr);

    struct json_object *choices = NULL, *choice = NULL;
    if( json_object_object_get_ex(root, "choices", &choices) 
    ||  json_object_is_type(choices, json_type_array)) {
        choice = json_object_array_get_idx(choices, 0);
    }

    struct json_object *remsg = NULL;
    json_object_object_get_ex(choice, "message", &remsg);
    struct json_object *recontext = NULL;
    json_object_object_get_ex(remsg, "content", &recontext);
    printf("%s\n", json_object_get_string(recontext));

    json_object_put(body);

    free(auth);
    free(system_prompt);
    free(apikey);

    curl_easy_cleanup(curl);
    curl_global_cleanup();
    return 0;
}
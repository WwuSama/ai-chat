#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include <json-c/json.h>

struct AiChatStruct {
    char *apikey;   // 访问的apikey
    struct curl_slist *headers;
    json_object *messages;
};
static struct AiChatStruct aichat = {0};

struct dataBuffer {
    char *data;
    size_t length;
};
static struct dataBuffer aibuf = {0};

struct StreamBuffer {
    struct dataBuffer cache;
    struct dataBuffer message;
};
static struct StreamBuffer streambuf = {0};


// 错误退出
static void error_exit(char *message)
{
    fprintf(stderr, "%s\n", message);
    exit(EXIT_FAILURE);
}

// 获取config.json 中的apikey
static void Aichat_get_apikey(char **apikey)
{
    json_object *json_config = json_object_from_file("config.json");
    if(!json_config) {
        error_exit("没有找到或无法解析config.json");
    }

    json_object *key_obj = NULL;
    if( json_object_object_get_ex(json_config, "deepseek-api", &key_obj) 
    &&  json_object_is_type(key_obj, json_type_string) ) {
        *apikey = strdup(json_object_get_string(key_obj));
        if(*apikey == NULL) {
            error_exit("Aichat_get_apikey: 字符串分配失败");
        }
        json_object_put(json_config);
        return ;
    }

    error_exit("Aichat_get_apikey: 没有找到或无法解析 \"deepseek-api\" 字段");
}

static void Aichat_create_headers(void)
{
    // 构造请求头 添加 Content-Type: application/json
    aichat.headers = curl_slist_append(aichat.headers, "Content-Type: application/json");
    if(aichat.headers == NULL)
        error_exit("curl请求头构造失败 1");

    // 追加请求头 追加 Accept: application/json
    aichat.headers = curl_slist_append(aichat.headers, "Accept: application/json");
    if(aichat.headers == NULL)
        error_exit("curl请求头构造失败 2");

    // 追加请求头 追加 Authorization: Bearer <TOKEN>
    char *Authorization = malloc(sizeof("Authorization: Bearer ") + strlen(aichat.apikey));
    sprintf(Authorization, "Authorization: Bearer %s", aichat.apikey);
    aichat.headers = curl_slist_append(aichat.headers, Authorization);
    if(aichat.headers == NULL)
        error_exit("curl请求头构造失败 3");
    free(Authorization);
}

// 为message数组添加成员
static void Aichat_message_add(const char *role, const char *content)
{
    if(aichat.messages == NULL)
        aichat.messages = json_object_new_array();

    json_object *node = json_object_new_object();
    json_object_object_add(node, "role", json_object_new_string(role));
    json_object_object_add(node, "content", json_object_new_string(content));
    json_object_array_add(aichat.messages, node);
}

// 从终端获取字符串 返回字符串需要free
static char *Aichat_getline(void)
{
    size_t len = 0, capacity = 128;
    int ch;
    char *str = malloc(capacity);
    if(str == NULL)
        error_exit("输入分配内存失败");

    printf("user:");

    while((ch = getchar()) != EOF && ch != '\n')  {
        if(len == capacity - 1) {
            size_t new_capacity = capacity * 2;
            char *tmp = realloc(str, new_capacity);
            if(tmp == NULL) {
                error_exit("输入分配内存重分配失败");
            }
            str = tmp;
            capacity = new_capacity;
        }

        str[len++] = (char)ch;
    }
    str[len] = '\0';

    if(len == 0) {
        printf("输入为空请重新输入\n\n");
        free(str);
        return NULL;
    }

    return str;
}

static void Aichat_MessageCache(struct dataBuffer *message, const char *data, size_t len)
{
    static size_t capacity = 128;
    if(message->data == NULL) {
        capacity = 128;
        message->data = realloc(message->data, capacity);
        if(message->data == NULL) error_exit("Aichat_MessageCache 内存重分配失败");
    }
  
    if(message->length + len + 1 > capacity) {
        size_t needed = message->length + len + 1;
        while(capacity < needed) capacity *= 2;
        message->data = realloc(message->data, capacity);
        if(message->data == NULL) error_exit("Aichat_MessageCache 内存重分配失败");
    }

    memcpy(message->data + message->length, data, len);
    message->length += len;
    message->data[message->length] = '\0';
}

// 重定向非流式输出函数
static size_t nostream_receive (const void *data, size_t size, size_t count, void *user_data)
{
    size_t bytes = size * count;
    struct dataBuffer *buf = (struct dataBuffer*)user_data;
    char *tmp = realloc(buf->data, buf->length + bytes + 1);
    if(tmp == NULL)
        error_exit("接收字符串分配空间失败");
    buf->data = tmp;

    memcpy(buf->data + buf->length, data, bytes);
    buf->length += bytes;
    buf->data[buf->length] = '\0';
    return bytes;
}

static void auto_pushBuf(struct dataBuffer *buf, const char *star_addr, size_t len)
{
    buf->data = realloc(buf->data, buf->length + len + 1);
    if(buf->data == NULL) error_exit("auto_pushBuf 重新分配内存失败");

    memcpy(buf->data + buf->length, star_addr, len);
    buf->length += len;
    buf->data[buf->length] = '\0';
}

// 流式缓存处理无思考内容
static void cache_programing(struct StreamBuffer *strbuf)
{
    if(strbuf->cache.length < 6) return ;

    json_tokener *tok = json_tokener_new();
    json_object *data = json_tokener_parse_ex(tok, strbuf->cache.data + 6, strbuf->cache.length - 6);
    json_object *choices = NULL, *choice = NULL, *delta = NULL, *content = NULL;
    if( data && json_object_object_get_ex(data, "choices", &choices) 
    &&  json_object_is_type(choices, json_type_array)) {
        choice = json_object_array_get_idx(choices, 0);

        if( json_object_object_get_ex(choice, "delta", &delta) )
        {
            if( json_object_object_get_ex(delta, "role", &content)) {
                fprintf(stdout, "deepseek: ");
                fflush(stdout);
            }
            else if( json_object_object_get_ex(delta, "content", &content)
            &&  json_object_is_type(content, json_type_string)
            &&  json_object_get_string_len(content) > 0) {
                const char *content_ctr = json_object_get_string(content);
                Aichat_MessageCache(&strbuf->message, content_ctr, strlen(content_ctr));
                fprintf(stdout, "%s", content_ctr);
                fflush(stdout);
            }
        }
    }

    json_tokener_free(tok);
    json_object_put(data);
}

// 重定向流式输出函数
static size_t stream_receive (const char *data, size_t size, size_t count, void *user_data)
{
    size_t bytes = size * count, start = 0;
    struct StreamBuffer *stmbuf = (struct StreamBuffer *)user_data;

    for(size_t i = 0; i < bytes; i ++) {
        if(data[i] != '\n') continue;

        auto_pushBuf(&stmbuf->cache, data + start, i - start);
        cache_programing(stmbuf);
        if(stmbuf->cache.data != NULL) {
            free(stmbuf->cache.data);
            stmbuf->cache.data = NULL;
            stmbuf->cache.length = 0;
        }
        start = i + 1;
    }

    auto_pushBuf(&stmbuf->cache, data + start, bytes - start);

    return bytes;
}

// 非流式接收内容并转换成json并处理
static void reponse_processing(void)
{
    json_object *responses = json_tokener_parse(aibuf.data);
    if(responses == NULL || !json_object_is_type(responses, json_type_object))
        error_exit("Response 读取或JSON解析失败");
    
    json_object *responses_choices = NULL;
    if( !json_object_object_get_ex(responses, "choices", &responses_choices)
    ||  !json_object_is_type(responses_choices, json_type_array))
        error_exit("Response 读取 \"choices\" 失败");

    json_object *responses_choices_0 = json_object_array_get_idx(responses_choices, 0);
    json_object *responses_choices_0_message = NULL;
    json_object *responses_choices_0_message_content = NULL;
    char *assistant = NULL;
    if( json_object_object_get_ex(responses_choices_0, "message", &responses_choices_0_message)
    &&  json_object_is_type(responses_choices_0_message, json_type_object)
    &&  json_object_object_get_ex(responses_choices_0_message, "content", &responses_choices_0_message_content)
    &&  json_object_is_type(responses_choices_0_message_content, json_type_string))
    {
        printf("\ndeepseek: %s\n", json_object_get_string(responses_choices_0_message_content));
        Aichat_message_add("assistant", json_object_get_string(responses_choices_0_message_content));
    }

    json_object_put(responses);
}

int main(void)
{
    Aichat_get_apikey(&aichat.apikey);

    // libcurl库初始化
    CURLcode rc = curl_global_init(CURL_GLOBAL_DEFAULT);
    if(rc != CURLE_OK)
        error_exit("libcurl初始化失败");

    // 初始化一个curl句柄
    CURL *curl = curl_easy_init();
    if(curl == NULL)
        error_exit("curl句柄构造失败");

    // 设置URL
    rc = curl_easy_setopt(curl, CURLOPT_URL, "https://api.deepseek.com/chat/completions");
    if(rc != CURLE_OK)
        error_exit("设置URL失败");

    // 构造请求头
    Aichat_create_headers();

    // 设置请求头
    rc = curl_easy_setopt(curl, CURLOPT_HTTPHEADER, aichat.headers);
    if(rc != CURLE_OK)
        error_exit("设置HTTP请求头失败");

    // 重定向输出函数
    rc = curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, stream_receive);
    if(rc != CURLE_OK)
        error_exit("设置curl输出函数失败");

    // 传入重定向输出函数参数
    rc = curl_easy_setopt(curl, CURLOPT_WRITEDATA, &streambuf);
    if(rc != CURLE_OK)
        error_exit("设置curl输出函数参数失败");
    
    json_object *body = json_object_new_object();
    Aichat_message_add("system", "你是用户忠诚的小狗");
    json_object_object_add(body, "model", json_object_new_string("deepseek-flash"));
    json_object_object_add(body, "messages", aichat.messages);
    json_object_object_add(body, "stream", json_object_new_boolean(1));

    json_object *thinking = json_object_new_object();
    json_object_object_add(thinking, "type", json_object_new_string("disabled"));
    json_object_object_add(body, "thinking", thinking);

    while(1) {
        char *userline = Aichat_getline();
        if(userline == NULL) continue;
        Aichat_message_add("user", userline);
        free(userline);

        // 设置请求体
        rc = curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_object_to_json_string(body));
        if(rc != CURLE_OK)
            error_exit("设置HTTP请求体失败");

        // curl发起请求
        rc = curl_easy_perform(curl);
        if(rc != CURLE_OK)
            error_exit("请求失败");

        printf("\n");

        // 根据状态码判断是否异常
        long status = 0;
        rc = curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
        if(rc == CURLE_OK) {
            if(status < 200 || status >= 300) {
                fprintf(stderr, "\nHTTP status: %ld\n", status);
                return 1;
            }
        }

        Aichat_message_add("assistant", streambuf.message.data);
        free(streambuf.message.data);
        streambuf.message.data = NULL;
        streambuf.message.length = 0;
        free(streambuf.cache.data);
        streambuf.cache.data = NULL;
        streambuf.cache.length = 0;

        free(aibuf.data);
        memset(&aibuf, 0, sizeof(aibuf));
    }

    curl_easy_cleanup(curl);
    curl_slist_free_all(aichat.headers);
    json_object_put(body);
    curl_global_cleanup();
    return 0;
}
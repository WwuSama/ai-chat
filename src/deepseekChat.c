#include <stdio.h>
#include <stdlib.h>
#include <curl/curl.h>
#include <json-c/json.h>
#include <string.h>

// 异常退出函数
static void fatal(const char *message)
{
    fprintf(stderr, "%s\n", message);
    exit(EXIT_FAILURE);
}

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

static char *apikey;

int main(void)
{
    get_apikey(&apikey);
    printf("apikey = %s\n", apikey);

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

    

    free(auth);
    free(apikey);

    curl_easy_cleanup(curl);
    curl_global_cleanup();
    return 0;
}
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <curl/curl.h>
#include <json-c/json.h>

struct Buffer {
    char *data;
    size_t len;
};

struct Stream {
    struct Buffer line;    /* 尚未收到换行的字节 */
    struct Buffer event;   /* 一个 SSE 事件中的 data 字段 */
    struct Buffer answer;  /* 拼接后的完整回答，可用于下一轮对话 */
    int done;
    const char *error;
};

static int append(struct Buffer *buf, const char *data, size_t len)
{
    if(buf->len == SIZE_MAX || len > SIZE_MAX - buf->len - 1)
        return 0;
    char *tmp = realloc(buf->data, buf->len + len + 1);
    if(!tmp)
        return 0;
    buf->data = tmp;
    memcpy(buf->data + buf->len, data, len);
    buf->len += len;
    buf->data[buf->len] = '\0';
    return 1;
}

static void clear(struct Buffer *buf)
{
    buf->len = 0;
    if(buf->data)
        buf->data[0] = '\0';
}

/* 这里只解析已经收齐的 SSE 事件，不直接解析 curl 收到的网络片段。 */
static int process_event(struct Stream *stream)
{
    struct Buffer *event = &stream->event;
    if(!event->len || stream->done)
        return 1;

    /* 每个 data 字段后追加过换行，分发时去掉最后一个。 */
    event->data[--event->len] = '\0';
    if(strcmp(event->data, "[DONE]") == 0) {
        stream->done = 1;
        return 1;
    }

    json_object *root = json_tokener_parse(event->data);
    if(!root || !json_object_is_type(root, json_type_object)) {
        if(root)
            json_object_put(root);
        stream->error = "流式事件 JSON 解析失败";
        return 0;
    }

    int ok = 1;
    json_object *error = NULL, *choices = NULL, *choice = NULL;
    json_object *delta = NULL, *content = NULL;
    if(json_object_object_get_ex(root, "error", &error)) {
        fprintf(stderr, "\nAPI error: %s\n", json_object_to_json_string(error));
        stream->error = "API 返回错误";
        ok = 0;
    } else if(json_object_object_get_ex(root, "choices", &choices)
           && json_object_is_type(choices, json_type_array)
           && (choice = json_object_array_get_idx(choices, 0)) != NULL
           && json_object_object_get_ex(choice, "delta", &delta)
           && json_object_object_get_ex(delta, "content", &content)
           && json_object_is_type(content, json_type_string)) {
        const char *text = json_object_get_string(content);
        size_t len = (size_t)json_object_get_string_len(content);
        if(!append(&stream->answer, text, len)) {
            stream->error = "完整回答分配内存失败";
            ok = 0;
        } else if(fwrite(text, 1, len, stdout) != len || fflush(stdout) == EOF) {
            stream->error = "终端输出失败";
            ok = 0;
        }
    }
    /* role、空 delta 和 usage 等事件没有正文，直接跳过。 */
    json_object_put(root);
    return ok;
}

static int process_line(struct Stream *stream)
{
    struct Buffer *line = &stream->line;
    if(line->len && line->data[line->len - 1] == '\r')
        line->data[--line->len] = '\0';

    if(line->len == 0) {
        if(!process_event(stream))
            return 0;
        clear(&stream->event);
    } else if(line->len >= 5 && memcmp(line->data, "data:", 5) == 0) {
        size_t offset = 5;
        if(line->len > offset && line->data[offset] == ' ')
            offset++;
        if(!append(&stream->event, line->data + offset, line->len - offset)
        || !append(&stream->event, "\n", 1))
            return 0;
    }
    /* 忽略 SSE 注释（心跳）、event、id 等其他字段。 */
    clear(line);
    return 1;
}

static size_t receive(char *data, size_t size, size_t count, void *user_data)
{
    struct Stream *stream = user_data;
    if(size && count > SIZE_MAX / size)
        return 0;
    size_t bytes = size * count;
    size_t start = 0;

    /* 一次回调可能包含半条事件、多条事件，甚至半个中文字符。 */
    for(size_t i = 0; i < bytes; i++) {
        if(data[i] != '\n')
            continue;
        if(!append(&stream->line, data + start, i - start)
        || !process_line(stream))
            goto failed;
        start = i + 1;
    }
    if(!append(&stream->line, data + start, bytes - start))
        goto failed;
    return bytes;

failed:
    if(!stream->error)
        stream->error = "流式缓冲区分配内存失败";
    return 0; /* 通知 curl 停止请求 */
}

int main(int argc, char **argv)
{
    int result = EXIT_FAILURE;
    CURL *curl = NULL;
    struct curl_slist *headers = NULL;
    struct Stream stream = {0};
    json_object *config = NULL, *body = NULL, *key = NULL;
    char *authorization = NULL, *input = NULL;
    size_t capacity = 0;

    if(curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
        fprintf(stderr, "libcurl 初始化失败\n");
        return EXIT_FAILURE;
    }

    config = json_object_from_file("config.json");
    if(!config || !json_object_object_get_ex(config, "deepseek-api", &key)
    || !json_object_is_type(key, json_type_string)
    || json_object_get_string_len(key) == 0) {
        fprintf(stderr, "请在当前目录的 config.json 中配置 deepseek-api\n");
        goto cleanup;
    }

    const char *prompt = argc > 1 ? argv[1] : NULL;
    if(!prompt) {
        printf("user:");
        fflush(stdout);
        ssize_t len = getline(&input, &capacity, stdin);
        if(len <= 0)
            goto cleanup;
        input[strcspn(input, "\r\n")] = '\0';
        prompt = input;
    }
    if(!*prompt) {
        fprintf(stderr, "请输入非空问题\n");
        goto cleanup;
    }

    const char *apikey = json_object_get_string(key);
    authorization = malloc(strlen(apikey) + sizeof("Authorization: Bearer "));
    if(!authorization)
        goto cleanup;
    sprintf(authorization, "Authorization: Bearer %s", apikey);
    const char *fields[] = {
        "Content-Type: application/json", "Accept: text/event-stream", authorization
    };
    for(size_t i = 0; i < sizeof(fields) / sizeof(fields[0]); i++) {
        struct curl_slist *tmp = curl_slist_append(headers, fields[i]);
        if(!tmp)
            goto cleanup;
        headers = tmp;
    }

    body = json_object_new_object();
    json_object *messages = json_object_new_array();
    json_object *message = json_object_new_object();
    json_object_object_add(message, "role", json_object_new_string("user"));
    json_object_object_add(message, "content", json_object_new_string(prompt));
    json_object_array_add(messages, message);
    json_object_object_add(body, "model", json_object_new_string("deepseek-flash"));
    json_object_object_add(body, "messages", messages);
    json_object_object_add(body, "stream", json_object_new_boolean(1));
    json_object *thinking = json_object_new_object();
    json_object_object_add(thinking, "type", json_object_new_string("disabled"));
    json_object_object_add(body, "thinking", thinking);

    curl = curl_easy_init();
    if(!curl)
        goto cleanup;
    CURLcode rc;
#define SETOPT(option, value) do { \
    rc = curl_easy_setopt(curl, option, value); \
    if(rc != CURLE_OK) { \
        fprintf(stderr, "curl 配置失败: %s\n", curl_easy_strerror(rc)); \
        goto cleanup; \
    } \
} while(0)
    SETOPT(CURLOPT_URL, "https://api.deepseek.com/chat/completions");
    SETOPT(CURLOPT_HTTPHEADER, headers);
    SETOPT(CURLOPT_POSTFIELDS, json_object_to_json_string(body));
    SETOPT(CURLOPT_WRITEFUNCTION, receive);
    SETOPT(CURLOPT_WRITEDATA, &stream);
    SETOPT(CURLOPT_FAILONERROR, 1L);
    SETOPT(CURLOPT_CONNECTTIMEOUT, 30L);
#undef SETOPT

    printf("deepseek: ");
    fflush(stdout);
    rc = curl_easy_perform(curl);
    putchar('\n');
    if(rc != CURLE_OK) {
        long status = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
        fprintf(stderr, "请求失败 (HTTP %ld): %s\n", status,
                stream.error ? stream.error : curl_easy_strerror(rc));
        goto cleanup;
    }
    if(!stream.done) {
        fprintf(stderr, "响应提前结束，未收到 [DONE]，已显示的内容可能不完整\n");
        goto cleanup;
    }
    result = EXIT_SUCCESS;

cleanup:
    curl_easy_cleanup(curl);
    curl_slist_free_all(headers);
    if(body)
        json_object_put(body);
    if(config)
        json_object_put(config);
    free(authorization);
    free(input);
    free(stream.line.data);
    free(stream.event.data);
    free(stream.answer.data);
    curl_global_cleanup();
    return result;
}

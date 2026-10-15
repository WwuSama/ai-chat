#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#include <curl/curl.h>
#include <json-c/json.h>

/* 本例面向 Linux / WSL。非流式响应：先收集完整 JSON，再解析。 */
#define RESPONSE_LIMIT (8u * 1024u * 1024u)

struct buffer {
    char *data;
    size_t length;
};

static void fatal(const char *message)
{
    fprintf(stderr, "%s\n", message);
    exit(EXIT_FAILURE);
}

static struct json_object *checked(struct json_object *obj)
{
    if (!obj)
        fatal("JSON allocation failed.");
    return obj;
}

/* add 成功后，父对象接管子对象的引用。 */
static void field(struct json_object *obj, const char *key,
                  struct json_object *value)
{
    if (json_object_object_add(obj, key, checked(value)) != 0)
        fatal("Cannot add JSON field.");
}

static void append(struct json_object *array, struct json_object *value)
{
    if (json_object_array_add(array, checked(value)) != 0)
        fatal("Cannot append JSON message.");
}

static struct json_object *message(const char *role, const char *content)
{
    struct json_object *obj = checked(json_object_new_object());
    field(obj, "role", json_object_new_string(role));
    field(obj, "content", json_object_new_string(content));
    return obj;
}

/* 回调不是每次返回一份 JSON，不能在这里直接解析。 */
static size_t receive(char *data, size_t size, size_t count, void *userdata)
{
    struct buffer *buf = userdata;
    if (size && count > SIZE_MAX / size)
        return 0;
    size_t bytes = size * count;
    if (!bytes)
        return 0;
    if (bytes > RESPONSE_LIMIT - buf->length) {
        fprintf(stderr, "Response exceeds 8 MiB limit.\n");
        return 0;
    }
    char *next = realloc(buf->data, buf->length + bytes + 1);
    if (!next)
        return 0;
    buf->data = next;
    memcpy(buf->data + buf->length, data, bytes);
    buf->length += bytes;
    buf->data[buf->length] = '\0';
    return bytes;
}

/* 返回一个自己拥有引用的 assistant message；失败返回 NULL。 */
static struct json_object *ask(CURL *curl, struct json_object *history,
                                int thinking)
{
    struct json_object *request = checked(json_object_new_object());
    struct json_object *mode = checked(json_object_new_object());
    field(request, "model", json_object_new_string("deepseek-flash"));
    /* history 还要继续使用，因此增加引用再交给 request。 */
    field(request, "messages", json_object_get(history));
    field(mode, "type", json_object_new_string(thinking ? "enabled" : "disabled"));
    field(request, "thinking", mode);
    if (thinking)
        field(request, "reasoning_effort", json_object_new_string("high"));
    field(request, "stream", json_object_new_boolean(0));
    field(request, "max_tokens", json_object_new_int(4096));

    struct buffer response = {NULL, 0};
    struct json_object *root = NULL;
    struct json_object *answer = NULL;
    struct json_tokener *tok = NULL;
    CURLcode rc;
    long status = 0;
    const char *body = json_object_to_json_string_ext(request, JSON_C_TO_STRING_PLAIN);
    if (!body)
        goto cleanup;

    rc = curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
    if (rc != CURLE_OK)
        goto curl_error;
    rc = curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    if (rc != CURLE_OK)
        goto curl_error;
    fprintf(stderr, "Waiting for response...\n");
    rc = curl_easy_perform(curl);
    if (rc != CURLE_OK)
        goto curl_error;
    rc = curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    if (rc != CURLE_OK)
        goto curl_error;
    if (status < 200 || status >= 300) {
        fprintf(stderr, "HTTP %ld\n%s\n", status,
                response.data ? response.data : "(empty response)");
        goto cleanup;
    }
    if (!response.length) {
        fprintf(stderr, "Empty response body.\n");
        goto cleanup;
    }

    tok = json_tokener_new();
    if (!tok)
        goto cleanup;
    /* 长度包含末尾 NUL，便于检测截断的响应。 */
    root = json_tokener_parse_ex(tok, response.data, (int)response.length + 1);
    if (json_tokener_get_error(tok) != json_tokener_success ||
        !json_object_is_type(root, json_type_object)) {
        fprintf(stderr, "Invalid response JSON: %s\n",
                json_tokener_error_desc(json_tokener_get_error(tok)));
        goto cleanup;
    }
    size_t end = json_tokener_get_parse_end(tok);
    while (end < response.length && isspace((unsigned char)response.data[end]))
        ++end;
    if (end < response.length) {
        fprintf(stderr, "Unexpected data after response JSON.\n");
        goto cleanup;
    }

    struct json_object *choices = NULL, *choice = NULL;
    struct json_object *msg = NULL, *content = NULL, *role = NULL;
    if (!json_object_object_get_ex(root, "choices", &choices) ||
        !json_object_is_type(choices, json_type_array) ||
        json_object_array_length(choices) == 0)
        goto invalid_response;
    choice = json_object_array_get_idx(choices, 0);
    if (!json_object_is_type(choice, json_type_object) ||
        !json_object_object_get_ex(choice, "message", &msg) ||
        !json_object_is_type(msg, json_type_object) ||
        !json_object_object_get_ex(msg, "role", &role) ||
        !json_object_is_type(role, json_type_string) ||
        strcmp(json_object_get_string(role), "assistant") != 0 ||
        !json_object_object_get_ex(msg, "content", &content) ||
        !json_object_is_type(content, json_type_string))
        goto invalid_response;

    printf("\nAssistant: %s\n\n", json_object_get_string(content));
    struct json_object *reason = NULL;
    if (json_object_object_get_ex(choice, "finish_reason", &reason) &&
        json_object_is_type(reason, json_type_string) &&
        strcmp(json_object_get_string(reason), "length") == 0)
        fprintf(stderr, "Output reached token limit; answer may be incomplete.\n");
    /* 保留完整 message，包括可能存在的 reasoning_content。 */
    answer = json_object_get(msg);
    goto cleanup;

invalid_response:
    fprintf(stderr, "Missing assistant text in response:\n%s\n", response.data);
    goto cleanup;
curl_error:
    fprintf(stderr, "Transfer error: %s\n", curl_easy_strerror(rc));
cleanup:
    /* 在 request 和栈上 response 失效前，清除句柄里的借用指针。 */
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, NULL);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, NULL);
    if (tok)
        json_tokener_free(tok);
    if (root)
        json_object_put(root);
    json_object_put(request);
    free(response.data);
    return answer;
}

int main(int argc, char **argv)
{
    int thinking = argc == 2 && strcmp(argv[1], "--thinking") == 0;
    if (argc > 2 || (argc == 2 && !thinking)) {
        fprintf(stderr, "Usage: %s [--thinking]\n", argv[0]);
        return 1;
    }
    const char *key = getenv("DEEPSEEK_API_KEY");
    if (!key || !*key)
        fatal("Set DEEPSEEK_API_KEY first.");
    if (strlen(key) > 4096 || strchr(key, '\r') || strchr(key, '\n'))
        fatal("Invalid API key format.");
    char auth[sizeof("Authorization: Bearer ") + 4096];
    snprintf(auth, sizeof(auth), "Authorization: Bearer %s", key);

    CURLcode rc = curl_global_init(CURL_GLOBAL_DEFAULT);
    if (rc != CURLE_OK)
        fatal(curl_easy_strerror(rc));
    CURL *curl = curl_easy_init();
    if (!curl)
        fatal("Cannot create CURL handle.");
    struct curl_slist *headers = curl_slist_append(NULL, "Content-Type: application/json");
    if (!headers)
        fatal("Cannot allocate headers.");
    struct curl_slist *next = curl_slist_append(headers, auth);
    if (!next)
        fatal("Cannot allocate authorization header.");
    headers = next;

#define SET(option, value) do { \
    rc = curl_easy_setopt(curl, option, value); \
    if (rc != CURLE_OK) fatal(curl_easy_strerror(rc)); \
} while (0)
    SET(CURLOPT_URL, "https://api.deepseek.com/chat/completions");
    SET(CURLOPT_HTTPHEADER, headers);
    SET(CURLOPT_CONNECTTIMEOUT, 10L);
    SET(CURLOPT_TIMEOUT, 180L);
    SET(CURLOPT_WRITEFUNCTION, receive);

    const char *system_prompt = "你是一个语言犀利、不耐烦的虚构恶霸角色，请简短回答。";
    struct json_object *history = checked(json_object_new_array());
    append(history, message("system", system_prompt));
    puts("DeepSeek terminal: /quit to exit, /clear to reset history.");
    printf("Thinking: %s\n", thinking ? "enabled (high)" : "disabled");
    char *line = NULL;
    size_t capacity = 0;
    for (;;) {
        printf("You: ");
        fflush(stdout);
        ssize_t length = getline(&line, &capacity, stdin);
        if (length < 0)
            break;
        while (length > 0 && (line[length - 1] == '\n' || line[length - 1] == '\r'))
            line[--length] = '\0';
        if (length == 0)
            continue;
        if (strcmp(line, "/quit") == 0)
            break;
        if (strcmp(line, "/clear") == 0) {
            json_object_put(history);
            history = checked(json_object_new_array());
            append(history, message("system", system_prompt));
            puts("History cleared.");
            continue;
        }
        if (length > 16384) {
            fprintf(stderr, "Input exceeds 16 KiB limit.\n");
            continue;
        }
        size_t previous = json_object_array_length(history);
        append(history, message("user", line));
        struct json_object *answer = ask(curl, history, thinking);
        if (answer)
            append(history, answer);
        else {
            /* 失败的这一轮不进入历史；用户可重新输入。 */
            json_object_array_del_idx(history, previous, 1);
            fprintf(stderr, "This turn was not saved.\n");
        }
    }
    int failed = ferror(stdin);
    if (failed)
        fprintf(stderr, "Input read failed.\n");
    free(line);
    json_object_put(history);
    curl_easy_cleanup(curl);
    curl_slist_free_all(headers);
    curl_global_cleanup();
    return failed ? 1 : 0;
}

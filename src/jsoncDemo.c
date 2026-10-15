#include <stdio.h>
#include <json-c/json.h>

int Demo_1(void)
{
    const char *text = "{\"name\":\"张三\",\"age\":20}";

    struct json_object *root = json_tokener_parse(text);
    if(root == NULL) {
        fprintf(stderr, "JSON 解析失败\n");
        return 1;
    }

    struct json_object *ptr = NULL;

    if(json_object_object_get_ex(root, "name", &ptr) && json_object_is_type(ptr, json_type_string)) {
        printf("姓名: %s\n", json_object_get_string(ptr));
    }

    if(json_object_object_get_ex(root, "age", &ptr) && json_object_is_type(ptr, json_type_int)) {
        printf("年龄: %d\n", json_object_get_int(ptr));
    }

    json_object_put(root);

    return 0;
}

int Demo_2(void)
{
    struct json_object *person = json_object_new_object();
    if(person == NULL)
        return 1;

    json_object_object_add(person, "name", json_object_new_string("张三"));

    json_object_object_add(person, "age", json_object_new_int(20));

    int rc = json_object_to_file_ext("person.json", person, JSON_C_TO_STRING_PRETTY);

    if(rc != 0) {
        fprintf(stderr, "写入 JSON 文件失败\n");
        json_object_put(person);
        return 1;
    }

    json_object_put(person);

    return 0;

}

int Demo_3(void)
{
    struct json_object *root = json_object_from_file("person.json");

    if(root == NULL) {
        fprintf(stderr, "读取或解析 JSON 文件失败\n");
        return 1;
    }

    struct json_object *ptr = NULL;

    if(json_object_object_get_ex(root, "name", &ptr) && json_object_is_type(ptr, json_type_string)) {
        printf("姓名: %s\n", json_object_get_string(ptr));
    }

    if(json_object_object_get_ex(root, "age", &ptr) && json_object_is_type(ptr, json_type_int)) {
        printf("年龄: %d\n", json_object_get_int(ptr));
    }

    json_object_put(root);

    return 0;
}

int main(int argc, char **argv)
{   
    if(argc < 2) {
        fprintf(stderr, "输入参数太少\n");
        return 0;
    }

    int num = atoi(argv[1]);

    switch (num)
    {
    case 1:
        return Demo_1();
        break;

    case 2:
        return Demo_2();
        break;

    case 3:
        return Demo_3();
        break;
    
    default:
        fprintf(stderr, "参数错误\n");
        break;
    }

    return 0;
}
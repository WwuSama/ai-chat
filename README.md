# ai_chat

一个使用 C 语言、libcurl 和 json-c 的学习项目，包含 DeepSeek 命令行流式聊天、HTTP 请求示例和 JSON 操作示例。

## 当前功能

- **aiChat**：从终端输入问题，调用 DeepSeek 接口并实时输出回答；在当前进程内保存多轮对话历史。
- **curlDemo**：请求 `https://example.com`，输出响应内容、curl 调试信息和 HTTP 状态码。
- **jsoncDemo**：演示 JSON 解析、对象和数组构造，以及 JSON 文件读写。

## 目录结构

```text
ai_chat/
├── CMakeLists.txt
├── README.md
├── .gitignore
└── src/
    ├── aiChat.c
    ├── curlDemo.c
    └── jsoncDemo.c
```

本地使用时还会涉及以下文件和目录：

- `config.json`：聊天程序的 API Key 配置，已被 Git 忽略。
- `build/`：CMake 构建目录，已被 Git 忽略。
- `bin/`：可执行文件输出目录，已被 Git 忽略。
- `person.json`、`array.json`、`persons.json`：JSON 示例生成或读取的文件。其中 `array.json` 和 `persons.json` 已被 Git 忽略。

## 编译依赖

需要 C 编译器、pkg-config、libcurl 开发库和 json-c 开发库。使用 CMake 时，版本需要为 **3.23 或以上**。

Ubuntu / Debian 可安装：

```bash
sudo apt update
sudo apt install build-essential cmake pkg-config libcurl4-openssl-dev libjson-c-dev
```

### 直接编译

在仓库根目录执行：

```bash
mkdir -p bin
cc src/aiChat.c -o bin/aiChat $(pkg-config --cflags --libs libcurl json-c)
cc src/curlDemo.c -o bin/curlDemo $(pkg-config --cflags --libs libcurl)
cc src/jsoncDemo.c -o bin/jsoncDemo $(pkg-config --cflags --libs json-c)
```

上述命令使用编译器默认语言模式；`aiChat.c` 使用了 POSIX 的 `strdup()`。

### 使用 CMake

在仓库根目录执行：

```bash
cmake -S . -B build
cmake --build build
```

构建生成 `aiChat`、`curlDemo` 和 `jsoncDemo` 三个可执行文件，输出到仓库根目录下的 `bin/`。

## 使用 aiChat

在仓库根目录创建 `config.json`，填写自己的 API Key：

```json
{
  "deepseek-api": "YOUR_API_KEY"
}
```

程序按**当前工作目录**查找 `config.json`，因此建议在仓库根目录启动：

```bash
./bin/aiChat
```

看到 `user:` 后输入问题并回车，回答会在 `deepseek:` 后逐步显示。回答结束后可继续输入下一轮问题，使用 **Ctrl+C** 结束程序。

当前请求参数写在 `src/aiChat.c` 中：

| 参数 | 当前代码中的值 |
| --- | --- |
| 接口地址 | `https://api.deepseek.com/chat/completions` |
| 模型 | `deepseek-flash` |
| 流式输出 | `stream: true` |
| 思考参数 | `thinking.type: disabled` |
| 系统提示词 | `你是用户忠诚的小狗` |

这些是代码当前发送的参数；实际调用是否成功取决于服务端支持情况和 API Key 权限。修改这些参数后需要重新编译。

### 流式处理流程

```text
终端输入 → 加入 messages → 发起 HTTP 请求
         → stream_receive 缓存网络分片并按换行拆分
         → cache_programing 解析 JSON、输出并缓存正文
         → 将完整回答加入 messages → 等待下一轮输入
```

`cache` 用于保存尚未收完整的一行，`message` 用于拼接当前回答。当前实现按 `data: ` 前缀占 6 字节、每行一个完整 JSON 的响应格式处理，并提取 `choices[0].delta.content`。代码按 `role` 与正文分别出现在不同块中的约定处理。

对话历史仅保存在内存中，退出后不会持久化。源码保留了非流式接收和解析函数，但当前主流程使用流式模式，没有命令行切换选项。

## 使用示例程序

### curlDemo

```bash
./bin/curlDemo
```

程序访问固定地址 `https://example.com`，需要网络连接，不读取 `config.json`。

### jsoncDemo

通过数字参数选择示例：

| 命令 | 功能 |
| --- | --- |
| `./bin/jsoncDemo 1` | 解析内置 JSON 字符串，输出姓名和年龄 |
| `./bin/jsoncDemo 2` | 创建 JSON 对象并写入 `person.json` |
| `./bin/jsoncDemo 3` | 读取 `person.json` 并输出字段，需要先运行示例 2 |
| `./bin/jsoncDemo 4` | 创建 JSON 数组并写入 `array.json` |
| `./bin/jsoncDemo 5` | 创建包含 `persons` 数组的对象并写入 `persons.json` |

文件路径均相对于当前工作目录，写入示例会覆盖对应文件。

## 当前限制

- 流式解析失败及响应中的错误对象尚未提供完整的错误提示。
- 尚未通过 `[DONE]` 标志检查回答是否完整结束。
- 没有正文时，主流程尚未单独处理空回答。
- 聊天历史持续累积，尚未实现长度限制、重置或持久化。
- EOF 尚未与空输入区分处理；当前请用 Ctrl+C 退出。
- 仓库尚未提供自动化测试。

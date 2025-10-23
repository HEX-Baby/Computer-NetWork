#pragma once

// 引入固定宽度的整数类型，如 uint32_t, int64_t，确保在不同平台上的大小一致。
#include <cstdint>

// 定义消息类型枚举，用于清晰地标识不同消息的用途。
// 使用 enum class 可以避免命名冲突，并提供更强的类型检查。
enum class MessageType : uint32_t {
    // 客户端 -> 服务端：请求登录
    // 消息体: LoginRequestBody
    MSG_LOGIN,

    // 客户端 -> 服务端：发送聊天内容
    // 消息体: ChatRequestBody + 聊天文本
    MSG_CHAT,

    // 客户端 -> 服务端：请求登出
    // 消息体: 无
    MSG_LOGOUT,

    // 服务端 -> 客户端：广播聊天消息
    // 消息体: ChatRequestBody + 聊天文本
    MSG_CHAT_BC, // BC for Broadcast

    // 服务端 -> 客户端：发送系统通知 (如 "Alice 加入了聊天")
    // 消息体: 变长的UTF-8字符串
    MSG_SERVER_INFO,

    // 服务端 -> 客户端：响应登录/操作结果
    // 消息体: 变长的UTF-8字符串描述错误信息
    MSG_ERROR
};

// 定义一些协议中使用的常量
const int USERNAME_MAX_LEN = 32;

// 使用 #pragma pack(push, 1) 指令，确保编译器按1字节对齐。
// 这对于网络编程至关重要，可以防止因编译器内存对齐优化
// 导致结构体在发送端和接收端大小不一致的问题。
#pragma pack(push, 1)

// 通用消息头
// 所有消息都以此结构体开头
struct MessageHeader {
    MessageType msgType; // 消息的类型
    uint32_t bodySize;   // 紧跟在消息头之后的消息体总长度（字节）
};

// 登录请求的消息体
struct LoginRequestBody {
    char username[USERNAME_MAX_LEN]; // 用户名，以'\0'结尾的字符串
};

// 聊天消息的消息体
// 注意：这个结构体后面还紧跟着变长的聊天内容文本。
// bodySize = sizeof(ChatRequestBody) + 聊天文本的长度
struct ChatRequestBody {
    int64_t timestamp;               // 服务器生成或转发的时间戳 (例如 time(0))
    char username[USERNAME_MAX_LEN]; // 发送消息的用户名
};

#pragma pack(pop) // 恢复默认的内存对齐方式
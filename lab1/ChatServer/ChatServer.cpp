#define WIN32_LEAN_AND_MEAN

#include <iostream>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <thread>
#include <vector>
#include <string>
#include <mutex>
#include <algorithm>
#include <ctime>

// 新增的头文件，用于宽字符输出
#include <io.h>
#include <fcntl.h>

#include "protocol.h"

#pragma comment(lib, "ws2_32.lib")

// 用于存储客户端信息的结构体
struct ClientInfo {
    SOCKET socket;
    std::string username; // 仍然使用string(UTF-8)存储，因为网络传输需要
};

// 全局变量
std::vector<ClientInfo> g_clients;
std::mutex g_clients_mutex;

// --- 新增：编码转换函数 (从client.cpp复制而来) ---
// 将 UTF-8 (char*) 字符串转换为 UTF-16 (wstring)
std::wstring Utf8ToWstring(const std::string& str) {
    if (str.empty()) return std::wstring();
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), NULL, 0);
    std::wstring wstrTo(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), &wstrTo[0], size_needed);
    return wstrTo;
}

// 函数声明
void HandleClient(SOCKET clientSocket);
void BroadcastBuffer(const char* buffer, int size, SOCKET excludeSocket = INVALID_SOCKET);
void BroadcastSystemMessage(const std::string& message, SOCKET excludeSocket = INVALID_SOCKET);

int main() {
    // *** 关键改动：设置服务器控制台输出为UTF-16模式 ***
    _setmode(_fileno(stdout), _O_U16TEXT);
    _setmode(_fileno(stderr), _O_U16TEXT);

    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::wcerr << L"WSAStartup 失败, 错误码: " << WSAGetLastError() << std::endl;
        return 1;
    }
    std::wcout << L"服务器启动中..." << std::endl;

    SOCKET listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenSocket == INVALID_SOCKET) {
        std::wcerr << L"创建 Socket 失败, 错误码: " << WSAGetLastError() << std::endl;
        WSACleanup();
        return 1;
    }

    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(8888);

    if (bind(listenSocket, (SOCKADDR*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        std::wcerr << L"绑定失败, 错误码: " << WSAGetLastError() << std::endl;
        closesocket(listenSocket);
        WSACleanup();
        return 1;
    }

    if (listen(listenSocket, SOMAXCONN) == SOCKET_ERROR) {
        std::wcerr << L"监听失败, 错误码: " << WSAGetLastError() << std::endl;
        closesocket(listenSocket);
        WSACleanup();
        return 1;
    }

    std::wcout << L"服务器正在端口 8888 上监听..." << std::endl;

    while (true) {
        SOCKET clientSocket = accept(listenSocket, NULL, NULL);
        if (clientSocket == INVALID_SOCKET) {
            std::wcerr << L"接受连接失败, 错误码: " << WSAGetLastError() << std::endl;
            continue;
        }

        std::wcout << L"检测到新的客户端连接: " << clientSocket << std::endl;

        std::thread clientThread(HandleClient, clientSocket);
        clientThread.detach();
    }

    closesocket(listenSocket);
    WSACleanup();

    return 0;
}

void HandleClient(SOCKET clientSocket) {
    ClientInfo clientInfo;
    clientInfo.socket = clientSocket;
    clientInfo.username = "匿名用户";

    {
        MessageHeader header;
        int bytesRead = recv(clientSocket, (char*)&header, sizeof(header), 0);
        if (bytesRead <= 0 || header.msgType != MessageType::MSG_LOGIN) {
            std::wcerr << L"客户端 " << clientSocket << L" 未发送登录包或连接中断。" << std::endl;
            closesocket(clientSocket);
            return;
        }

        LoginRequestBody loginBody;
        recv(clientSocket, (char*)&loginBody, sizeof(loginBody), MSG_WAITALL);
        loginBody.username[USERNAME_MAX_LEN - 1] = '\0';
        clientInfo.username = loginBody.username;

        {
            std::lock_guard<std::mutex> lock(g_clients_mutex);
            g_clients.push_back(clientInfo);
        }

        // *** 关键改动：使用wcout并转换编码 ***
        std::wstring w_username = Utf8ToWstring(clientInfo.username);
        std::wcout << w_username << L" (" << clientSocket << L") 已登录。" << std::endl;

        std::string welcomeMsg = clientInfo.username + " Join Chat ROOM.";
        BroadcastSystemMessage(welcomeMsg, clientSocket);
    }


    while (true) {
        MessageHeader header;
        int bytesRead = recv(clientSocket, (char*)&header, sizeof(header), 0);
        if (bytesRead <= 0) {
            break;
        }

        std::vector<char> bodyBuffer;
        if (header.bodySize > 0) {
            bodyBuffer.resize(header.bodySize);
            recv(clientSocket, bodyBuffer.data(), header.bodySize, MSG_WAITALL);
        }

        switch (header.msgType) {
        case MessageType::MSG_CHAT: {
            ChatRequestBody* clientChatBody = (ChatRequestBody*)bodyBuffer.data();
            const char* messageText = bodyBuffer.data() + sizeof(ChatRequestBody);

            MessageHeader bc_header;
            bc_header.msgType = MessageType::MSG_CHAT_BC;

            ChatRequestBody bc_chatBody;
            strncpy_s(bc_chatBody.username, clientInfo.username.c_str(), _TRUNCATE);
            bc_chatBody.timestamp = time(0);

            std::string text(messageText);
            bc_header.bodySize = sizeof(ChatRequestBody) + text.length() + 1;

            std::vector<char> bc_buffer(sizeof(MessageHeader) + bc_header.bodySize);
            memcpy(bc_buffer.data(), &bc_header, sizeof(MessageHeader));
            memcpy(bc_buffer.data() + sizeof(MessageHeader), &bc_chatBody, sizeof(ChatRequestBody));
            memcpy(bc_buffer.data() + sizeof(MessageHeader) + sizeof(ChatRequestBody), text.c_str(), text.length() + 1);

            BroadcastBuffer(bc_buffer.data(), bc_buffer.size());
            break;
        }
        case MessageType::MSG_LOGOUT: {
            goto cleanup;
        }
        default:
            std::wcerr << L"收到来自 " << Utf8ToWstring(clientInfo.username) << L" 的未知消息类型。" << std::endl;
            break;
        }
    }

cleanup:
    closesocket(clientSocket);

    {
        std::lock_guard<std::mutex> lock(g_clients_mutex);
        g_clients.erase(
            std::remove_if(g_clients.begin(), g_clients.end(),
                [clientSocket](const ClientInfo& ci) {
                    return ci.socket == clientSocket;
                }),
            g_clients.end());
    }

    // *** 关键改动：使用wcout并转换编码 ***
    std::wstring w_username_dc = Utf8ToWstring(clientInfo.username);
    std::wcout << w_username_dc << L" (" << clientSocket << L") 已断开连接。" << std::endl;

    std::string farewellMsg = clientInfo.username + " Left Chat ROOM.";
    BroadcastSystemMessage(farewellMsg);
}

void BroadcastBuffer(const char* buffer, int size, SOCKET excludeSocket) {
    std::lock_guard<std::mutex> lock(g_clients_mutex);
    for (const auto& client : g_clients) {
        if (client.socket != excludeSocket) {
            send(client.socket, buffer, size, 0);
        }
    }
}

void BroadcastSystemMessage(const std::string& message, SOCKET excludeSocket) {
    MessageHeader header;
    header.msgType = MessageType::MSG_SERVER_INFO;
    header.bodySize = message.length() + 1;

    std::vector<char> buffer(sizeof(MessageHeader) + header.bodySize);
    memcpy(buffer.data(), &header, sizeof(MessageHeader));
    memcpy(buffer.data() + sizeof(MessageHeader), message.c_str(), message.length() + 1);

    BroadcastBuffer(buffer.data(), buffer.size(), excludeSocket);
}
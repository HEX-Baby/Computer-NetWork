#define WIN32_LEAN_AND_MEAN

#include <iostream>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <thread>
#include <string>
#include <vector>
#include <atomic>
#include <ctime>
#include <iomanip>
#include <sstream>

// 为了使用_setmode, wcout等
#include <io.h>
#include <fcntl.h>

#include "protocol.h"

#pragma comment(lib, "ws2_32.lib")

using namespace std;

atomic<bool> g_isConnected(false);

// --- 编码转换函数 ---
// 将 UTF-8 (char*) 字符串转换为 UTF-16 (wstring)
wstring Utf8ToWstring(const string& str) {
    if (str.empty()) return wstring();
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), NULL, 0);
    wstring wstrTo(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), &wstrTo[0], size_needed);
    return wstrTo;
}

// 将 UTF-16 (wstring) 转换为 UTF-8 (string)
string WstringToUtf8(const wstring& wstr) {
    if (wstr.empty()) return string();
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
    string strTo(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &strTo[0], size_needed, NULL, NULL);
    return strTo;
}
// --- 编码转换函数结束 ---


void ReceiveMessages(SOCKET serverSocket);
string FormatTimestamp(int64_t timestamp);

int main() {
    // *** 关键改动: 设置控制台输入输出为UTF-16模式 ***
    _setmode(_fileno(stdout), _O_U16TEXT);
    _setmode(_fileno(stdin), _O_U16TEXT);

    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        wcerr << L"WSAStartup 失败, 错误码: " << WSAGetLastError() << endl;
        return 1;
    }

    SOCKET serverSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (serverSocket == INVALID_SOCKET) {
        wcerr << L"创建 Socket 失败, 错误码: " << WSAGetLastError() << endl;
        WSACleanup();
        return 1;
    }

    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(8888);
    inet_pton(AF_INET, "127.0.0.1", &serverAddr.sin_addr);

    if (connect(serverSocket, (SOCKADDR*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        wcerr << L"连接服务器失败, 错误码: " << WSAGetLastError() << endl;
        closesocket(serverSocket);
        WSACleanup();
        return 1;
    }

    wcout << L"成功连接到服务器。" << endl;
    g_isConnected = true;

    wstring w_username; // 使用wstring
    wcout << L"请输入您的昵称: ";
    getline(wcin, w_username); // 使用wcin

    string username = WstringToUtf8(w_username); // 转换为UTF-8用于网络传输

    if (username.length() >= USERNAME_MAX_LEN) {
        wcerr << L"错误: 昵称过长，请保持在 " << USERNAME_MAX_LEN - 1 << L" 个字符以内。" << endl;
        g_isConnected = false;
    }
    else {
        MessageHeader header;
        header.msgType = MessageType::MSG_LOGIN;
        header.bodySize = sizeof(LoginRequestBody);

        LoginRequestBody loginBody;
        strncpy_s(loginBody.username, username.c_str(), _TRUNCATE);

        send(serverSocket, (const char*)&header, sizeof(header), 0);
        send(serverSocket, (const char*)&loginBody, sizeof(loginBody), 0);
    }

    thread recvThread(ReceiveMessages, serverSocket);
    recvThread.detach();

    wcout << L"现在可以开始聊天了 (输入 'exit' 退出程序):" << endl;
    wstring w_line;
    while (g_isConnected) {
        getline(wcin, w_line); // 使用wcin

        if (!g_isConnected) break;
        if (w_line.empty()) continue;
        if (w_line == L"exit") break;

        string line = WstringToUtf8(w_line); // 转换为UTF-8用于网络传输

        MessageHeader chatHeader;
        chatHeader.msgType = MessageType::MSG_CHAT;

        ChatRequestBody chatBody;
        strncpy_s(chatBody.username, username.c_str(), _TRUNCATE);
        chatBody.timestamp = 0;

        chatHeader.bodySize = sizeof(ChatRequestBody) + line.length() + 1;
        vector<char> buffer(sizeof(MessageHeader) + chatHeader.bodySize);

        memcpy(buffer.data(), &chatHeader, sizeof(MessageHeader));
        memcpy(buffer.data() + sizeof(MessageHeader), &chatBody, sizeof(ChatRequestBody));
        memcpy(buffer.data() + sizeof(MessageHeader) + sizeof(ChatRequestBody), line.c_str(), line.length() + 1);

        if (send(serverSocket, buffer.data(), buffer.size(), 0) == SOCKET_ERROR) {
            wcerr << L"消息发送失败。" << endl;
            break;
        }
    }

    if (g_isConnected) {
        MessageHeader logoutHeader;
        logoutHeader.msgType = MessageType::MSG_LOGOUT;
        logoutHeader.bodySize = 0;
        send(serverSocket, (const char*)&logoutHeader, sizeof(logoutHeader), 0);
    }

    wcout << L"正在断开连接..." << endl;
    g_isConnected = false;
    closesocket(serverSocket);
    WSACleanup();
    Sleep(1000);
    return 0;
}

void ReceiveMessages(SOCKET serverSocket) {
    while (g_isConnected) {
        MessageHeader header;
        int bytesRead = recv(serverSocket, (char*)&header, sizeof(header), 0);

        if (bytesRead <= 0) {
            if (g_isConnected) {
                wcerr << L"\n与服务器的连接已断开。" << endl;
            }
            g_isConnected = false;
            break;
        }

        vector<char> bodyBuffer;
        if (header.bodySize > 0) {
            bodyBuffer.resize(header.bodySize);
            int bodyBytesRead = recv(serverSocket, bodyBuffer.data(), header.bodySize, MSG_WAITALL);
            if (bodyBytesRead <= 0) {
                if (g_isConnected) {
                    wcerr << L"\n接收消息体失败，连接已断开。" << endl;
                }
                g_isConnected = false;
                break;
            }
        }

        switch (header.msgType) {
        case MessageType::MSG_CHAT_BC: {
            ChatRequestBody* chatBody = (ChatRequestBody*)bodyBuffer.data();
            const char* messageText = bodyBuffer.data() + sizeof(ChatRequestBody);

            // 将收到的UTF-8数据转换为wstring用于显示
            wstring w_username = Utf8ToWstring(chatBody->username);
            wstring w_messageText = Utf8ToWstring(messageText);

            wstringstream wss;
            wss << L"\r"
                << Utf8ToWstring(FormatTimestamp(chatBody->timestamp)).c_str()
                << L" [" << w_username << L"]: " << w_messageText << endl;
            wcout << wss.str();
            wcout.flush();
            break;
        }
        case MessageType::MSG_SERVER_INFO: {
            wstring w_message = Utf8ToWstring(bodyBuffer.data());
            wcout << L"\r[系统消息]: " << w_message << endl;
            wcout.flush();
            break;
        }
        case MessageType::MSG_ERROR: {
            wstring w_message = Utf8ToWstring(bodyBuffer.data());
            wcerr << L"\r[服务器错误]: " << w_message << endl;
            wcout.flush();
            break;
        }
        default:
            wcerr << L"\r收到未知的消息类型: " << static_cast<int>(header.msgType) << endl;
            break;
        }
    }
}

// 这个函数无需修改，因为它的输出是string，会被转换
string FormatTimestamp(int64_t timestamp) {
    time_t time = timestamp;
    tm local_tm;
    localtime_s(&local_tm, &time);
    stringstream ss;
    ss << put_time(&local_tm, "%Y-%m-%d %H:%M:%S");
    return ss.str();
}
#include <iostream>
#include <string>
#include <vector>
#include <cstring>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <unordered_map>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <conio.h>
#include <windows.h>
#pragma comment(lib, "ws2_32.lib")
#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#endif
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/select.h>
#include <fcntl.h>
#include <errno.h>
#include <termios.h>
#include <sys/ioctl.h>
#define SOCKET int
#define INVALID_SOCKET -1
#define SOCKET_ERROR -1
#define closesocket close
#endif

// ANSI color codes
#define ANSI_RESET "\033[0m"
#define ANSI_CYAN "\033[36m"
#define ANSI_GREEN "\033[32m"
#define ANSI_YELLOW "\033[33m"
#define ANSI_MAGENTA "\033[35m"
#define ANSI_BLUE "\033[34m"
#define ANSI_BOLD "\033[1m"
#define ANSI_ITALIC "\033[3m"
#define ANSI_UNDERLINE "\033[4m"

// Message struct to track type and metadata
struct Message {
    enum class Type { Sent, Received, System, PrivateSent, PrivateReceived };
    Type type;
    std::string content;
    std::string timestamp;
    std::string sender;

    Message(Type t, const std::string& c, const std::string& ts, const std::string& s = "")
        : type(t), content(c), timestamp(ts), sender(s) {}
};

class ChatClient {
private:
    std::unordered_map<std::string, std::string> userColors;
    std::vector<std::string> availableColors = {
        ANSI_CYAN, ANSI_YELLOW, ANSI_MAGENTA, ANSI_BLUE
    };
    SOCKET clientSocket;
    std::string username;
    std::string room;
    std::vector<Message> messages;
    std::string currentInput;
    bool running;
    int terminalWidth;
    int terminalHeight;
    size_t scrollOffset;
    size_t lastRenderedMessageCount;
    std::string lastInput;

    void showWelcomeAnimation() {
        std::cout << "\033[?25l";
        std::cout << "\033[2J\033[H";
        
        getTerminalSize();
        int centerX = terminalWidth / 2;
        int centerY = terminalHeight / 2;

        std::vector<std::string> asciiArt = {
            "  ____ _           _     ____ _           _   ",
            " / ___| |__   __ _| |_  / ___| |__   __ _| |_ ",
            "| |   | '_ \\ / _` | __|| |   | '_ \\ / _` | __|",
            "| |___| | | | (_| | |_ | |___| | | | (_| | |_ ",
            " \\____|_| |_|\\__,_|\\__| \\____|_| |_|\\__,_|\\__|"
        };

        std::string welcomePrefix = "Welcome to ";
        std::string welcomeRoom = room;
        std::string welcomeInfix = ", ";
        std::string welcomeUser = username + "!";
        
        std::vector<std::string> colors = {ANSI_BLUE, ANSI_GREEN, ANSI_MAGENTA, ANSI_YELLOW, ANSI_CYAN};
        
        for (int i = 0; i < 15; i++) {
            std::cout << "\033[2J\033[H";
            
            for (size_t j = 0; j < asciiArt.size(); j++) {
                moveCursor(centerY - 3 + j, centerX - asciiArt[j].length()/2);
                int colorIndex = (i + j) % colors.size();
                std::cout << colors[colorIndex] << asciiArt[j] << ANSI_RESET;
            }
            
            std::string spinner = "|/-\\";
            moveCursor(centerY + 2, centerX);
            std::cout << spinner[i % spinner.length()];
            
#ifdef _WIN32
            Sleep(100);
#else
            usleep(100000);
#endif
        }
        
        std::string fullWelcome = welcomePrefix + welcomeRoom + welcomeInfix + welcomeUser;
        for (size_t i = 0; i <= fullWelcome.length(); i++) {
            std::string current = fullWelcome.substr(0, i);
            moveCursor(centerY + 3, centerX - fullWelcome.length()/2);
            std::cout << ANSI_YELLOW << current << ANSI_RESET;
            
            if (i < fullWelcome.length()) {
                std::cout << "_";
            }
            
#ifdef _WIN32
            Sleep(50 + rand() % 100);
#else
            usleep(50000 + (rand() % 100000));
#endif
        }
        
        for (int i = 0; i < 5; i++) {
            for (int j = 0; j <= 10; j++) {
                moveCursor(centerY + 3, centerX - fullWelcome.length()/2);
                std::cout << "\033[38;5;" << (255 - j*10) << "m" << fullWelcome << ANSI_RESET;
#ifdef _WIN32
                Sleep(30);
#else
                usleep(30000);
#endif
            }
            
            for (int j = 10; j >= 0; j--) {
                moveCursor(centerY + 3, centerX - fullWelcome.length()/2);
                std::cout << "\033[38;5;" << (255 - j*10) << "m" << fullWelcome << ANSI_RESET;
#ifdef _WIN32
                Sleep(30);
#else
                usleep(30000);
#endif
            }
        }
        
        std::cout << "\033[2J\033[H";
        std::cout << "\033[?25h";
    }

    void sendToServer(const std::string& message) {
        send(clientSocket, message.c_str(), static_cast<int>(message.length()), 0);
    }

    std::string getTimestamp() {
        auto now = std::chrono::system_clock::now();
        std::time_t current_time = std::chrono::system_clock::to_time_t(now);
        std::stringstream ss;
        ss << std::put_time(std::localtime(&current_time), "%Y-%m-%d %H:%M:%S");
        return ss.str();
    }

    void getTerminalSize() {
#ifdef _WIN32
        CONSOLE_SCREEN_BUFFER_INFO csbi;
        GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi);
        terminalWidth = csbi.srWindow.Right - csbi.srWindow.Left + 1;
        terminalHeight = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
#else
        struct winsize w;
        ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
        terminalWidth = w.ws_col;
        terminalHeight = w.ws_row;
#endif
        terminalWidth = std::max(80, terminalWidth);
        terminalHeight = std::max(24, terminalHeight);
    }

    void moveCursor(int row, int col) {
        std::cout << "\033[" << row << ";" << col << "H";
    }

    void clearLine(int row) {
        moveCursor(row, 1);
        std::cout << "\033[K";
    }

    std::string formatMessage(const std::string& content) {
        std::string result;
        size_t pos = 0;
        while (pos < content.length()) {
            if (pos + 1 < content.length() && content[pos] == '*' && content[pos + 1] == '*') {
                size_t end = content.find("**", pos + 2);
                if (end != std::string::npos) {
                    result += ANSI_BOLD + content.substr(pos + 2, end - pos - 2) + ANSI_RESET;
                    pos = end + 2;
                    continue;
                }
            } else if (content[pos] == '*') {
                size_t end = content.find('*', pos + 1);
                if (end != std::string::npos) {
                    result += ANSI_ITALIC + content.substr(pos + 1, end - pos - 1) + ANSI_RESET;
                    pos = end + 1;
                    continue;
                }
            } else if (pos + 1 < content.length() && content[pos] == '_' && content[pos + 1] == '_') {
                size_t end = content.find("__", pos + 2);
                if (end != std::string::npos) {
                    result += ANSI_UNDERLINE + content.substr(pos + 2, end - pos - 2) + ANSI_RESET;
                    pos = end + 2;
                    continue;
                }
            }
            result += content[pos];
            ++pos;
        }
        return result;
    }

    void render() {
        getTerminalSize();
        int messageAreaHeight = terminalHeight - 3;
    
        static int lastWidth = 0;
        if (lastWidth != terminalWidth || lastRenderedMessageCount == 0) {
            clearLine(1);
            std::string header = "Room: " + room + " | User: " + username;
            int headerPos = std::max(1, (terminalWidth - static_cast<int>(header.length())) / 2);
            moveCursor(1, headerPos);
            std::cout << ANSI_BLUE << ANSI_BOLD << header << ANSI_RESET;
            lastWidth = terminalWidth;
        }
    
        size_t maxMessages = std::min(static_cast<size_t>(messageAreaHeight), messages.size());
        size_t startIdx;
        if (scrollOffset == 0) {
            startIdx = messages.size() >= maxMessages ? messages.size() - maxMessages : 0;
        } else {
            startIdx = messages.size() >= maxMessages 
                ? std::max(static_cast<size_t>(0), messages.size() - maxMessages - scrollOffset)
                : 0;
        }
    
        for (int row = 2; row < terminalHeight - 1; ++row) {
            clearLine(row);
        }
    
        auto visibleLength = [](const std::string& s) {
            size_t len = 0;
            bool inEscape = false;
            for (char c : s) {
                if (c == '\033') {
                    inEscape = true;
                } else if (inEscape) {
                    if (c == 'm') {
                        inEscape = false;
                    }
                } else {
                    len++;
                }
            }
            return len;
        };
    
        int row = terminalHeight - 2;
        for (size_t i = std::min(messages.size(), startIdx + maxMessages); i > startIdx && row >= 2; --i) {
            const auto& msg = messages[i - 1];
            std::string displayText;
            std::string content = msg.content;
            if (content.back() == '\n') {
                content.pop_back();
            }
    
            if (msg.type == Message::Type::Sent) {
                displayText = "[" + msg.timestamp + "] " + ANSI_GREEN + "You: " + formatMessage(content) + ANSI_RESET;
                int visibleLen = visibleLength(displayText);
                int pos = std::max(1, terminalWidth - visibleLen - 1);
                moveCursor(row, pos);
                std::cout << displayText;
            } else if (msg.type == Message::Type::Received) {
                std::string userColor = userColors.find(msg.sender) != userColors.end() ? userColors[msg.sender] : ANSI_RESET;
                displayText = "[" + msg.timestamp + "] " + userColor + msg.sender + ": " + formatMessage(content) + ANSI_RESET;
                moveCursor(row, 1);
                std::cout << displayText;
            } else if (msg.type == Message::Type::PrivateSent) {
                displayText = "[" + msg.timestamp + "] " + ANSI_GREEN + "(PM to " + msg.sender + "): " + formatMessage(content) + ANSI_RESET;
                int visibleLen = visibleLength(displayText);
                int pos = std::max(1, terminalWidth - visibleLen - 1);
                moveCursor(row, pos);
                std::cout << displayText;
            } else if (msg.type == Message::Type::PrivateReceived) {
                std::string userColor = userColors.find(msg.sender) != userColors.end() ? userColors[msg.sender] : ANSI_RESET;
                displayText = "[" + msg.timestamp + "] " + userColor + "(PM from " + msg.sender + "): " + formatMessage(content) + ANSI_RESET;
                moveCursor(row, 1);
                std::cout << displayText;
            } else { // System
                displayText = "[" + msg.timestamp + "] " + formatMessage(content);
                int visibleLen = visibleLength(displayText);
                int pos = std::max(1, (terminalWidth - visibleLen) / 2);
                moveCursor(row, pos);
                std::cout << displayText;
            }
            --row;
        }
    
        if (currentInput != lastInput || lastRenderedMessageCount == 0) {
            clearLine(terminalHeight);
            moveCursor(terminalHeight, 1);
            std::cout << ANSI_MAGENTA << "->: " << ANSI_RESET << currentInput;
            lastInput = currentInput;
        }
    
        lastRenderedMessageCount = messages.size();
        std::cout.flush();
    }

public:
    ChatClient(const std::string& serverIP, int port, const std::string& user, const std::string& rm)
    : username(user), room(rm), running(true), terminalWidth(80), terminalHeight(24), scrollOffset(0), lastRenderedMessageCount(0) {
#ifdef _WIN32
        HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
        DWORD dwMode = 0;
        if (GetConsoleMode(hOut, &dwMode)) {
            dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
            if (!SetConsoleMode(hOut, dwMode)) {
                std::cerr << "Warning: Failed to enable ANSI support. Colors may not display.\n";
            }
        } else {
            std::cerr << "Warning: Failed to get console mode. Colors may not display.\n";
        }
#endif

        clientSocket = socket(AF_INET, SOCK_STREAM, 0);
        if (clientSocket == INVALID_SOCKET) {
            throw std::runtime_error("Socket creation failed.");
        }

        sockaddr_in serverAddr{};
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_port = htons(port);
        serverAddr.sin_addr.s_addr = inet_addr(serverIP.c_str());
        if (serverAddr.sin_addr.s_addr == INADDR_NONE) {
            closesocket(clientSocket);
            throw std::runtime_error("Invalid IP address: " + serverIP);
        }

        if (connect(clientSocket, reinterpret_cast<sockaddr*>(&serverAddr), sizeof(serverAddr)) == SOCKET_ERROR) {
            std::string error = "Connection failed: ";
            error += errno ? strerror(errno) : std::to_string(WSAGetLastError());
            closesocket(clientSocket);
            throw std::runtime_error(error);
        }

        std::string initMsg = username + ":" + room;
        sendToServer(initMsg);

        showWelcomeAnimation();

#ifdef _WIN32
        u_long mode = 1;
        if (ioctlsocket(clientSocket, FIONBIO, &mode) == SOCKET_ERROR) {
            closesocket(clientSocket);
            throw std::runtime_error("Failed to set non-blocking mode: " + std::to_string(WSAGetLastError()));
        }
#else
        int flags = fcntl(clientSocket, F_GETFL, 0);
        if (fcntl(clientSocket, F_SETFL, flags | O_NONBLOCK) < 0) {
            closesocket(clientSocket);
            throw std::runtime_error("Failed to set non-blocking mode: " + std::string(strerror(errno)));
        }
#endif

        std::cout << "\033[2J\033[H";
    }    

    ~ChatClient() {
        closesocket(clientSocket);
#ifdef _WIN32
        WSACleanup();
#endif
    }

    void run() {
        std::cout << "\033[?25l";
        render();

        fd_set read_fds;
        struct timeval tv;

#ifndef _WIN32
        struct termios oldt, newt;
        tcgetattr(STDIN_FILENO, &oldt);
        newt = oldt;
        newt.c_lflag &= ~(ICANON | ECHO);
        tcsetattr(STDIN_FILENO, TCSANOW, &newt);
#endif

        while (running) {
            FD_ZERO(&read_fds);
            FD_SET(clientSocket, &read_fds);

            tv.tv_sec = 0;
            tv.tv_usec = 100000;

            int result = select(clientSocket + 1, &read_fds, nullptr, nullptr, &tv);
            if (result == SOCKET_ERROR) {
                std::cerr << "Select failed: " << (errno ? strerror(errno) : std::to_string(WSAGetLastError())) << "\n";
                running = false;
                break;
            }

            if (result > 0 && FD_ISSET(clientSocket, &read_fds)) {
                char buffer[1024];
                memset(buffer, 0, sizeof(buffer));
                int bytes = recv(clientSocket, buffer, sizeof(buffer), 0);
                if (bytes <= 0) {
                    messages.emplace_back(Message::Type::System, "Disconnected from server.", getTimestamp());
                    running = false;
                    render();
                    break;
                }
                std::string received(buffer, bytes);
                std::string sender = "";
                std::string content = received;

                // Handle private messages
                if (received.find("[PM]") == 0) {
                    size_t senderEnd = received.find(':', 4);
                    if (senderEnd != std::string::npos) {
                        sender = received.substr(4, senderEnd - 4);
                        content = received.substr(senderEnd + 1);
                        if (content.back() == '\n') content.pop_back();
                        if (userColors.find(sender) == userColors.end()) {
                            size_t colorIndex = std::hash<std::string>{}(sender) % availableColors.size();
                            userColors[sender] = availableColors[colorIndex];
                        }
                        messages.emplace_back(Message::Type::PrivateReceived, content, getTimestamp(), sender);
                    }
                }
                // Check for join/leave messages
                else if (received.find(" joined ") != std::string::npos || received.find(" left ") != std::string::npos) {
                    size_t colon_pos = received.find(':');
                    if (colon_pos != std::string::npos) {
                        sender = received.substr(0, colon_pos);
                        if (userColors.find(sender) == userColors.end() && sender != username) {
                            size_t colorIndex = std::hash<std::string>{}(sender) % availableColors.size();
                            userColors[sender] = availableColors[colorIndex];
                        }
                    }
                    messages.emplace_back(Message::Type::System, received, getTimestamp());
                }
                // Handle regular messages
                else if (received.find(':') != std::string::npos && 
                         received.find("Members in room") == std::string::npos) {
                    sender = received.substr(0, received.find(':'));
                    content = received.substr(received.find(':') + 1);
                    if (content.back() == '\n') content.pop_back();
                    if (userColors.find(sender) == userColors.end() && sender != username) {
                        size_t colorIndex = std::hash<std::string>{}(sender) % availableColors.size();
                        userColors[sender] = availableColors[colorIndex];
                    }
                    if (sender == username) {
                        messages.emplace_back(Message::Type::Sent, content, getTimestamp(), sender);
                    } else {
                        messages.emplace_back(Message::Type::Received, content, getTimestamp(), sender);
                    }
                }
                else {
                    messages.emplace_back(Message::Type::System, received, getTimestamp());
                }
                scrollOffset = 0;
                render();
            }

#ifdef _WIN32
            if (_kbhit()) {
                char ch = _getch();
                if (ch == '\r') {
                    if (currentInput == "exit") {
                        running = false;
                    } else if (!currentInput.empty()) {
                        if (currentInput[0] == '@') {
                            size_t firstSpace = currentInput.find(' ');
                            if (firstSpace != std::string::npos) {
                                std::string targetUser = currentInput.substr(1, firstSpace - 1);
                                std::string pmContent = currentInput.substr(firstSpace + 1);
                                if (!pmContent.empty()) {
                                    std::string message = "[PM]" + username + ":" + targetUser + ":" + pmContent + "\n";
                                    sendToServer(message);
                                    messages.emplace_back(Message::Type::PrivateSent, pmContent, getTimestamp(), targetUser);
                                    currentInput.clear();
                                    scrollOffset = 0;
                                }
                            }
                        } else {
                            std::string message = username + ": " + currentInput + "\n";
                            sendToServer(message);
                            messages.emplace_back(Message::Type::Sent, currentInput, getTimestamp());
                            currentInput.clear();
                            scrollOffset = 0;
                        }
                    }
                } else if (ch == '\b' && !currentInput.empty()) {
                    currentInput.pop_back();
                } else if (ch == 27) {
                    if (_kbhit()) {
                        char next = _getch();
                        if (next == '[') {
                            if (_kbhit()) {
                                char arrow = _getch();
                                if (arrow == 'A' && messages.size() > static_cast<size_t>(terminalHeight - 3)) {
                                    ++scrollOffset;
                                } else if (arrow == 'B' && scrollOffset > 0) {
                                    --scrollOffset;
                                }
                            }
                        }
                    }
                } else if (ch >= 32 && ch <= 126) {
                    currentInput += ch;
                }
                render();
            }
#else
            char ch;
            if (read(STDIN_FILENO, &ch, 1) > 0) {
                if (ch == '\n') {
                    if (currentInput == "exit") {
                        running = false;
                    } else if (!currentInput.empty()) {
                        if (currentInput[0] == '@') {
                            size_t firstSpace = currentInput.find(' ');
                            if (firstSpace != std::string::npos) {
                                std::string targetUser = currentInput.substr(1, firstSpace - 1);
                                std::string pmContent = currentInput.substr(firstSpace + 1);
                                if (!pmContent.empty()) {
                                    std::string message = "[PM]" + username + ":" + targetUser + ":" + pmContent + "\n";
                                    sendToServer(message);
                                    messages.emplace_back(Message::Type::PrivateSent, pmContent, getTimestamp(), targetUser);
                                    currentInput.clear();
                                    scrollOffset = 0;
                                }
                            }
                        } else {
                            std::string message = username + ": " + currentInput + "\n";
                            sendToServer(message);
                            messages.emplace_back(Message::Type::Sent, currentInput, getTimestamp());
                            currentInput.clear();
                            scrollOffset = 0;
                        }
                    }
                } else if (ch == 127 && !currentInput.empty()) {
                    currentInput.pop_back();
                } else if (ch == 27) {
                    char seq[3];
                    if (read(STDIN_FILENO, &seq[0], 1) > 0 && read(STDIN_FILENO, &seq[1], 1) > 0) {
                        if (seq[0] == '[' && seq[1] == 'A' && messages.size() > static_cast<size_t>(terminalHeight - 3)) {
                            ++scrollOffset;
                        } else if (seq[0] == '[' && seq[1] == 'B' && scrollOffset > 0) {
                            --scrollOffset;
                        }
                    }
                } else if (ch >= 32 && ch <= 126) {
                    currentInput += ch;
                }
                render();
            }
#endif
        }

#ifndef _WIN32
        tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
#endif
        std::cout << "\033[?25h";
        std::cout << "\033[2J\033[H";
    }
};

int main(int argc, char* argv[]) {
    if (argc != 4) {
        std::cerr << "Usage: client <IP Address> <Port> <Room>\n";
        return 1;
    }

#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        std::cerr << "WSAStartup failed: " << WSAGetLastError() << "\n";
        return 1;
    }
#endif

    std::string serverIP = argv[1];
    int port = std::stoi(argv[2]);
    std::string room = argv[3];

    std::string username;
    std::cout << "Enter your name: ";
    std::getline(std::cin, username);
    if (username.empty()) {
        std::cerr << "Username cannot be empty.\n";
        return 1;
    }

    try {
        ChatClient client(serverIP, port, username, room);
        client.run();
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return 1;
    }

    std::cout << "Disconnected from server.\n";
    return 0;
}

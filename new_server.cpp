#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <cstring>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/select.h>
#include <errno.h>
#define SOCKET int
#define INVALID_SOCKET -1
#define SOCKET_ERROR -1
#define closesocket close
#endif

class Client {
public:
    SOCKET socket;
    std::string username;
    std::string room;

    Client(SOCKET s, const std::string& u, const std::string& r)
        : socket(s), username(u), room(r) {}
};

class ChatRoom {
public:
    std::string name;
    std::vector<Client> clients;
    std::vector<std::string> messageHistory;

    ChatRoom(const std::string& n) : name(n) {}
    ChatRoom() {}

    void addClient(const Client& client) {
        clients.push_back(client);
    }

    void removeClient(SOCKET socket) {
        for (auto it = clients.begin(); it != clients.end(); ++it) {
            if (it->socket == socket) {
                clients.erase(it);
                break;
            }
        }
    }

    void broadcast(const std::string& message, SOCKET excludeSocket) const {
        for (const auto& client : clients) {
            if (client.socket != excludeSocket) {
                send(client.socket, message.c_str(), static_cast<int>(message.length()), 0);
            }
        }
    }

    void addMessage(const std::string& message) {
        messageHistory.push_back(message);
    }

    void sendHistory(SOCKET socket) const {
        for (const auto& message : messageHistory) {
            send(socket, message.c_str(), static_cast<int>(message.length()), 0);
        }
    }

    std::string getMemberList() const {
        std::string result = "Members in room " + name + ": ";
        for (size_t i = 0; i < clients.size(); ++i) {
            result += clients[i].username;
            if (i < clients.size() - 1) result += ", ";
        }
        result += "\n";
        return result;
    }
};

class ChatServer {
private:
    SOCKET listeningSocket;
    std::map<std::string, ChatRoom> rooms;
    std::vector<Client> clients;

    void sendToClient(SOCKET socket, const std::string& message) {
        send(socket, message.c_str(), static_cast<int>(message.length()), 0);
    }

    Client* findClientByUsername(const std::string& username) {
        for (auto& client : clients) {
            if (client.username == username) {
                return &client;
            }
        }
        return nullptr;
    }

public:
    ChatServer(int port) {
        listeningSocket = socket(AF_INET, SOCK_STREAM, 0);
        if (listeningSocket == INVALID_SOCKET) {
            throw std::runtime_error("Failed to create listening socket.");
        }

        sockaddr_in serverAddr{};
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_port = htons(port);
        serverAddr.sin_addr.s_addr = INADDR_ANY;

        if (bind(listeningSocket, reinterpret_cast<sockaddr*>(&serverAddr), sizeof(serverAddr)) == SOCKET_ERROR) {
            std::string error = "Bind failed: ";
            error += errno ? strerror(errno) : std::to_string(WSAGetLastError());
            closesocket(listeningSocket);
            throw std::runtime_error(error);
        }

        if (listen(listeningSocket, 10) == SOCKET_ERROR) {
            std::string error = "Listen failed: ";
            error += errno ? strerror(errno) : std::to_string(WSAGetLastError());
            closesocket(listeningSocket);
            throw std::runtime_error(error);
        }
    }

    ~ChatServer() {
        for (const auto& client : clients) {
            closesocket(client.socket);
        }
        closesocket(listeningSocket);
#ifdef _WIN32
        WSACleanup();
#endif
    }

    void run() {
        std::cout << "Server running. Waiting for connections...\n";

        fd_set read_fds;
        struct timeval tv;
        bool serverRunning = true;

        while (serverRunning) {
            FD_ZERO(&read_fds);
            FD_SET(listeningSocket, &read_fds);
            int max_fd = listeningSocket;

            for (const auto& client : clients) {
                FD_SET(client.socket, &read_fds);
                if (client.socket > max_fd) max_fd = client.socket;
            }

            tv.tv_sec = 0;
            tv.tv_usec = 100000;

            int result = select(max_fd + 1, &read_fds, nullptr, nullptr, &tv);
            if (result == SOCKET_ERROR) {
                std::cerr << "Select failed: " << (errno ? strerror(errno) : std::to_string(WSAGetLastError())) << "\n";
                break;
            } else if (result > 0) {
                if (FD_ISSET(listeningSocket, &read_fds)) {
                    sockaddr_in clientAddr{};
                    socklen_t clientSize = sizeof(clientAddr);
                    SOCKET clientSocket = accept(listeningSocket, reinterpret_cast<sockaddr*>(&clientAddr), &clientSize);
                    if (clientSocket == INVALID_SOCKET) {
                        std::cerr << "Accept error.\n";
                        continue;
                    }

                    char buffer[1024];
                    memset(buffer, 0, sizeof(buffer));
                    int bytes = recv(clientSocket, buffer, sizeof(buffer), 0);
                    if (bytes <= 0) {
                        closesocket(clientSocket);
                        continue;
                    }

                    std::string data(buffer, bytes);
                    size_t delim = data.find(':');
                    if (delim == std::string::npos) {
                        closesocket(clientSocket);
                        continue;
                    }

                    std::string username = data.substr(0, delim);
                    std::string roomName = data.substr(delim + 1);

                    Client client(clientSocket, username, roomName);
                    clients.push_back(client);

                    auto it = rooms.find(roomName);
                    if (it == rooms.end()) {
                        it = rooms.emplace(roomName, ChatRoom(roomName)).first;
                    }
                    it->second.addClient(client);

                    std::cout << username << " connected to room " << roomName << " from " << inet_ntoa(clientAddr.sin_addr) << "\n";

                    it->second.sendHistory(clientSocket);
                    sendToClient(clientSocket, it->second.getMemberList());

                    std::string joinMsg = username + " joined room " + roomName + "!\n";
                    it->second.addMessage(joinMsg);
                    it->second.broadcast(joinMsg, clientSocket);
                }

                for (size_t i = 0; i < clients.size(); ++i) {
                    SOCKET clientSocket = clients[i].socket;
                    if (FD_ISSET(clientSocket, &read_fds)) {
                        char buffer[1024];
                        memset(buffer, 0, sizeof(buffer));
                        int bytes = recv(clientSocket, buffer, sizeof(buffer), 0);
                        if (bytes <= 0) {
                            std::string roomName = clients[i].room;
                            std::string username = clients[i].username;
                            std::cout << username << " disconnected from room " << roomName << ".\n";
                            rooms[roomName].removeClient(clientSocket);
                            closesocket(clientSocket);
                            clients.erase(clients.begin() + i);
                            --i;
                            std::string leftMsg = username + " left room " + roomName + "!\n";
                            rooms[roomName].addMessage(leftMsg);
                            rooms[roomName].broadcast(leftMsg, INVALID_SOCKET);
                            if (rooms[roomName].clients.empty()) {
                                rooms.erase(roomName);
                            }
                        } else {
                            std::string message(buffer, bytes);
                            std::string roomName = clients[i].room;
                            std::string username = clients[i].username;

                            if (message.find("[PM]") == 0) {
                                size_t firstColon = message.find(':', 4);
                                size_t secondColon = message.find(':', firstColon + 1);
                                if (firstColon != std::string::npos && secondColon != std::string::npos) {
                                    std::string sender = message.substr(4, firstColon - 4);
                                    std::string targetUser = message.substr(firstColon + 1, secondColon - firstColon - 1);
                                    std::string pmContent = message.substr(secondColon + 1);
                                    Client* target = findClientByUsername(targetUser);
                                    if (target) {
                                        std::string pmMessage = "[PM]" + sender + ":" + pmContent;
                                        sendToClient(target->socket, pmMessage);
                                        sendToClient(clientSocket, pmMessage);
                                        std::cout << "[" << roomName << "] PM from " << sender << " to " << targetUser << ": " << pmContent;
                                    } else {
                                        std::string errorMsg = "User " + targetUser + " not found.\n";
                                        sendToClient(clientSocket, errorMsg);
                                    }
                                }
                            } else {
                                std::cout << "[" << roomName << "] " << message << std::endl;
                                rooms[roomName].addMessage(message);
                                rooms[roomName].broadcast(message, clientSocket);
                            }
                        }
                    }
                }
            }
        }
    }
};

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: server <Port>\n";
        return 1;
    }
    int port = std::stoi(argv[1]);

#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        std::cerr << "WSAStartup failed: " << WSAGetLastError() << "\n";
        return 1;
    }
#endif

    try {
        ChatServer server(port);
        server.run();
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return 1;
    }

    return 0;
}

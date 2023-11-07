//
// Created by azazo1 on 2023/11/5.
//

#ifndef DORMTRANSFERFILECONNECTINGSERVER_SCM_MAIN_H
#define DORMTRANSFERFILECONNECTINGSERVER_SCM_MAIN_H

#include <sys/socket.h>
#include <vector>

#define MSG_FETCH_AVAILABLE_SENDERS 0
#define MSG_REGISTER_SENDER 1
#define MSG_QUERY_SENDER_SERVER_ADDRESS 2

#define SEQUENCE_CODE_LENGTH 5
#define TYPE_CODE_LENGTH 2
#define MAX_FILENAME_LENGTH 100
#define SERVER_PORT 8088
#define MAX_CONNECTION 10


class ConnectionCodeGenerator {
#define MAX_CODE 9999
private:
    int connection_code_cursor = 1000;
public:
    int next();

    ConnectionCodeGenerator() = default;
};

class ConnectingServer {
private:
    int client_fds[MAX_CONNECTION];
    /**
     * IP address of net bytes order, corresponding to client_fds
     * */
    uint32_t ip_address_net[MAX_CONNECTION];
    /**
     * Sender connection code. When client registered as a sender, it gets a connection code.
     * Default invalid code value is 0.
     * */
    int sender_connection_code[MAX_CONNECTION];
    /**
     * Sender server port, corresponding to client_fds. It's valid only when the client registered as a sender
     * */
    ushort sender_server_port[MAX_CONNECTION];
    /**
     * Sender server port, corresponding to client_fds. It's valid only when the client registered as a sender
     * */
    char sender_filenames[MAX_CONNECTION][MAX_FILENAME_LENGTH];
    ConnectionCodeGenerator code_generator;
    int server_sock_fd;
    /**
     * is server can be use
     * */
    bool available = false;
public:
    ConnectingServer();

    ~ConnectingServer();

    void handle_msg(int client_fd, char seq_code_bytes[SEQUENCE_CODE_LENGTH]);

    /**
     * 客户端的接收, 信息收发
     * */
    void handleLoop();

    /**
     * 用来预防过频繁的套接字操作
     * */
    static int send_(int sock_fd, char *buffer, int length, int flags);

    void add_sender(int client_index, char *filename, int port);
};

#endif //DORMTRANSFERFILECONNECTINGSERVER_SCM_MAIN_H

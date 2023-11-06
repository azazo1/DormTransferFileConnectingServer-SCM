#include <Arduino.h>
#include <WiFi.h>
#include "main.h"


#define WIFI_SSID "TP-LINK_8524"
#define WIFI_PWD "asdfghjkl"
using namespace std;

int ConnectionCodeGenerator::next() {
    int rst = connection_code_cursor++;
    if (connection_code_cursor > MAX_CODE) {
        connection_code_cursor = 1000;
    }
    return rst;
}

ConnectingServer::~ConnectingServer() {
    if (available) {
        closesocket(server_sock_fd);
        for (int &i: client_fds) {
            if (i != 0) {
                closesocket(i);
            }
        }
    }
}

int ConnectingServer::send_(int sock_fd, char *buffer, int length, int flags) {
    int rst = send(sock_fd, buffer, length, flags);
    delay(10);
    return rst;
}

ConnectingServer::ConnectingServer() : client_fds{0}, sender_connection_code{0}, sender_server_port{0},
                                       ip_address_net{0} {
    struct sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    // create socket
    if ((server_sock_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
        Serial.println("Server create error");
        return;
    }
    if (bind(server_sock_fd, (const struct sockaddr *) &server_addr, sizeof(server_addr)) < 0) {
        Serial.println("Server bind error");
        return;
    }
    if (listen(server_sock_fd, MAX_CONNECTION) < 0) {
        Serial.println("Server listen error");
        return;
    }
    sender_filenames.reserve(MAX_CONNECTION);
    available = true;
}

void ConnectingServer::handleLoop() {
    fd_set server_fd_set;
    int max_fd = 1; // 最大的一个fd值
    struct timeval my_time{};
    Serial.println("Start handle loop");
    while (available) {
        my_time.tv_sec = 27;
        my_time.tv_usec = 0;
        FD_ZERO(&server_fd_set);

        // add server
        FD_SET(server_sock_fd, &server_fd_set);
        if (max_fd < server_sock_fd) {
            max_fd = server_sock_fd;
        }
        // add client
        for (int client_fd: client_fds) {
            FD_SET(client_fd, &server_fd_set);
            if (max_fd < client_fd) {
                max_fd = client_fd;
            }
        }
        int ret = select(max_fd + 1, &server_fd_set, nullptr, nullptr, &my_time); // 超时 27秒
        if (ret < 0) {
            Serial.println("Select failure");
            continue;
        } else if (ret == 0) {
            Serial.println("Time out");
            continue;
        } else {
            if (FD_ISSET(server_sock_fd, &server_fd_set)) {
                struct sockaddr_in client_address{};
                socklen_t address_len;
                int client_sock_fd = accept(server_sock_fd, (struct sockaddr *) &client_address, &address_len);
                if (client_sock_fd > 0) {
                    int accepted_client_fd = -1;
                    for (int i = 0; i < MAX_CONNECTION; i++) {
                        int &stored_client_fd = client_fds[i];
                        if (stored_client_fd == 0) {
                            accepted_client_fd = stored_client_fd = client_sock_fd;
                            ip_address_net[i] = client_address.sin_addr.s_addr;
                            Serial.printf("New stored_client_fd:[%d] added successfully!\n", client_sock_fd);
                            break;
                        }
                    }
                    if (accepted_client_fd < 0) { // full
                        closesocket(client_sock_fd);
                    }
                }
            }
            for (int i = 0; i < MAX_CONNECTION; i++) { // nb, this has no need to erase ip_address_net when client exit
                int &client_fd = client_fds[i];
                if (client_fd == 0) {
                    continue;
                }
                if (FD_ISSET(client_fd, &server_fd_set)) {
                    char receive_bytes[SEQUENCE_CODE_LENGTH + 1] = {0};
                    int n_read_bytes = read(client_fd, receive_bytes, SEQUENCE_CODE_LENGTH);
                    if (n_read_bytes > 0) {
                        Serial.printf("Receive client_fd[%d]\n", client_fd);
                        handle_msg(client_fd, receive_bytes);
                    } else if (n_read_bytes < 0) {
                        Serial.printf("Receive client_fd[%d]: error, client_fd[%d] exit\n", client_fd, client_fd);
                        closesocket(client_fd);
                        client_fd = 0;
                        FD_CLR(client_fd, &server_fd_set);
                        sender_connection_code[i] = 0;
                    } else { // get empty data, client exit
                        Serial.printf("client_fd[%d] exit\n", client_fd);
                        closesocket(client_fd);
                        client_fd = 0;
                        FD_CLR(client_fd, &server_fd_set);
                        sender_connection_code[i] = 0;
                    }
                }
            }
        }
    }
}

void ConnectingServer::handle_msg(int client_fd, char seq_code_bytes[SEQUENCE_CODE_LENGTH]) {
    char type_code_buff[TYPE_CODE_LENGTH + 1] = {0};
    read(client_fd, type_code_buff, TYPE_CODE_LENGTH);
    int type_code = strtol(type_code_buff, nullptr, 10);
    send_(client_fd, seq_code_bytes, SEQUENCE_CODE_LENGTH, MSG_WAITALL);
    send_(client_fd, type_code_buff, TYPE_CODE_LENGTH, MSG_WAITALL);
    switch (type_code) {
        case MSG_FETCH_AVAILABLE_SENDERS: {
            Serial.println("Get fetch available senders msg");
            int n_sender = 0;
            int conn_codes[MAX_CONNECTION];
            vector<string> filenames;
            for (int i = 0; i < MAX_CONNECTION; i++) {
                if (sender_connection_code[i] != 0) {
                    conn_codes[n_sender] = sender_connection_code[i];
                    filenames.push_back(sender_filenames[i]);
                    n_sender++;
                }
            }
            char temp_buffer[5];
            sprintf(temp_buffer, "%02d", n_sender);
            send_(client_fd, temp_buffer, 2, MSG_WAITALL); // send n_sender
            for (int i = 0; i < n_sender; i++) {
                sprintf(temp_buffer, "%04d", conn_codes[i]);
                send_(client_fd, temp_buffer, 4, MSG_WAITALL); // send conn_code
                string filename = filenames[i];
                char filename_buffer[MAX_FILENAME_LENGTH];

                int filename_length = sprintf(filename_buffer, "%s", filename.c_str());
                sprintf(temp_buffer, "%03d", filename_length);
                send_(client_fd, temp_buffer, 3, MSG_WAITALL); // send length of filename
                // send filename, here I assume that no matter which encoding here uses won't affect the result
                send_(client_fd, filename_buffer, filename_length, MSG_WAITALL);
            }
            break;
        }
        case MSG_REGISTER_SENDER: {
            Serial.println("Get register sender msg");
            char filename_data_length_buffer[4] = {0};
            read(client_fd, filename_data_length_buffer, 3); // read filename data length
            int filename_data_length = strtol(filename_data_length_buffer, nullptr, 10);
            char filename_data[filename_data_length + 1];
            bzero(filename_data, filename_data_length + 1);
            read(client_fd, filename_data, filename_data_length); // read filename
            char port_buffer[6] = {0};
            read(client_fd, port_buffer, 5); // read port
            ushort port = strtol(port_buffer, nullptr, 10);
            int client_index = -1;
            for (int i = 0; i < MAX_CONNECTION; i++) { // search for client index
                if (client_fds[i] == client_fd) {
                    client_index = i;
                }
            }
            if (client_index > 0) {
                sender_connection_code[client_index] = code_generator.next();
                sender_server_port[client_index] = port;
                sender_filenames[client_index] = string(filename_data);

                char connection_code_buffer[5];
                sprintf(connection_code_buffer, "%04d", sender_connection_code[client_index]);
                send_(client_fd, connection_code_buffer, 4, MSG_WAITALL);
            }
            break;
        }
        case MSG_QUERY_SENDER_SERVER_ADDRESS: {
            Serial.println("Get query sender server address msg");
            char conn_code_buffer[5] = {0};
            read(client_fd, conn_code_buffer, 4);
            int conn_code = strtol(conn_code_buffer, nullptr, 10);

            char sender_not_found_response[3] = "00";

            if (conn_code == 0) {
                send_(client_fd, sender_not_found_response, 2, MSG_WAITALL);
            }
            int query_rst = -1;
            for (int i = 0; i < MAX_CONNECTION; i++) {
                if (conn_code == sender_connection_code[i]) {
                    query_rst = i;
                    break;
                }
            }
            if (query_rst > 0) { // query succeed
                char *ip = inet_ntoa(ip_address_net[query_rst]);
                int ip_length = (int) strlen(ip);
                char ip_length_data[3] = {0};
                sprintf(ip_length_data, "%02d", ip_length);
                char port_data[6] = {0};
                sprintf(port_data, "%05d", sender_server_port[query_rst]);
                send_(client_fd, ip_length_data, 2, MSG_WAITALL);
                send_(client_fd, ip, ip_length, MSG_WAITALL);
                send_(client_fd, port_data, 5, MSG_WAITALL);
            } else { // no query result
                send_(client_fd, sender_not_found_response, 2, MSG_WAITALL);
            }
            break;
        }
        default:
            Serial.printf("Unknown msg type code: %d\n", type_code);
    }
}

void setup() {
    Serial.begin(9600);
    WiFi.begin(WIFI_SSID, WIFI_PWD);
    wl_status_t status;
    while ((status = WiFiClass::status()) != WL_CONNECTED) {
        Serial.printf("Status: %d\n", status);
        delay(1000);
    }
    Serial.println("Connected");
}

void loop() {
    ConnectingServer server;
    server.handleLoop();
    delay(1000);
}
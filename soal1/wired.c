#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <time.h>
#include "protocol.h"

typedef struct {
    int socket;
    char name[50];
    int is_admin;
    int waiting_password;
    int authenticated; // untuk memastikan admin terverivikasi dan sudah masuk ke admin console
} Client;

Client clients[MAX_CLIENTS];
time_t start_time;

// kirim pesan ke semua client (broadcast), kecuali pengirimnya sendiri dan admin
void broadcast(char *msg, int sender) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].socket != 0 &&
            clients[i].socket != sender &&
            clients[i].authenticated == 0) {  

            send(clients[i].socket, msg, strlen(msg), 0);
        }
    }
}

// hapus client
void remove_client(int i) {
    char msg[100];
    sprintf(msg, "[User '%s' disconnected]", clients[i].name);

    // kirim informasi ke client lain
    broadcast(msg, clients[i].socket);
    // kirim aktivitas ke history.log
    log_event("System", msg);

    close(clients[i].socket);
    clients[i].socket = 0;
    strcpy(clients[i].name, "");
    clients[i].is_admin = 0;
    clients[i].waiting_password = 0;
    clients[i].authenticated = 0;
}

// putuskan koneksi admin
void remove_admin(int i) {
    // kirim aktivitas ke history.log
    char msg[100];
    sprintf(msg, "[RPC_SHUTDOWN]\n");
    log_event("Admin", msg);

    close(clients[i].socket);
    clients[i].socket = 0;
    strcpy(clients[i].name, "");
    clients[i].is_admin = 0;
    clients[i].waiting_password = 0;
    clients[i].authenticated = 0;
}

int main() {
    int server_fd, new_socket;
    struct sockaddr_in address;
    fd_set readfds;
    char buffer[BUFFER_SIZE];

    // Catat waktu server
    start_time = time(NULL);

    // reset log setiap start
    FILE *f = fopen("history.log", "w");
    fclose(f);

    log_event("System", "[SERVER ONLINE]");

    server_fd = socket(AF_INET, SOCK_STREAM, 0); // socket server

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(8080);

    bind(server_fd, (struct sockaddr *)&address, sizeof(address));
    listen(server_fd, 5); // mulai terima antrian koneksi

    printf("Server running...\n");

    while (1) {
        FD_ZERO(&readfds);
        FD_SET(server_fd, &readfds);
        int max_sd = server_fd;  

        // Tambahkan semua client aktif ke fd_set
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i].socket > 0) {
                FD_SET(clients[i].socket, &readfds);
                // Lacak file descriptor terbesar
                if (clients[i].socket > max_sd)
                    max_sd = clients[i].socket;
            }
        }

        // Tunggu aktivitas di fd manapun 
        select(max_sd + 1, &readfds, NULL, NULL, NULL);

        // koneksi baru
        if (FD_ISSET(server_fd, &readfds)) {
            new_socket = accept(server_fd, NULL, NULL);

            // Cari slot kosong di array clients[]
            for (int i = 0; i < MAX_CLIENTS; i++) {
                if (clients[i].socket == 0) {
                    clients[i].socket = new_socket;
                    clients[i].is_admin = 0;
                    clients[i].waiting_password = 0;
                    clients[i].authenticated = 0;
                    strcpy(clients[i].name, "");

                    send(new_socket, "Enter your name: ", 17, 0);
                    break;
                }
            }
        }

        // handle data dari client
        for (int i = 0; i < MAX_CLIENTS; i++) {
            int sd = clients[i].socket;

            if (sd > 0 && FD_ISSET(sd, &readfds)) {
                int valread = read(sd, buffer, BUFFER_SIZE);

                if (valread <= 0) {
                    remove_client(i);
                    continue;
                }

                buffer[valread] = '\0';

                // hindari input kosong
                if (strlen(buffer) <= 1) continue;

                // jika nama masih kosong (belum login)
                if (strlen(clients[i].name) == 0) {
                    buffer[strcspn(buffer, "\n")] = 0;

                    char names[MAX_CLIENTS][50];
                     // Kumpulkan semua nama yang sudah ada
                    for (int j = 0; j < MAX_CLIENTS; j++)
                        strcpy(names[j], clients[j].name);

                     // cek_nama() dari protocol.h 
                    if (cek_nama(names, MAX_CLIENTS, buffer)) {
                        char msg[200];
                        sprintf(msg,
                        "\n[System] The identity %s is already synchronized in The Wired.\n",
                        buffer);

                        send(sd, msg, strlen(msg), 0);
                        send(sd, "Enter your name: ", 17, 0);
                    }
                    else {
                        // jika nama sudah unik
                        strcpy(clients[i].name, buffer);

                        char msg[100];
                        sprintf(msg, "[User '%s' Connected]", buffer);

                        broadcast(msg, sd);
                        log_event("System", msg); // kirim aktifitas ke history.log

                        char welcome[100];
                        sprintf(welcome, "\n-=-=-=-Welcome to The Wired, %s -=-=-=-\n", buffer);
                        send(sd, welcome, strlen(welcome), 0);

                        // admin
                        if (strcmp(buffer, "The Knights") == 0) {
                            clients[i].is_admin = 1;
                            clients[i].waiting_password = 1;
                            send(sd, "Enter Password: ", 23, 0);
                        }
                    }
                }

                else {

                    // password admin
                    if (clients[i].waiting_password) {
                        buffer[strcspn(buffer, "\n")] = 0;

                        if (strcmp(buffer, "protocol7") == 0) {
                            clients[i].authenticated = 1;
                            clients[i].waiting_password = 0;
                            send(sd, "\n[System] Authentication Successful. Granted Admin privileges.\n ", 62, 0);
                            send(sd,
                            "\n-=-=-=- THE KNIGHT CONSOLE -=-=-=-\n"
                            "1. Check Active Entites (Users)\n"
                            "2. Check Server Uptime\n"
                            "3. Execute Emergency Shutdown\n"
                            "4. Disconnect\n"
                            "Command >> ", 250, 0);

                        } else {
                            send(sd, "Wrong password, write again: \n", 30, 0);
                        }
                        continue;
                    }

                    // menu admin
                    if (clients[i].authenticated) {
                        int choice = atoi(buffer);

                        if (choice == 1) {
                            char list[500] = "Active users:\n";

                            for (int j = 0; j < MAX_CLIENTS; j++) {
                                if (clients[j].socket != 0) {
                                    strcat(list, clients[j].name);
                                    strcat(list, "\n");
                                }
                            }
                            send(sd, list, strlen(list), 0);

                            char logmsg[100];
                            sprintf(logmsg, "[RPC_GET_USERS]");
                            log_event("Admin", logmsg);
                        }

                        else if (choice == 2) {
                            time_t now = time(NULL);
                            int uptime = (int)(now - start_time);

                            char msg[100];
                            sprintf(msg, "Uptime: %d seconds\n", uptime);
                            send(sd, msg, strlen(msg), 0);

                            char logmsg[100];
                            sprintf(logmsg, "[RPC_GET_UPTIME]");
                            log_event("Admin", logmsg);
                        }

                        else if (choice == 3) {
                            log_event("[System]", "[EMERGENCY SHUTDOWN INITIATED]");
                            printf("Server shutting down...\n");
                            exit(0); // langsung keluar dari proses.
                        }

                        else if (choice == 4) {
                            send(sd, "\n[System] Disconnecting from The Wired...\n", 50, 0);
                            // bersihkan slot admin
                            remove_admin(i);
                            continue;
                        }

                        // tampilkan menu lagi
                        send(sd,
                        "\n-=-=-=- THE KNIGHT CONSOLE -=-=-=-\n"
                        "1. Check Active Entites (Users)\n"
                        "2. Check Server Uptime\n"
                        "3. Execute Emergency Shutdown\n"
                        "4. Disconnect\n"
                        "Command >> ", 250, 0);

                        continue;
                    }

                    // user exit
                    if (strncmp(buffer, "/exit", 5) == 0) {
                        remove_client(i);
                    }

                    // chat ke semua client lain berdasarkan format yang telah di tentukan dan mencatat aktivitasnya
                    else {
                        char msg[BUFFER_SIZE];
                        format_message(msg, clients[i].name, buffer);

                        broadcast(msg, sd);
                        log_event("User", msg);
                    }
                }
            }
        }
    }
}
# SISOP-3-2026-IT-011
Nama : Revalinda Bunga Nayla Laksono <br>
Nrp : 5027251011 <br>
## Soal 1
### Deskripsi Permasalahan 
**The Wired**, infrastruktur digital yang berupa pengembangan sebuah server yang stabil dan aplikasi (client) yang mampu menangani fragmentasi identitas. Aplikasi chat berbasis client-server ini berfokus menciptakan ruang komunikasi anonim di mana batar antara individu dileburkan melalui baris-baris kode sistem operasi, memungkinkan terjadinya sinkronisasi tanpa batas di dalam jaringan. <br>
### Struktur Repository
- `protocoh.h`, sebagai header file (deklarasi) yang berisi: konstanta dan prototype fungsi
- `protocol.c`, berisi implementasi dari fungsi-fungsi di `protocol.h`
- `wired.c`, **(Server)** berfungsi untuk menerima koneksi client, mengatur komunikasi antar client, dan mengelola admin.
- `navi.c`,**(Client)** berfungsi untuk terhubung ke server, mengirim dan menerima pesan
### Protocol.h
```c
#ifndef PROTOCOL_H
#define PROTOCOL_H

#define MAX_CLIENTS 10
#define BUFFER_SIZE 1024

// Format pesan user
void format_message(char *output, const char *name, const char *msg);

// Logging
void log_event(const char *role, const char *msg);

// Cek username
int is_name_taken(char names[][50], int count, const char *name);

#endif
```
### Protocol.c
```c
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "protocol.h"

void format_message(char *output, const char *name, const char *msg) {
    sprintf(output, "[%s]: %s", name, msg);
}

void log_event(const char *role, const char *msg) {
    FILE *file = fopen("history.log", "a");

    // mengambil waktu sekarang dalam format timestamp
    time_t now = time(NULL);
    // mengubahnya menjadi struktur waktu lokal
    struct tm *t = localtime(&now);

    fprintf(file, "[%04d-%02d-%02d %02d:%02d:%02d] [%s] %s\n",
        t->tm_year+1900, t->tm_mon+1, t->tm_mday,
        t->tm_hour, t->tm_min, t->tm_sec,
        role, msg);

    fclose(file);
}

// cek nama user, apakah uniqe
int cek_nama(char names[][50], int count, const char *name) {
    for (int i = 0; i < count; i++) {
        // jika tidak unik
        if (strcmp(names[i], name) == 0)
            return 1;
    }
    return 0;
}
```
Function `format_massage()` membentuk format pesan yang akan dikirim antar client melalui server, yaitu **[nama_pengirim] : [pesan]**. `log_event()` mencatat semua aktivitas sistem ke dalam `history.log` dengan format **[YYYY-MM-DD HH:MM:SS] [System/Admin/User] [Status/Comamnd/Chat]**. `cek_nama()` akan memastikan bahwa setiap client mempunyai nama unik dengan perintah `strcmp` untuk membandingkan nama-nama client. <br>
### navi.c
```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/select.h>

// port server yang akan dihubungi
#define PORT 8080
#define BUFFER_SIZE 1024

int main() {
    // File descriptor untuk socket
    int sock;
    // Struktur berisi info alamat server
    struct sockaddr_in serv_addr;
    char buffer[BUFFER_SIZE];

    // buat socket
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("Socket failed");
        return 1;
    }

    // konfigurasi alamat server
    serv_addr.sin_family = AF_INET; // keluarga alamat: IPv4
    serv_addr.sin_port = htons(PORT);

    // mengubah IP string ke binary
    if (inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr) <= 0) {
        perror("Invalid address");
        return 1;
    }

    // connect ke server
    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("Connection failed");
        return 1;
    }

    printf("Successfully Connected to The Wired\n");

    // loop utama (ASYNC)
    while (1) {
        fd_set readfds;
        FD_ZERO(&readfds);

        // monitor server & keyboard
        FD_SET(sock, &readfds);
        FD_SET(STDIN_FILENO, &readfds);

        int max_sd = (sock > STDIN_FILENO) ? sock : STDIN_FILENO;

        // tunggu aktivitas
        if (select(max_sd + 1, &readfds, NULL, NULL, NULL) < 0) {
            perror("select error");
            break;
        }

        // ada pesan dari server
        if (FD_ISSET(sock, &readfds)) { // cek apakah sock masih ada di readfds
            // baca data
            int valread = read(sock, buffer, BUFFER_SIZE);

            if (valread <= 0) {
                // Server tutup koneksi atau terjadi error
                printf("Successfully Disconnected from server\n");
                break;
            }

            // jika tidak eror akan mencetak pesan dari server
            buffer[valread] = '\0';
            printf("%s", buffer);
            fflush(stdout);
        }

        // user mengetik
        if (FD_ISSET(STDIN_FILENO, &readfds)) {
            if (fgets(buffer, BUFFER_SIZE, stdin) != NULL) {
                // kririm data ke server nelalui socket
                send(sock, buffer, strlen(buffer), 0);
            }
        }
    }

    close(sock);
    return 0;
}
```
Setelah dibuat dan dihubungkan ke server, program masuk ke loop tak terbatas. Setiap iterasi dimulai dengan `select()` yang memblokir program sambil menunggu, ia akan "terbangun" hanya jika ada data dari server atau ada input keyboard dari client. Karena keduanya dipantau sekaligus, tidak ada situasi di mana menunggu server membuat keyboard tidak responsif, dan sebaliknya. <br>
### wired.c
```c
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
```
**struktur data client** :
- `socket` → identitas koneksi client
- `name` → nama pengguna
- `is_admin` → penanda apakah user adalah admin
- `waiting_password` → status menunggu input password
- `authenticated` → status apakah admin sudah terverifikasi <br>
Semua client disimpan dalam array global `clients[MAX_CLIENTS]`, sehingga server dapat menangani banyak client secara bersamaan. <br>

Server menggunakan mekanisme `select()` untuk memantau banyak socket dalam satu waktu tanpa blocking, sehingga mampu melayani banyak client secara real-time. Setiap kali ada koneksi baru, server akan menerimanya melalui `accept()`, mencari slot kosong, lalu meminta memasukkan nama untuk diverifikasi agar tidak sama dengan client lain. `broadcast()` mengirimkan pesan yang telah diformat ke semua client kecuali pengirimnya sendiri dan admin. <br>

"The Knights" sebagai kode untuk admin akan dimintai password (`waiting_password` == 1), jika terverifikasi (`authenticated` == 1) admin mendapat akses ke menu khusus yang memungkinkan melihat daftar user aktif, mengecek waktu uptime server, melakukan shutdown, atau keluar dari sistem. Semua aktivitas System, User (client), dan Admin akan dicatat ke dalam file log. <br>
### Kode Program
Kode Program secara lengkap dapat dilihat pada [soal_1]() <br>
<img width="360" height="170" alt="1_start" src="https://github.com/user-attachments/assets/a8b7f354-4bee-4d6c-94b4-ce3d655f155b" /> <br>
<img width="362" height="179" alt="1_client ke-1" src="https://github.com/user-attachments/assets/05ae5ce3-f675-44fb-9efd-6ce87a04e11d" /> <br>
<img width="425" height="211" alt="1_client ke-2" src="https://github.com/user-attachments/assets/fcb8933f-1bf7-42ef-bec7-cf1c04dccc59" /> <br>
<img width="387" height="606" alt="1_admin console" src="https://github.com/user-attachments/assets/18f1f047-1a96-4c58-9c22-02001114b816" /> <br>
<img width="383" height="255" alt="1_emergency shutdown" src="https://github.com/user-attachments/assets/a2691263-768b-434f-91e0-802e0e90a02e" /> <br>
<img width="497" height="279" alt="1_history log" src="https://github.com/user-attachments/assets/9eb0f828-e955-49ad-9c0e-2839fa3adcb1" /> <br>


## Soal 2
### Deskripsi Permasalahan 
Seorang panglima tempur harus membuat arena pertempuran yang terintegrasi. `arena.h` akan menjadi tempat bagaimana pertempuran dimulai, semua srategi dan sistem pertempuran ada disini, `orion.c` akan bertindak sebagi server, dan `eternal.c` akan bertindak sebagi client. <br>

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
Kode Program secara lengkap dapat dilihat pada [soal_1](https://github.com/rbvnga/SISOP-3-2026-IT-011/tree/main/soal1) <br>
<img width="360" height="170" alt="1_start" src="https://github.com/user-attachments/assets/a8b7f354-4bee-4d6c-94b4-ce3d655f155b" /> <br>
<img width="362" height="179" alt="1_client ke-1" src="https://github.com/user-attachments/assets/05ae5ce3-f675-44fb-9efd-6ce87a04e11d" /> <br>
<img width="425" height="211" alt="1_client ke-2" src="https://github.com/user-attachments/assets/fcb8933f-1bf7-42ef-bec7-cf1c04dccc59" /> <br>
<img width="387" height="606" alt="1_admin console" src="https://github.com/user-attachments/assets/18f1f047-1a96-4c58-9c22-02001114b816" /> <br>
<img width="383" height="255" alt="1_emergency shutdown" src="https://github.com/user-attachments/assets/a2691263-768b-434f-91e0-802e0e90a02e" /> <br>
<img width="497" height="279" alt="1_history log" src="https://github.com/user-attachments/assets/9eb0f828-e955-49ad-9c0e-2839fa3adcb1" /> <br>


## Soal 2
### Deskripsi Permasalahan 
Seorang panglima tempur harus membuat arena pertempuran yang terintegrasi. `arena.h` akan menjadi tempat bagaimana pertempuran dimulai, semua srategi dan sistem pertempuran ada disini, `orion.c` akan bertindak sebagi server, dan `eternal.c` akan bertindak sebagi client. <br>
Konsep IPC(Inter-Process Communication) yang digunakan:
- Message Queue  -> komunikasi aksi (pesan) antara warrior (client) dan server
- Shared Memory  -> menyimpan data player yang persisten & dapat diakses bersama <br>
Sistem mengalokasikan satu blok memori yang dapat diakses oleh beberapa proses sekaligus. Karena berada di ruang memori yang sama, proses tidak perlu menyalin data bolak-balik.
- Semaphore      -> mencegah Race Condition <br>
Jika banyak proses mengakses Shared Memory secara bersamaan, muncul risiko Race Condition (kondisi di mana dua proses mengubah data yang sama di waktu yang sama, menyebabkan data korup). <br>
### arena.h
```c
#ifndef ARENA_H
#define ARENA_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <time.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/msg.h>
#include <sys/sem.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>

#define SHARED_MEMORY_KEY 0x00001234 // Key untuk Shared Memory
#define MSG_KEY 0x00005678 // Key untuk Message Queue
#define SEM_KEY 0x00009012 // Key untuk Semaphore

#define MAX_PLAYERS         32
#define MAX_USERNAME        32
#define MAX_PASSWORD        32
#define MAX_HISTORY         50
#define MAX_LOG             5    // jumlah combat log yang ditampilkan
#define MATCHMAKING_TIMEOUT 35   // 35 detik matchmaking sebelum melawan bot

// state default warrior
#define BASE_GOLD           150
#define BASE_LVL            1
#define BASE_XP             0
#define BASE_HEALTH         100
#define BASE_DAMAGE         10

// pengaturan state berdasarkan hasil pertandingan
#define XP_WIN              50
#define XP_LOSS             15
#define GOLD_WIN            120
#define GOLD_LOSS           30

#define MAX_WEAPONS         5

typedef struct {
    char name[32];
    int  price;
    int  bonus_damage;
} Weapon; // Menyimpan data senjata yang bisa dibeli di armory

// isi armory
static const Weapon WEAPON_LIST[MAX_WEAPONS] = {
    {"Wood Sword",   100,   5},
    {"Iron Sword",   300,  15},
    {"Steel Axe",    600,  30},
    {"Demon Blade",  1500, 60},
    {"God Slayer",   5000, 150}
};

// untuk mencatat hasil satu pertandingan (battle)
typedef struct {
    char opponent[MAX_USERNAME];
    int result;
    int xp_gained;
    int hour;
    int minute;
} MatchRecord;

typedef struct {
    char username[MAX_USERNAME];
    char password[MAX_PASSWORD];
    int  gold;
    int  lvl;
    int  xp;
    int  weapon_index;
    int  in_battle;
    int  in_matchmaking;
    int  is_logged_in;
    int  opponent_pid;   // PID lawan saat battle 
    int  hp;   
    int  is_registered; 
    pid_t pid;  // PID proses eternal yang sedang login 

    MatchRecord history[MAX_HISTORY];
    int history_count;
} Player;

 // container utama untuk menyimpan data player (warrior) di shared memory
typedef struct {
    Player players[MAX_PLAYERS];
    int player_count;
} SharedData;

//Tipe-tipe aksi dalam arena 
#define MSG_PING            1    // Cek apakah server aktif 
#define MSG_REGISTER        2
#define MSG_LOGIN           3
#define MSG_LOGOUT          4
#define MSG_MATCHMAKING     5
#define MSG_ATTACK          6
#define MSG_ULTIMATE        7
#define MSG_BUY_WEAPON      8
#define MSG_VIEW_HISTORY    9
#define MSG_CANCEL_MM       10
#define MSG_BATTLE_UPDATE   11   /* Update state battle (HP, log) */
#define MSG_BATTLE_END      12   /* Notifikasi battle selesai */
#define MSG_RESPONSE        13   /* Response umum dari server */

#define BATTLE_MTYPE_BASE   2000000LL

typedef struct {
    long msg_type;
    int  action; // Jenis aksi 
    char username[MAX_USERNAME];
    char password[MAX_PASSWORD];
    char data[256]; 
    pid_t sender_pid; 
    int  int_data;
    int  success;
} Message;

// UNION untuk semctl (semaphore control)
union semun {
    int val;
    struct semid_ds *buf;
    unsigned short *array;
};

// Hitung total damage berdasarkan XP dan senjata 
static inline int hitung_damage(Player *p) {
    int bonus_weapon = 0;
    if (p->weapon_index >= 0 && p->weapon_index < MAX_WEAPONS) {
        bonus_weapon = WEAPON_LIST[p->weapon_index].bonus_damage;
    }
    return BASE_DAMAGE + (p->xp / 50) + bonus_weapon;
}

// Hitung total HP maksimum berdasarkan XP 
static inline int hitung_max_hp(Player *p) {
    return BASE_HEALTH + (p->xp / 10);
}

// Hitung damage Ultimate 
static inline int hitung_ultimate(Player *p) {
    return hitung_damage(p) * 3;
}

// getter nama senjata player
static inline const char* get_weapon_name(Player *p) {
    if (p->weapon_index < 0) return "None";
    return WEAPON_LIST[p->weapon_index].name;
}

#endif 
```
`arena.h` merupakan header utama dalam Battle ini untuk mendefinisikan struct, konstanta, dan semua key yang di butuhkan.

```c
#define SHARED_MEMORY_KEY 0x00001234 
typedef struct {
    Player players[MAX_PLAYERS];
    int player_count;
} SharedData;
```
`SHARED_MEMORY_KEY` mendefinisikan key untuk Shared Memory yang digunakan agar server dan client menunjuk ke lokasi memori yang sama dan seluruh data pemain disimpan dalam `struct SharedData`. Shared Memory berfungsi sebagai database pusat yang bersifat _real-time_. Status HP, level, gold, dan weapon setiap pemain disimpan di sini sehingga saat satu proses (misal: proses Battle) mengubah HP pemain, proses lain (misal: proses Client) bisa langsung melihat perubahannya. <br>
Konsep Semaphore dalam kode, terlihat pada `SEM_KEY 0x00009012`, dan penggunaan `union semun` yang merupakan standar untuk operasi `semctl` (seperti inisialisasi nilai semaphore), sehingga akses ke `SharedData` selalui didahului dengan operasi Wait (mengunci) dan diakhiri dengan Signal (melepas kunci). <br>
```c
#define MSG_KEY 0x00005678
#define BATTLE_MTYPE_BASE   2000000LL

typedef struct {
    long msg_type;
    int  action; // Jenis aksi 
    char username[MAX_USERNAME];
    char password[MAX_PASSWORD];
    char data[256]; 
    pid_t sender_pid; 
    int  int_data;
    int  success;
} Message;
```
Proses dapat mengirimkan "paket" pesan ke antrean, dan proses lain dapat mengambilnya berdasarkan tipe pesan tertentu (`msg_type`). Perubahan state warrior dikirimkan dari server ke client melalui Message Queue menggunakan PID client sebagai alamat tujuan. <br>
Mekanisme Filtering:
- **Server Listen**: Server mendengarkan pesan dengan `msg_type = 1`.
- **Client Response**: Server mengirim balik ke client menggunakan `msg_type = PID_Client`.
- `sg_ = 2000000 + PID_client` untuk memastikan perintah Attack atau Ultimate langsung masuk ke thread pertempuran, bukan ke loop utama server. `_sg` sebagai label aksi (pesan) yang terjadi ketika battle berlangsung. <br>

## eternal.c
```c
#include "arena.h"
#include <termios.h> // program membaca input saat tombol ditekan tanpa perlu Enter
#include <fcntl.h>

// Global state client 
int massage_Queue_id = -1; // sama dengan server
int shared_memory_id = -1;
SharedData *shm = NULL;

// Info player yang sedang login
char current_user[MAX_USERNAME] = "";
int  current_lvl    = 0;
int  current_gold   = 0;
int  current_xp     = 0;
int  current_weapon = -1;
int  is_logged_in   = 0;

volatile int battle_running = 0;
volatile int battle_result  = -1;  
// -1 = ongoing, 1 = menang, 0 = kalah

// Mendefinisikan warna terminal ANSI 
#define RED     "\033[1;31m"
#define GREEN   "\033[1;32m"
#define YELLOW  "\033[1;33m"
#define CYAN    "\033[1;36m"
#define WHITE   "\033[1;37m"
#define RESET   "\033[0m"
#define BOLD    "\033[1m"

// tampilan banner di menu
void print_banner() {
    printf(CYAN);
    printf("  ______  ____  ______  _____    _____  ____   _     __ \n");
    printf(" |  ____||_  _||  ____||  __ \\ |_   _|/ __ \\| \\  |  |\n");
    printf(" | |__    | |  | |__   | |__) |   | | | | || ||  \\ |  |\n");
    printf(" |  __|   | |  |  __|  |  _  /    | | | | || .|`  \\|  |\n");
    printf(" | |____  | |  | |____ | | \\ \\ _| |_| |__| ||  | \\  |\n");
    printf(" |______| |_|  |______||_|  \\_\\_____||____/ |__|  \\_|\n");
    printf(RESET);
}
```
### Terminal utilies
```c
tatic struct termios original_termios;

void enable_raw_mode() {
    tcgetattr(STDIN_FILENO, &original_termios); // menyalin pengaturan terminal saat ini, agar bisa dikembalikan
    struct termios raw = original_termios;
    raw.c_lflag &= ~(ECHO | ICANON);  // mematikan tampilan karakter yang diketik dan mematikan buffering baris
    raw.c_cc[VMIN] = 0; // Non-blocking read 
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

void disable_raw_mode() {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &original_termios);
}
```
`enable_raw_mode()` mengalihkan dari mode Canonical ke mode Raw, agar saat pemain memberi input (menekan tombol keyboard), program menangkap input tersebut tanpa harus memunculkannya di layar terminal, serta supaya program tidak akan berhenti menunggu input. Jika tidak ada input, ia akan langsung lanjut ke baris kode berikutnya (non-blocking read). `disable_raw_mode()` akan mengembalikan pengaturan terminal ke kondisi semula menggunakan cadangan `orig_termios`. <br>
### Kirim pesan ke server
```c
void send_to_server(int action, const char *username, const char *password,
                    const char *data, int int_data) {
    Message msg;
    memset(&msg, 0, sizeof(msg));
    msg.msg_type = 1;
    msg.action = action;
    msg.sender_pid = getpid();
    msg.int_data = int_data;
    if (username) strncpy(msg.username, username, MAX_USERNAME - 1);
    if (password) strncpy(msg.password, password, MAX_PASSWORD - 1);
    if (data)     strncpy(msg.data, data, sizeof(msg.data) - 1);
    msgsnd(massage_Queue_id, &msg, sizeof(msg) - sizeof(long), 0);
}
void send_battle_action(int action) {
    Message msg;
    memset(&msg, 0, sizeof(msg));
    msg.msg_type = BATTLE_MTYPE_BASE + (long)getpid(); 
    msg.action = action;
    msg.sender_pid = getpid();
    strncpy(msg.username, current_user, MAX_USERNAME - 1);
    msgsnd(massage_Queue_id, &msg, sizeof(msg) - sizeof(long), 0);
}
```
Pada `send_to_server()`, `msg_type = 1` menjamin bahwa pesan akan dirutekan ke antrean utama server sedangkan pada `send_battle_action()` menggunakan `msg_type` yang berbeda agar battle thread di server listen ke msg_type ini secara spesifik. Melalui system call `msgsnd` akan mengirim pesan ke antrean pesan (Message Queue). <br>
### Terima pesan dari server
```c
int recv_from_server(Message *resp, int timeout_sec) {
    if (time<= 0) {
        // menunggu sampai ada pesan masuk
        return msgrcv(massage_queue_id, resp, sizeof(*resp) - sizeof(long),
                      (long)getpid(), 0);
    } else {
        time_t start = time(NULL);
        // mengecek antrean berulang kali selama durasi timeout_sec
        while (difftime(time(NULL), start) < timeout_sec) {
            //Jika tidak ada pesan, program tidak menunggu
            int r = msgrcv(massage_queue_id, resp, sizeof(*resp) - sizeof(long),
                           (long)getpid(), IPC_NOWAIT);
            if (r > 0) return r;
            usleep(100000); 
        }
        return -1; 
    }
}
```
Server kirim balasan dengan msg_type = PID client. Ketika `timeout_sec <= 0` program mengeksekusi `msgrcv` tanpa flag tambahan (flag `0`). Program tidak akan lanjut ke baris kode berikutnya sampai ada pesan yang masuk ke antrean dengan msg_type yang sesuai (mode blocking). Sedangkan  `timeout_sec > 0` program akan mengecek antrian menggunakan flag `IPC_NOWAIT` selama durasi `timeout_sec`, jika belum ada pesan, program akan menunggu 0,1 detik untuk di cek kembali. <br>
### Cek koneksi server
```c
int check_server() {
    // Client mengirimkan pesan
    send_to_server(MSG_PING, NULL, NULL, NULL, 0);
    Message resp;
    // jika dalam 3 detik server tidak menjawab, fungsi ini akan menganggap server mati
    if (recv_from_server(&resp, 3) > 0 && resp.success) {
        return 1;
    }
    return 0;
}
```
### Tampilan menu sebelum dan sesudah login 
```c
// Main Menu, sebelum login
void show_main_menu() {
    clear_screen();
    print_banner();
    printf("\n");
    printf("  1. Register\n");
    printf("  2. Login\n");
    printf("  3. Exit\n");
    printf("\nChoice: ");
    fflush(stdout);
}

// Profile player
void show_game_menu() {
    clear_screen();
    print_banner();
    printf("\n");
    printf("  ╔══════════ PROFILE ══════════╗\n");
    printf("  ║ Name : %-20s ║\n", current_user);
    printf("  ║ Lvl  : %-3d                  ║\n", current_lvl);
    printf("  ║ Gold : %-5d  XP : %-5d    ║\n", current_gold, current_xp);
    printf("  ╚═════════════════════════════╝\n\n");
    printf("  1. Battle\n");
    printf("  2. Armory\n");
    printf("  3. History\n");
    printf("  4. Logout\n");
    printf("\n> Choice: ");
    fflush(stdout);
}
```
### Register dan Login 
```c
void do_register() {
    char username[MAX_USERNAME], password[MAX_PASSWORD];

    clear_screen();
    printf("  ╔══════ CREATE ACCOUNT ══════╗\n");
    printf("  Username: ");
    fflush(stdout);
    scanf("%31s", username);

    printf("  Password: ");
    fflush(stdout);
    scanf("%31s", password);

    // kirim pesan do_register yang berisi username dan password ke server
    send_to_server(MSG_REGISTER, username, password, NULL, 0);

    // menerima pesan dari server sebagai balasan dari pesan do_register
    Message resp;
    if (recv_from_server(&resp, 5) > 0) {
        if (resp.success) {
            printf("\n  " GREEN "Registration successful!" RESET "\n");
        } else {
            printf("\n  " RED "%s" RESET "\n", resp.data);
        }
    } else {
        printf("\n  " RED "Server timeout!" RESET "\n");
    }
    printf("\n  Press ENTER to continue...");
    getchar(); getchar();
}

int login() {
    char username[MAX_USERNAME], password[MAX_PASSWORD];

    clear_screen();
    printf("  ╔══════════ LOGIN ══════════╗\n");
    printf("  Username: ");
    fflush(stdout);
    scanf("%31s", username);

    printf("  Password: ");
    fflush(stdout);
    scanf("%31s", password);

    // kirim pesan login ke server
    send_to_server(MSG_LOGIN, username, password, NULL, 0);

    Message resp;
    if (recv_from_server(&resp, 5) > 0) {
        if (resp.success) {
            // Parse data
            char uname[MAX_USERNAME];
            int  lvl, gold, xp, weapon_idx;
            sscanf(resp.data, "%31[^|]|%d|%d|%d|%d",
                   uname, &lvl, &gold, &xp, &weapon_idx);
            strncpy(current_user, uname, MAX_USERNAME - 1);
            current_lvl = lvl;
            current_gold = gold;
            current_xp = xp;
            current_weapon = weapon_idx;
            is_logged_in = 1; // player berhasil login
            printf("\n  " GREEN "Welcome!" RESET "\n");
            sleep(1);
            return 1;
        } else {
            printf("\n  " RED "%s" RESET "\n", resp.data);
            printf("\n  Press ENTER to continue...");
            getchar(); getchar();
        }
    } else {
        printf("\n  " RED "Server timeout!" RESET "\n");
        printf("\n  Press ENTER to continue...");
        getchar(); getchar();
    }
    return 0;
}

// Menampilkan arena pertempuran dengan HP bar, combat log, dan cooldown timer.
void render_battle(const char *data, int is_player_a) {
    char name_a[32], name_b[32];
    int hp_a, maxhp_a, hp_b, maxhp_b;
    char logs[MAX_LOG][128];
    int damage_a, damage_b, cd_a, cd_b, ulti_a, ulti_b;

    // Parse data
    sscanf(data, "%31[^|]|%d|%d|%31[^|]|%d|%d|%127[^|]|%127[^|]|%127[^|]|%127[^|]|%127[^|]|%d|%d|%d|%d|%d|%d",
           name_a, &hp_a, &maxhp_a,
           name_b, &hp_b, &maxhp_b,
           logs[0], logs[1], logs[2], logs[3], logs[4],
           &damage_a, &damage_b, &cd_a, &cd_b, &ulti_a, &ulti_b);

    clear_screen();

    printf(YELLOW "  ═══════════════════ ARENA ═══════════════════\n" RESET);

    // HP bars 
    int bar_width = 20;

    // Player 1
    printf("  " CYAN "%s" RESET "  Lvl 1\n", name_a);
    printf("  [");
    int filled_a = (maxhp_a > 0) ? (hp_a * bar_width / maxhp_a) : 0;
    for (int i = 0; i < bar_width; i++) {
        printf(i < filled_a ? RED "█" RESET : " ");
    }
    printf("] %d/%d\n\n", hp_a, maxhp_a);

    printf("       " BOLD "VS\n" RESET);

    // Player 2 (lawan) 
    printf("  " YELLOW "%s" RESET "  Lvl 1 | Weapon: %s\n",
           name_b,
           (is_player_a ? (current_weapon >= 0 ? WEAPON_LIST[current_weapon].name : "None") : "?"));
    printf("  [");
    int filled_b = (maxhp_b > 0) ? (hp_b * bar_width / maxhp_b) : 0;
    for (int i = 0; i < bar_width; i++) {
        printf(i < filled_b ? GREEN "█" RESET : " ");
    }
    printf("] %d/%d\n\n", hp_b, maxhp_b);

    // Combat log 
    printf("  Combat Log:\n");
    for (int i = 0; i < MAX_LOG; i++) {
        printf("  %s\n", logs[i][0] ? logs[i] : ">");
    }
    printf("\n");

    // Cooldown display 
    double cd_a_sec = cd_a / 10.0;
    double cd_b_sec = cd_b / 10.0;
    printf("  CD: Atk(%.1fs) | Ult(%.1fs)\n", cd_a_sec, cd_b_sec);
    printf("\n  [A] Attack  [U] Ultimate\n");
    fflush(stdout);
}
```

### Matchmaking 
```c
void matchmaking() {
    send_to_server(MSG_MATCHMAKING, current_user, NULL, NULL, 0);

    Message resp;
    // Tunggu konfirmasi masuk antrian 
    if (recv_from_server(&resp, 5) <= 0 || !resp.success) {
        printf("  " RED "Failed to join matchmaking: %s" RESET "\n", resp.data);
        sleep(2);
        return;
    }

    // mencari lawan
    printf("\n  Searching for an opponent");
    fflush(stdout);

    time_t start = time(NULL);
    char anim[] = {'|', '/', '-', '\\'};
    int anim_idx = 0;

    // Tunggu notifikasi dari server (match found atau timeout jadi bot) 
    while (1) {
        printf("\r  Searching for an opponent... [%c] (t-%ds)",
               anim[anim_idx++ % 4],
               (int)(MATCHMAKING_TIMEOUT - difftime(time(NULL), start)));
        fflush(stdout);

        // Non-blocking check untuk pesan matchmaking
        Message notif;
        int r = msgrcv(massage_queue_id, &notif, sizeof(notif) - sizeof(long),
                       (long)getpid(), IPC_NOWAIT);

        if (r > 0 && notif.action == MSG_MATCHMAKING && notif.success) {
            printf("\n\n  " GREEN "Opponent found!" RESET "\n");

            // Parse: "PLAYER|name" atau "BOT|Wild Beast"
            char type[16], opp_name[MAX_USERNAME];
            sscanf(notif.data, "%15[^|]|%31s", type, opp_name);

            printf("  Opponent: %s\n", opp_name);
            sleep(1);

            do_battle(opp_name, 1);
            return;
        }

        usleep(500000);
    }
}
```
`MSG_MATCHMAKING` dikirim ke server lalu masuk ke loop untuk menampilkan animasi loading sekaligus memeriksa pesan terus-menerus dengan flag `IPC_NOWAIT`. Jika lawan ditemukan, server akan mengirim data nama plauer atau bot sebagai lawan lalu masuk ke fase pertarungan. <br>
### Tampilan battle screen
```c
void render_battle(const char *data, int is_player_a) {
    char name_a[32], name_b[32];
    int hp_a, maxhp_a, hp_b, maxhp_b;
    char logs[MAX_LOG][128];
    int damage_a, damage_b, cd_a, cd_b, ulti_a, ulti_b;

    // Parse data
    sscanf(data, "%31[^|]|%d|%d|%31[^|]|%d|%d|%127[^|]|%127[^|]|%127[^|]|%127[^|]|%127[^|]|%d|%d|%d|%d|%d|%d",
           name_a, &hp_a, &maxhp_a,
           name_b, &hp_b, &maxhp_b,
           logs[0], logs[1], logs[2], logs[3], logs[4],
           &damage_a, &damage_b, &cd_a, &cd_b, &ulti_a, &ulti_b);

    clear_screen();

    printf(YELLOW "  ═══════════════════ ARENA ═══════════════════\n" RESET);

    // HP bars 
    int bar_width = 20;

    // Player 1
    printf("  " CYAN "%s" RESET "  Lvl 1\n", name_a);
    printf("  [");
    int filled_a = (maxhp_a > 0) ? (hp_a * bar_width / maxhp_a) : 0;
    for (int i = 0; i < bar_width; i++) {
        printf(i < filled_a ? RED "█" RESET : " ");
    }
    printf("] %d/%d\n\n", hp_a, maxhp_a);

    printf("       " BOLD "VS\n" RESET);

    // Player 2 (lawan) 
    printf("  " YELLOW "%s" RESET "  Lvl 1 | Weapon: %s\n",
           name_b,
           (is_player_a ? (current_weapon >= 0 ? WEAPON_LIST[current_weapon].name : "None") : "?"));
    printf("  [");
    int filled_b = (maxhp_b > 0) ? (hp_b * bar_width / maxhp_b) : 0;
    for (int i = 0; i < bar_width; i++) {
        printf(i < filled_b ? GREEN "█" RESET : " ");
    }
    printf("] %d/%d\n\n", hp_b, maxhp_b);

    // Combat log 
    printf("  Combat Log:\n");
    for (int i = 0; i < MAX_LOG; i++) {
        printf("  %s\n", logs[i][0] ? logs[i] : ">");
    }
    printf("\n");

    // Cooldown display 
    double cd_a_sec = cd_a / 10.0;
    double cd_b_sec = cd_b / 10.0;
    printf("  CD: Atk(%.1fs) | Ult(%.1fs)\n", cd_a_sec, cd_b_sec);
    printf("\n  [A] Attack  [U] Ultimate\n");
    fflush(stdout);
}
```
### Battle 
```c
// Argumen untuk thread battle
typedef struct {
    int is_player_a;
    char opponent[MAX_USERNAME];
} BattleUIArgs;

// Menerima update dari server dan render UI
void* battle_recv_thread(void *arg) {
    BattleUIArgs *bua = (BattleUIArgs*)arg;

    while (battle_running) {
        Message msg;
        memset(&msg, 0, sizeof(msg));

        // cek update battle 
        int r = msgrcv(massage_queue_id, &msg, sizeof(msg) - sizeof(long),
                       (long)getpid(), IPC_NOWAIT);

        if (r > 0) {
            // jika menerima pesan untuk me-render battle 
            if (msg.action == MSG_BATTLE_UPDATE) {
                render_battle(msg.data, bua->is_player_a);
            } 
            // jika server mengirim pesan bahwa pertarungan selesai (salah satu playyer mati)
            else if (msg.action == MSG_BATTLE_END) {
                battle_running = 0;
                battle_result  = msg.success;
            }
        }

        usleep(50000);
    }

    free(bua);
    return NULL;
}


void battle(const char *opponent, int is_player_a) {
    battle_running = 1;
    battle_result  = -1; // belum ada hasil pertandingan
    // mengaktifkan raw mode
    enable_raw_mode();
     // buang sisa karakter dari menu sebelumnya
    {
        char flush_buf;
        while (read(STDIN_FILENO, &flush_buf, 1) > 0) { /* buang semua */ }
    }

    // Spawn receiver thread 
    BattleUIArgs *bua = malloc(sizeof(BattleUIArgs));
    bua->is_player_a = is_player_a;
    strncpy(bua->opponent, opponent, MAX_USERNAME - 1);

    // Multithreading
    pthread_t recv_tid;
    pthread_create(&recv_tid, NULL, battle_recv_thread, bua);

    while (battle_running) {
        char c = 0;
        // baca input
        int r = read(STDIN_FILENO, &c, 1);
        if (r > 0) {
            if (c == 'a' || c == 'A') {
                // warrior menyerang (attack)
                send_battle_action(MSG_ATTACK);   
            } else if (c == 'u' || c == 'U') {
                // warrior me-ultimate 
                send_battle_action(MSG_ULTIMATE);  
            }
        }
        usleep(10000); 
    }

    // Mengembalikan terminal ke mode normal setelah pertandingan selesai
    disable_raw_mode();

    // Tunggu receiver thread selesai 
    pthread_join(recv_tid, NULL);

    // menampilkan hasil pertandingan dan menambah state playernya
    clear_screen();
    if (battle_result == 1) {
        printf(GREEN "\n  -=- VICTORY -═-\n" RESET);
        printf("  XP gained: +%d\n", XP_WIN);
        printf("  Gold gained: +%d\n", GOLD_WIN);
        current_xp   += XP_WIN;
        current_gold += GOLD_WIN;
    } else {
        printf(RED "\n  -═- DEFEAT -═-\n" RESET);
        printf("  XP gained: +%d\n", XP_LOSS);
        printf("  Gold gained: +%d\n", GOLD_LOSS);
        current_xp   += XP_LOSS;
        current_gold += GOLD_LOSS;
    }
    current_lvl = 1 + (current_xp / 100);

    printf("\n  Battle ended. Press [ENTER] to continue...");
    fflush(stdout);

    {
        int ch;
        while ((ch = getchar()) != '\n' && ch != EOF) { }
    }
}
```
Pada fase battle, program dalam mode raw sehingga setiap tombol langsung terdeteksi tanpa menunggu Enter. Tombol 'a' = attack, 'u' = ultimate. Input dikirim ke server sebagai `MSG_ATTACK` atau `MSG_ULTIMATE`. Multhithreading di terapkan untuk mendengarkan (listen) input dari keyboard dan tread baru `recv_tid` bertugas memantau pembaruan status pertempuran dari server melalui Message Queue secara real-time tanpa memblokir (interrupt) alur input pemain pada thread utama. Selain itu, program juga akan menampilkan hasil pertandingan beserta hadiah yang diperoleh, sementara server damage calculation dan cooldown enforcement . <br>
### Armory dan History 
```c
void armory() {
    while (1) {
        clear_screen();
        printf("-=-=-=-=-=-=- ARMORY -=-=-=-=-=-=-\n");
        // menampilkan gold yang dimiliki
        printf(" Gold: %d\n\n", current_gold);

        for (int i = 0; i < MAX_WEAPONS; i++) {
            printf("  %d. %-15s | %4d G  +%d Damage",
                   i + 1,
                   WEAPON_LIST[i].name,
                   WEAPON_LIST[i].price,
                   WEAPON_LIST[i].bonus_damage);
            if (current_weapon == i) printf(GREEN " [EQUIPPED]" RESET);
            printf("\n");
        }
        printf("  0. Back \nChoice: ");
        fflush(stdout);

        int choice;
        scanf("%d", &choice);

        // back
        if (choice == 0) break;
        // jika pilihan weapon tidak valid
        if (choice < 1 || choice > MAX_WEAPONS) continue;

        // kirim pesan ke server bahwa player membeli senjata 
        send_to_server(MSG_BUY_WEAPON, current_user, NULL, NULL, choice - 1);

        Message resp;
        if (recv_from_server(&resp, 5) > 0) {
            if (resp.success) {
                //Update gold dan weapon
                sscanf(resp.data, "Bought %*[^!]! Gold: %d", &current_gold);
                // senjata hanya otomatis terpasang jika memiliki damage lebih besar dari yang dimiliki
                if (WEAPON_LIST[choice-1].bonus_damage >
                    (current_weapon >= 0 ? WEAPON_LIST[current_weapon].bonus_damage : 0)) {
                    current_weapon = choice - 1;
                }
                printf("  " GREEN "%s" RESET "\n", resp.data);
            } else {
                printf("  " RED "%s" RESET "\n", resp.data);
            }
        }
        sleep(1);
    }
}


void history() {
    send_to_server(MSG_VIEW_HISTORY, current_user, NULL, NULL, 0);

    Message resp;
     // jika dalam 5 detik server tidak menjawab
    if (recv_from_server(&resp, 5) <= 0) {
        printf("  " RED "Failed to fetch history" RESET "\n");
        sleep(2);
        return;
    }

    clear_screen();
    printf("  ══════════════════ MATCH HISTORY ══════════════════\n");
    printf("  %-6s %-16s %-6s %-6s\n", "Time", "Opponent", "Res", "XP");
    printf("  ──────────────────────────────────────────────────\n");

    // Parse pesan history yang diterima
    char *entry = resp.data;
    char *tok;
    while ((tok = strsep(&entry, ";")) != NULL && *tok) {
        char opp[MAX_USERNAME];
        int result, xp, hh, mm;
        if (sscanf(tok, "%31[^|]|%d|%d|%d|%d", opp, &result, &xp, &hh, &mm) == 5) {
            printf("  %02d:%02d  %-16s %-6s +%d XP\n",
                   hh, mm, opp,
                   result ? GREEN "WIN" RESET : RED "LOSS" RESET,
                   xp);
        }
    }

    if (resp.int_data == 0) {
        printf("  (no history yet)\n");
    }

    printf("\n  Press any key...");
    fflush(stdout);
    getchar(); getchar();
}
```
`armory()` akan menampilkan gold yang dimiliki dan senjata yang di sediakan berserta harga dan buff nya. Transaksi dimulali dengan mengirim pesan `MSG_BUY_WEAPON`, lalu server akan mengecek di Shared Memory apakah Gold pemain cukup dan player akan menunggu jawaban  melalui `recv_from_server`. <br>

`history()` mengirim pesan `MSG_VIEW_HISTORY` beserta `current_user` ke server. lalu memisahkan entri dengan perintah `strep` dan `sscanf` untuk memisahkan atribut, sehingga data mentah yang diterima dari server dapat di sajikan kembali. <br>
### Manajemen sumber daya
```c
void client_cleanup() {
    if (is_logged_in) {
        send_to_server(MSG_LOGOUT, current_user, NULL, NULL, 0);
    }
    if (shm) shmdt(shm);
    disable_raw_mode();
}
void sig_handler(int sig) {
    (void)sig;
    printf("\n  [ETERNAL] Disconnecting...\n");
    client_cleanup();
    exit(0);
}
```
`client_cleanup` memutuskan hubungan antara ruang alamat proses Client dengan blok memori bersama. `sig_handler` merupakan interupsi untuk menangani kejadian tak terduga. <br>
### Fungsi Main
```c
int main() {
    signal(SIGINT,  sig_handler);
    signal(SIGTERM, sig_handler);

    massage_queue_id = msgget(MSG_KEY, 0666);
    // Jika server belum berjalan
    if (massage_queue_id < 0) {
        fprintf(stderr, "Orion are you there?\n");
        fprintf(stderr, "(Server not running. Start orion first!)\n");
        exit(1);
    }

    // Attach shared memory (read-only untuk display, tidak write langsung)
    shared_memory_id = shmget(SHARED_MEMORY_KEY, sizeof(SharedData), 0666);
    if (shared_memory_id >= 0) {
        shm = (SharedData*)shmat(shared_memory_id, NULL, SHM_RDONLY);
        if (shm == (SharedData*)-1) shm = NULL;
    }

    // Cek koneksi ke server
    if (!check_server()) {
        fprintf(stderr, "Orion are you there?\n");
        fprintf(stderr, "(Server not responding!)\n");
        exit(1);
    }

    while (1) {
        if (!is_logged_in) {
            show_main_menu();

            int choice;
            scanf("%d", &choice);

            switch (choice) {
                case 1:
                    do_register();
                    break;
                case 2:
                    if (login()) {
                    }
                    break;
                case 3:
                    printf("\n  Goodbye, warrior!\n");
                    client_cleanup();
                    return 0;
                default:
                    break;
            }
        } else {
            // masuk dunia eterion
            show_game_menu();

            int choice;
            scanf("%d", &choice);

            switch (choice) {
                case 1:
                    matchmaking();
                    break;
                case 2:
                    armory();
                    break;
                case 3:
                    history();
                    break;
                case 4: //logout
                    send_to_server(MSG_LOGOUT, current_user, NULL, NULL, 0);
                    Message resp;
                    recv_from_server(&resp, 3);
                    is_logged_in = 0;
                    memset(current_user, 0, sizeof(current_user));
                    printf("  " YELLOW "Logged out." RESET "\n");
                    sleep(1);
                    break;
                default:
                    break;
            }
        }
    }

    client_cleanup();
    return 0;
}
```
Client tidak membuat IPC resource baru, ia hanya membuka resource yang sudah dibuat server. Client (player) hanya di beri akses ke Shared Memory sebagai `SHM_RDONLY` (hanya baca), untuk memastikan client tidak dapat memodifikasi data global secara ilegal dan hanya mengandalkan Message Queue untuk mengirimkan permintaan perubahan data ke server. <br>
## orion.c



## Kode Program 
kode program dapat secara lengkap dilihat pada [soal2]() <br>


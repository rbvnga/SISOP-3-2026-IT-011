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
Setelah dibuat dan dihubungkan ke server, program masuk ke loop tak terbatas. Setiap iterasi dimulai dengan `select()` yang memblokir program sambil menunggu — ia akan "terbangun" hanya jika ada data dari server atau ada input keyboard dari client. Karena keduanya dipantau sekaligus, tidak ada situasi di mana menunggu server membuat keyboard tidak responsif, dan sebaliknya. <br>
### wired.c
```c

```
## Soal 2
### Deskripsi Permasalahan 
Seorang panglima tempur harus membuat arena pertempuran yang terintegrasi. `arena.h` akan menjadi tempat bagaimana pertempuran dimulai, semua srategi dan sistem pertempuran ada disini, `orion.c` akan bertindak sebagi server, dan `eternal.c` akan bertindak sebagi client. <br>

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
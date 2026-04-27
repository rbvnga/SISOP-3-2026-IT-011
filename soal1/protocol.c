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
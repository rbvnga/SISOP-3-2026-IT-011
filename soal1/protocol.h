#ifndef PROTOCOL_H
#define PROTOCOL_H

#define MAX_CLIENTS 10
#define BUFFER_SIZE 1024

// Format pesan user
void format_message(char *output, const char *name, const char *msg);

// Logging
void log_event(const char *role, const char *msg);

// Cek username
int cek_nama(char names[][50], int count, const char *name);

#endif
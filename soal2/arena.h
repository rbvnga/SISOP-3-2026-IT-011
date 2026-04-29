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
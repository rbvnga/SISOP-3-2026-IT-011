#include "arena.h"

int shared_memory_id = -1;
int massage_queue_id = -1;
int semaphore_id = -1;
SharedData *shm = NULL; // Pointer ke Shared Memory 

// Antrian matchmaking 
typedef struct {
    pid_t pid;
    char  username[MAX_USERNAME];
    time_t join_time;
} MMEntry;

MMEntry mm_queue[MAX_PLAYERS];
int mm_count = 0; // Penghitung jumlah pemain yang ada dalam antrean saat ini
//memastikan hanya satu thread yang bisa menambah atau mengurangi pemain dari mm_queue dalam satu waktu
pthread_mutex_t mm_mutex = PTHREAD_MUTEX_INITIALIZER; 

void sem_wait_op(int semaphore_id) {
    struct sembuf op = {0, -1, SEM_UNDO};
    semop(semaphore_id, &op, 1);
}

void sem_signal_op(int semaphore_id) {
    struct sembuf op = {0, +1, SEM_UNDO};
    semop(semaphore_id, &op, 1);
}

 // cari player berdasarkan user name 
Player* find_player(const char *username) {
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (shm->players[i].is_registered &&
            strcmp(shm->players[i].username, username) == 0) {
            return &shm->players[i];
        }
    }
    return NULL;
}
// mencari slot kosong untuk player baru
Player* find_empty_slot() {
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (!shm->players[i].is_registered) {
            return &shm->players[i];
        }
    }
    return NULL;
}

// mencari player berdasarkan PID
Player* find_player_by_pid(pid_t pid) {
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (shm->players[i].is_registered &&
            shm->players[i].is_logged_in &&
            shm->players[i].pid == pid) {
            return &shm->players[i];
        }
    }
    return NULL;
}

// Kirim response ke client berdasarkan pid nya
void send_response(pid_t client_pid, int success, const char *data) {
    Message resp;
    memset(&resp, 0, sizeof(resp));
    resp.msg_type = (long)client_pid;
    resp.action = MSG_RESPONSE;
    resp.success = success;
    if (data) strncpy(resp.data, data, sizeof(resp.data) - 1);
    msgsnd(massage_queue_id, &resp, sizeof(resp) - sizeof(long), 0);
}

void handle_register(Message *msg) {
    sem_wait_op(semaphore_id); // masuk critical section (lock)

    // Cek username sudah ada atau belum
    Player *existing = find_player(msg->username);
    if (existing) {
        sem_signal_op(semaphore_id); //keluar critical section (unlock)
        send_response(msg->sender_pid, 0, "Username already taken!");
        return;
    }

    // jika sudah tidak ada slot yang tersedia
    Player *slot = find_empty_slot();
    if (!slot) {
        sem_signal_op(semaphore_id); //keluar critical section (unlock)
        send_response(msg->sender_pid, 0, "Server full!");
        return;
    }

    // Inisialisasi player baru dengan default stats
    memset(slot, 0, sizeof(Player));
    strncpy(slot->username, msg->username, MAX_USERNAME - 1);
    strncpy(slot->password, msg->password, MAX_PASSWORD - 1);
    slot->gold = BASE_GOLD;
    slot->lvl = BASE_LVL;
    slot->xp = BASE_XP;
    slot->weapon_index = -1; // belum ada senjata
    slot->is_registered = 1;
    slot->in_battle = 0;
    slot->in_matchmaking= 0;
    slot->is_logged_in  = 0;
    slot->history_count = 0;

    shm->player_count++;

    sem_signal_op(semaphore_id); //keluar critical section (unlock)
    send_response(msg->sender_pid, 1, "Registration successful!");
    printf("[ORION] New player registered: %s\n", msg->username);
}

void handle_login(Message *msg) {
    sem_wait_op(semaphore_id);

    // validasi username
    Player *p = find_player(msg->username);
    if (!p) {
        sem_signal_op(semaphore_id);
        send_response(msg->sender_pid, 0, "Username not found!");
        return;
    }

    // validasi password
    if (strcmp(p->password, msg->password) != 0) {
        sem_signal_op(semaphore_id);
        send_response(msg->sender_pid, 0, "Wrong password!");
        return;
    }

    // Cek apakah sudah login di sesi lain
    if (p->is_logged_in) {
        sem_signal_op(semaphore_id);
        send_response(msg->sender_pid, 0, "Account already in use!");
        return;
    }

    p->is_logged_in = 1;
    p->pid = msg->sender_pid;

    // kirim data player
    char data[256];
    snprintf(data, sizeof(data), "%s|%d|%d|%d|%d",
             p->username, p->lvl, p->gold, p->xp, p->weapon_index);

    sem_signal_op(semaphore_id);

    Message resp;
    memset(&resp, 0, sizeof(resp));
    resp.msg_type = (long)msg->sender_pid;
    resp.action = MSG_RESPONSE;
    resp.success = 1;
    strncpy(resp.username, p->username, MAX_USERNAME - 1);
    strncpy(resp.data, data, sizeof(resp.data) - 1);
    msgsnd(massage_queue_id, &resp, sizeof(resp) - sizeof(long), 0);

    printf("[ORION] Player logged in: %s (PID: %d)\n", msg->username, msg->sender_pid);
}

void handle_logout(Message *msg) {
    sem_wait_op(semaphore_id);

    Player *p = find_player_by_pid(msg->sender_pid);
    if (p) {
        // Jika sedang dalam matchmaking, keluarkan dulu 
        p->in_matchmaking = 0;
        p->is_logged_in = 0;
        p->pid = 0;
    }

    sem_signal_op(semaphore_id);
    send_response(msg->sender_pid, 1, "Logged out.");
}

void handle_buy_weapon(Message *msg) {
    int weapon_idx = msg->int_data;

    // jika weapon yang di pilih diluar list atau melebihi kapasitas 
    if (weapon_idx < 0 || weapon_idx >= MAX_WEAPONS) {
        send_response(msg->sender_pid, 0, "Invalid weapon!");
        return;
    }

    sem_wait_op(semaphore_id);

    Player *p = find_player_by_pid(msg->sender_pid);
    if (!p) {
        sem_signal_op(semaphore_id);
        send_response(msg->sender_pid, 0, "Not logged in!");
        return;
    }

    int price = WEAPON_LIST[weapon_idx].price;
    // jika gold yang dimiliki tidak cukup
    if (p->gold < price) {
        sem_signal_op(semaphore_id);
        send_response(msg->sender_pid, 0, "Not enough gold!");
        return;
    }

    // Sistem otomatis pakai senjata damage terbesar
    int current_bonus = (p->weapon_index >= 0) ? WEAPON_LIST[p->weapon_index].bonus_damage : 0;
    if (WEAPON_LIST[weapon_idx].bonus_damage > current_bonus) {
        p->weapon_index = weapon_idx;
    }
    // menghitung jumlah gold setelah membeli
    p->gold -= price;

    char data[128];
    snprintf(data, sizeof(data), "Bought %s! Gold: %d",
             WEAPON_LIST[weapon_idx].name, p->gold);

    sem_signal_op(semaphore_id);
    // kirim pesan balasan
    send_response(msg->sender_pid, 1, data);
}

void handle_view_history(Message *msg) {
    sem_wait_op(semaphore_id);

    Player *p = find_player_by_pid(msg->sender_pid);
    if (!p) {
        sem_signal_op(semaphore_id);
        send_response(msg->sender_pid, 0, "Not logged in!");
        return;
    }

    Message resp;
    memset(&resp, 0, sizeof(resp));
    resp.msg_type = (long)msg->sender_pid;
    resp.action = MSG_VIEW_HISTORY;
    resp.success = 1;
    // cek jumlah pertandingan yang pernah dilakukan 
    resp.int_data = p->history_count;

    char buf[256] = "";
    int  n = p->history_count;
    // Kirim maksimal 10 history terakhir per pesan 
    int start = (n > 10) ? n - 10 : 0;
    for (int i = start; i < n; i++) {
        char entry[64];
        snprintf(entry, sizeof(entry), "%s|%d|%d|%d|%d;",
                 p->history[i].opponent,
                 p->history[i].result,
                 p->history[i].xp_gained,
                 p->history[i].hour,
                 p->history[i].minute);
        strncat(buf, entry, sizeof(buf) - strlen(buf) - 1);
    }
    strncpy(resp.data, buf, sizeof(resp.data) - 1);

    sem_signal_op(semaphore_id);
    // kirim pesan
    msgsnd(massage_queue_id, &resp, sizeof(resp) - sizeof(long), 0);
}

// Argumen yang diteruskan ke thread battle
typedef struct {
    pid_t pid_a;
    pid_t pid_b;
    char name_a[MAX_USERNAME];
    char name_b[MAX_USERNAME];
    int is_bot_b; // Cek apakah player b bot (== 1)
} BattleArgs;

// Kirim battle update ke kedua client
void send_battle_update(pid_t pid_a, pid_t pid_b,
                        int hp_a, int max_hp_a,
                        int hp_b, int max_hp_b,
                        char logs[MAX_LOG][128],
                        const char *name_a, const char *name_b,
                        int damage_a, int damage_b,
                        int cd_a, int cd_b,
                        int ulti_a, int ulti_b) {
    char buf[512];
    snprintf(buf, sizeof(buf),
             "%s|%d|%d|%s|%d|%d|%s|%s|%s|%s|%s|%d|%d|%d|%d|%d|%d",
             name_a, hp_a, max_hp_a,
             name_b, hp_b, max_hp_b,
             logs[0], logs[1], logs[2], logs[3], logs[4],
             damage_a, damage_b, cd_a, cd_b, ulti_a, ulti_b);

    // Kirim ke player A
    Message m;
    memset(&m, 0, sizeof(m));
    m.msg_type  = (long)pid_a;
    m.action = MSG_BATTLE_UPDATE;
    strncpy(m.data, buf, sizeof(m.data) - 1);
    msgsnd(massage_queue_id, &m, sizeof(m) - sizeof(long), 0);

    // Kirim ke player B, jika bukan bot
    if (pid_b != 0) {
        m.msg_type = (long)pid_b;
        msgsnd(massage_queue_id, &m, sizeof(m) - sizeof(long), 0);
    }
}

void* battle_thread(void *arg) {
    BattleArgs *ba = (BattleArgs*)arg;

    pid_t pid_a  = ba->pid_a;
    pid_t pid_b  = ba->pid_b;
    int is_bot   = ba->is_bot_b;
    char name_a[MAX_USERNAME], name_b[MAX_USERNAME];
    strncpy(name_a, ba->name_a, MAX_USERNAME - 1);
    strncpy(name_b, ba->name_b, MAX_USERNAME - 1);
    free(ba);

    //Ambil data player dari shared memory
    sem_wait_op(semaphore_id);
    Player *pa = find_player(name_a);
    Player *pb = is_bot ? NULL : find_player(name_b);

    int hp_a     = hitung_max_hp(pa);
    int max_hp_a = hp_a;
    int damage_a    = hitung_damage(pa);
    int ulti_a    = hitung_ultimate(pa);

    int hp_b, max_hp_b, damage_b, ulti_b;
    if (is_bot) {
        // jika bot
        hp_b = BASE_HEALTH;
        max_hp_b = BASE_HEALTH;
        damage_b = BASE_DAMAGE;
        ulti_b = BASE_DAMAGE * 3;
    } else {
        hp_b = hitung_max_hp(pb);
        max_hp_b = hp_b;
        damage_b = hitung_damage(pb);
        ulti_b = hitung_ultimate(pb);
    }

    pa->in_battle     = 1;
    pa->hp            = hp_a;
    pa->opponent_pid  = pid_b;
    if (!is_bot && pb) {
        pb->in_battle    = 1;
        pb->hp           = hp_b;
        pb->opponent_pid = pid_a;
    }
    sem_signal_op(semaphore_id);

    // Cooldown tracking (dalam millisecond) 
    struct timespec last_atk_a = {0}, last_atk_b = {0};
    struct timespec last_ult_a = {0}, last_ulti_b = {0};
    clock_gettime(CLOCK_MONOTONIC, &last_atk_a);
    last_atk_b = last_atk_a;
    last_ult_a = last_atk_a;
    last_ulti_b = last_atk_a;

    // Combat log ring buffer
    char logs[MAX_LOG][128];
    for (int i = 0; i < MAX_LOG; i++) memset(logs[i], 0, 128);
    int log_idx = 0;

    // Kirim state awal ke kedua player
    send_battle_update(pid_a, is_bot ? 0 : pid_b,
                       hp_a, max_hp_a, hp_b, max_hp_b,
                       logs, name_a, name_b,
                       damage_a, damage_b, 0, 0, ulti_a, ulti_b);

    long mtype_a = BATTLE_MTYPE_BASE + (long)pid_a;
    long mtype_b = (!is_bot) ? (BATTLE_MTYPE_BASE + (long)pid_b) : 0;

    Message attack_msg;
    int battle_active = 1;
    int winner = 0;

    // Untuk bot: interval serangan acak 1-3 detik 
    struct timespec bot_last_atk;
    clock_gettime(CLOCK_MONOTONIC, &bot_last_atk);
    int bot_interval = 1 + (rand() % 3);

    while (battle_active) {
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);

        //Proses serangan dari Player A 
        memset(&attack_msg, 0, sizeof(attack_msg));
        if (msgrcv(massage_queue_id, &attack_msg, sizeof(attack_msg) - sizeof(long),
                   mtype_a, IPC_NOWAIT) > 0) {

            double elapsed_a = (now.tv_sec - last_atk_a.tv_sec) +
                               (now.tv_nsec - last_atk_a.tv_nsec) / 1e9;
            int updated = 0;

            if (attack_msg.action == MSG_ATTACK && elapsed_a >= 1.0) {
                hp_b -= damage_a;
                last_atk_a = now;
                snprintf(logs[log_idx % MAX_LOG], 128,
                         "> %s hit for %d dmg!", name_a, damage_a);
                log_idx++;
                updated = 1;

            } else if (attack_msg.action == MSG_ULTIMATE) {
                sem_wait_op(semaphore_id);
                Player *att = find_player(name_a);
                int has_wpn = (att && att->weapon_index >= 0);
                sem_signal_op(semaphore_id);
                double ue = (now.tv_sec - last_ult_a.tv_sec) +
                            (now.tv_nsec - last_ult_a.tv_nsec) / 1e9;
                if (has_wpn && ue >= 1.0) {
                    hp_b -= ulti_a;
                    last_ult_a = now;
                    snprintf(logs[log_idx % MAX_LOG], 128,
                             "> ULTIMATE %s! %d dmg!", name_a, ulti_a);
                    log_idx++;
                    updated = 1;
                }
            }

            if (updated) {
                if (hp_a <= 0) { hp_a = 0; battle_active = 0; winner = 2; }
                if (hp_b <= 0) { hp_b = 0; battle_active = 0; winner = 1; }
                char ordered[MAX_LOG][128];
                for (int i = 0; i < MAX_LOG; i++) {
                    int src = (log_idx - 1 - i + MAX_LOG * 10) % MAX_LOG;
                    strncpy(ordered[i], logs[src], 127);
                }
                double cda = 1.0 - ((now.tv_sec - last_atk_a.tv_sec) +
                             (now.tv_nsec - last_atk_a.tv_nsec) / 1e9);
                double cdb = 1.0 - ((now.tv_sec - last_atk_b.tv_sec) +
                             (now.tv_nsec - last_atk_b.tv_nsec) / 1e9);
                if (cda < 0) cda = 0;
                if (cdb < 0) cdb = 0;
                send_battle_update(pid_a, is_bot ? 0 : pid_b,
                                   hp_a, max_hp_a, hp_b, max_hp_b,
                                   ordered, name_a, name_b, damage_a, damage_b,
                                   (int)(cda*10), (int)(cdb*10), ulti_a, ulti_b);
            }
        }

        // Proses serangan dari Player B (jika bukan bot) 
        if (!is_bot && battle_active) {
            memset(&attack_msg, 0, sizeof(attack_msg));
            if (msgrcv(massage_queue_id, &attack_msg, sizeof(attack_msg) - sizeof(long),
                       mtype_b, IPC_NOWAIT) > 0) {

                double elapsed_b = (now.tv_sec - last_atk_b.tv_sec) +
                                   (now.tv_nsec - last_atk_b.tv_nsec) / 1e9;
                int updated = 0;

                if (attack_msg.action == MSG_ATTACK && elapsed_b >= 1.0) {
                    hp_a -= damage_b;
                    last_atk_b = now;
                    snprintf(logs[log_idx % MAX_LOG], 128,
                             "> %s hit for %d dmg!", name_b, damage_b);
                    log_idx++;
                    updated = 1;

                } else if (attack_msg.action == MSG_ULTIMATE) {
                    sem_wait_op(semaphore_id);
                    Player *att = find_player(name_b);
                    int has_wpn = (att && att->weapon_index >= 0);
                    sem_signal_op(semaphore_id);
                    double ue = (now.tv_sec - last_ulti_b.tv_sec) +
                                (now.tv_nsec - last_ulti_b.tv_nsec) / 1e9;
                    if (has_wpn && ue >= 1.0) {
                        hp_a -= ulti_b;
                        last_ulti_b = now;
                        snprintf(logs[log_idx % MAX_LOG], 128,
                                 "> ULTIMATE %s! %d dmg!", name_b, ulti_b);
                        log_idx++;
                        updated = 1;
                    }
                }

                if (updated) {
                    if (hp_a <= 0) { hp_a = 0; battle_active = 0; winner = 2; }
                    if (hp_b <= 0) { hp_b = 0; battle_active = 0; winner = 1; }
                    char ordered[MAX_LOG][128];
                    for (int i = 0; i < MAX_LOG; i++) {
                        int src = (log_idx - 1 - i + MAX_LOG * 10) % MAX_LOG;
                        strncpy(ordered[i], logs[src], 127);
                    }
                    double cda = 1.0 - ((now.tv_sec - last_atk_a.tv_sec) +
                                 (now.tv_nsec - last_atk_a.tv_nsec) / 1e9);
                    double cdb = 1.0 - ((now.tv_sec - last_atk_b.tv_sec) +
                                 (now.tv_nsec - last_atk_b.tv_nsec) / 1e9);
                    if (cda < 0) cda = 0;
                    if (cdb < 0) cdb = 0;
                    send_battle_update(pid_a, pid_b,
                                       hp_a, max_hp_a, hp_b, max_hp_b,
                                       ordered, name_a, name_b, damage_a, damage_b,
                                       (int)(cda*10), (int)(cdb*10), ulti_a, ulti_b);
                }
            }
        }

        // Bot attack 
        if (is_bot && battle_active) {
            double bot_elapsed = (now.tv_sec  - bot_last_atk.tv_sec) +
                                 (now.tv_nsec - bot_last_atk.tv_nsec) / 1e9;
            if (bot_elapsed >= bot_interval) {
                hp_a -= damage_b;
                if (hp_a < 0) hp_a = 0;
                bot_last_atk = now;
                bot_interval = 1 + (rand() % 3);
                snprintf(logs[log_idx % MAX_LOG], 128,
                         "> %s attacks you for %d!", name_b, damage_b);
                log_idx++;

                if (hp_a <= 0) { battle_active = 0; winner = 2; }

                char ordered[MAX_LOG][128];
                for (int i = 0; i < MAX_LOG; i++) {
                    int src = (log_idx - 1 - i + MAX_LOG * 10) % MAX_LOG;
                    strncpy(ordered[i], logs[src], 127);
                }
                send_battle_update(pid_a, 0,
                                   hp_a, max_hp_a, hp_b, max_hp_b,
                                   ordered, name_a, name_b,
                                   damage_a, damage_b, 0, 0, ulti_a, ulti_b);
            }
        }
        usleep(10000);
    }

    // battle selesai, update state kedua player
    sem_wait_op(semaphore_id);

    time_t t = time(NULL);
    struct tm *tm_info = localtime(&t);

    // Update Player A
    Player *pa2 = find_player(name_a);
    if (pa2) {
        int xp_gained   = (winner == 1) ? XP_WIN : XP_LOSS;
        int gold_gained = (winner == 1) ? GOLD_WIN : GOLD_LOSS;
        pa2->xp   += xp_gained;
        pa2->gold += gold_gained;

        // Level up: bertambah setiap kelipatan 100 XP, XP tidak direset
        pa2->lvl = 1 + (pa2->xp / 100);

        // Simpan ke history 
        if (pa2->history_count < MAX_HISTORY) {
            MatchRecord *rec = &pa2->history[pa2->history_count++];
            strncpy(rec->opponent, name_b, MAX_USERNAME - 1);
            rec->result    = (winner == 1) ? 1 : 0;
            rec->xp_gained = xp_gained;
            rec->hour      = tm_info->tm_hour;
            rec->minute    = tm_info->tm_min;
        }

        pa2->in_battle    = 0;
        pa2->opponent_pid = 0;
    }

    // Update Player B 
    if (!is_bot) {
        Player *pb2 = find_player(name_b);
        if (pb2) {
            int xp_gained   = (winner == 2) ? XP_WIN : XP_LOSS;
            int gold_gained = (winner == 2) ? GOLD_WIN : GOLD_LOSS;
            pb2->xp   += xp_gained;
            pb2->gold += gold_gained;
            pb2->lvl   = 1 + (pb2->xp / 100);

            if (pb2->history_count < MAX_HISTORY) {
                MatchRecord *rec = &pb2->history[pb2->history_count++];
                strncpy(rec->opponent, name_a, MAX_USERNAME - 1);
                rec->result    = (winner == 2) ? 1 : 0;
                rec->xp_gained = xp_gained;
                rec->hour      = tm_info->tm_hour;
                rec->minute    = tm_info->tm_min;
            }

            pb2->in_battle    = 0;
            pb2->opponent_pid = 0;
        }
    }

    sem_signal_op(semaphore_id);

    // Kirim hasil ke player A
    Message end_msg;
    memset(&end_msg, 0, sizeof(end_msg));
    end_msg.msg_type    = (long)pid_a;
    end_msg.action   = MSG_BATTLE_END;
    end_msg.success  = (winner == 1) ? 1 : 0;
    end_msg.int_data = (winner == 1) ? XP_WIN : XP_LOSS;
    snprintf(end_msg.data, sizeof(end_msg.data), "%s",
             (winner == 1) ? "VICTORY" : "DEFEAT");
    msgsnd(massage_queue_id, &end_msg, sizeof(end_msg) - sizeof(long), 0);

    // Kirim hasil ke client B (jika bukan bot) 
    if (!is_bot) {
        end_msg.msg_type   = (long)pid_b;
        end_msg.success = (winner == 2) ? 1 : 0;
        end_msg.int_data= (winner == 2) ? XP_WIN : XP_LOSS;
        snprintf(end_msg.data, sizeof(end_msg.data), "%s",
                 (winner == 2) ? "VICTORY" : "DEFEAT");
        msgsnd(massage_queue_id, &end_msg, sizeof(end_msg) - sizeof(long), 0);
    }

    printf("[ORION] Battle ended: %s vs %s -> Winner: Player %d\n",
           name_a, name_b, winner);
    return NULL;
}


void* matchmaking_thread(void *arg) {
    (void)arg;
    printf("[ORION] Matchmaking thread started\n");

    while (1) {
        sleep(1);
        pthread_mutex_lock(&mm_mutex);
        time_t now = time(NULL);

        // Scan antrian: cari yang sudah timeout 
        for (int i = 0; i < mm_count; i++) {
            if (difftime(now, mm_queue[i].join_time) >= MATCHMAKING_TIMEOUT) {
                // Lawan bot 
                printf("[ORION] %s vs BOT (timeout)\n", mm_queue[i].username);

                // Set status player tidak lagi matchmaking 
                sem_wait_op(semaphore_id);
                Player *p = find_player(mm_queue[i].username);
                if (p) p->in_matchmaking = 0;
                sem_signal_op(semaphore_id);

                // Buat argumen battle 
                BattleArgs *ba = malloc(sizeof(BattleArgs));
                ba->pid_a    = mm_queue[i].pid;
                ba->pid_b    = 0;
                ba->is_bot_b = 1;
                strncpy(ba->name_a, mm_queue[i].username, MAX_USERNAME - 1);
                strncpy(ba->name_b, "Wild Beast", MAX_USERNAME - 1);

                // Beritahu client bahwa lawan ditemukan (bot) 
                Message notif;
                memset(&notif, 0, sizeof(notif));
                notif.msg_type  = (long)mm_queue[i].pid;
                notif.action = MSG_MATCHMAKING;
                notif.success = 1;
                snprintf(notif.data, sizeof(notif.data), "BOT|Wild Beast");
                msgsnd(massage_queue_id, &notif, sizeof(notif) - sizeof(long), 0);

                // Spawn battle thread 
                pthread_t bt;
                pthread_create(&bt, NULL, battle_thread, ba);
                pthread_detach(bt);

                // Hapus dari antrian 
                mm_queue[i] = mm_queue[--mm_count];
                i--;
                continue;
            }
        }

        // Coba pasangkan 2 player
        if (mm_count >= 2) {
            // Cari 2 player yang keduanya masih valid (belum battle) 
            int idx_a = -1, idx_b = -1;
            for (int i = 0; i < mm_count && idx_a < 0; i++) {
                sem_wait_op(semaphore_id);
                Player *p = find_player(mm_queue[i].username);
                int valid = (p && p->in_matchmaking && !p->in_battle);
                sem_signal_op(semaphore_id);
                if (valid) idx_a = i;
            }
            for (int i = 0; i < mm_count && idx_b < 0; i++) {
                if (i == idx_a) continue;
                sem_wait_op(semaphore_id);
                Player *p = find_player(mm_queue[i].username);
                int valid = (p && p->in_matchmaking && !p->in_battle);
                sem_signal_op(semaphore_id);
                if (valid) idx_b = i;
            }

            if (idx_a >= 0 && idx_b >= 0) {
                MMEntry ea = mm_queue[idx_a];
                MMEntry eb = mm_queue[idx_b];

                // Hapus keduanya dari antrian 
                int hi = (idx_a > idx_b) ? idx_a : idx_b;
                int lo = (idx_a < idx_b) ? idx_a : idx_b;
                mm_queue[hi] = mm_queue[--mm_count];
                mm_queue[lo] = mm_queue[--mm_count];

                // Update status
                sem_wait_op(semaphore_id);
                Player *pa = find_player(ea.username);
                Player *pb = find_player(eb.username);
                if (pa) pa->in_matchmaking = 0;
                if (pb) pb->in_matchmaking = 0;
                sem_signal_op(semaphore_id);

                printf("[ORION] Match found: %s vs %s\n", ea.username, eb.username);

                // Beritahu kedua player
                Message notif;
                memset(&notif, 0, sizeof(notif));
                notif.msg_type  = (long)ea.pid;
                notif.action = MSG_MATCHMAKING;
                notif.success = 1;
                snprintf(notif.data, sizeof(notif.data), "PLAYER|%s", eb.username);
                msgsnd(massage_queue_id, &notif, sizeof(notif) - sizeof(long), 0);

                notif.msg_type  = (long)eb.pid;
                snprintf(notif.data, sizeof(notif.data), "PLAYER|%s", ea.username);
                msgsnd(massage_queue_id, &notif, sizeof(notif) - sizeof(long), 0);

                // Spawn battle thread 
                BattleArgs *ba = malloc(sizeof(BattleArgs));
                ba->pid_a    = ea.pid;
                ba->pid_b    = eb.pid;
                ba->is_bot_b = 0;
                strncpy(ba->name_a, ea.username, MAX_USERNAME - 1);
                strncpy(ba->name_b, eb.username, MAX_USERNAME - 1);

                pthread_t bt;
                pthread_create(&bt, NULL, battle_thread, ba);
                pthread_detach(bt);
            }
        }

        pthread_mutex_unlock(&mm_mutex);
    }
    return NULL;
}

void handle_matchmaking(Message *msg) {
    sem_wait_op(semaphore_id);
    Player *p = find_player_by_pid(msg->sender_pid);
    if (!p || p->in_battle) {
        sem_signal_op(semaphore_id);
        send_response(msg->sender_pid, 0, "Cannot join matchmaking!");
        return;
    }
    p->in_matchmaking = 1;
    char uname[MAX_USERNAME];
    strncpy(uname, p->username, MAX_USERNAME - 1);
    sem_signal_op(semaphore_id);

    // Tambahkan ke antrian matchmaking 
    pthread_mutex_lock(&mm_mutex);

    // Cek apakah sudah di antrian 
    for (int i = 0; i < mm_count; i++) {
        if (mm_queue[i].pid == msg->sender_pid) {
            pthread_mutex_unlock(&mm_mutex);
            send_response(msg->sender_pid, 0, "Already in queue!");
            return;
        }
    }

    if (mm_count >= MAX_PLAYERS) {
        pthread_mutex_unlock(&mm_mutex);
        send_response(msg->sender_pid, 0, "Queue full!");
        return;
    }

    mm_queue[mm_count].pid = msg->sender_pid;
    mm_queue[mm_count].join_time = time(NULL);
    strncpy(mm_queue[mm_count].username, uname, MAX_USERNAME - 1);
    mm_count++;

    pthread_mutex_unlock(&mm_mutex);

    // Kirim konfirmasi masuk antrian 
    send_response(msg->sender_pid, 1, "Searching...");
}

// membersihkan IPC resources, saat server mati
void cleanup(int sig) {
    (void)sig;
    printf("\n[ORION] Shutting down, cleaning up IPC...\n");
    if (shared_memory_id != -1) {
        shmdt(shm);
        shmctl(shared_memory_id, IPC_RMID, NULL);
    }
    if (massage_queue_id != -1) msgctl(massage_queue_id, IPC_RMID, NULL);
    if (semaphore_id != -1) semctl(semaphore_id, 0, IPC_RMID);
    exit(0);
}

int main() {
    srand(time(NULL));
    signal(SIGINT,  cleanup);
    signal(SIGTERM, cleanup);
    printf("╔══════════════════════════════════╗\n");
    printf("║     ORION - Battle Eterion       ║\n");
    printf("║         Server Starting...       ║\n");
    printf("╚══════════════════════════════════╝\n");

    shared_memory_id = shmget(SHARED_MEMORY_KEY, sizeof(SharedData), IPC_CREAT | 0666);
    if (shared_memory_id < 0) {
        perror("[ORION] shmget failed");
        exit(1);
    }

    shm = (SharedData*)shmat(shared_memory_id, NULL, 0);
    if (shm == (SharedData*)-1) {
        perror("[ORION] shmat failed");
        exit(1);
    }

    // Inisialisasi data hanya jika baru dibuat 
    if (shm->player_count < 0 || shm->player_count > MAX_PLAYERS) {
        memset(shm, 0, sizeof(SharedData));
        printf("[ORION] Shared memory initialized (fresh start)\n");
    } else {
        // server restrat
        for (int i = 0; i < MAX_PLAYERS; i++) {
            shm->players[i].is_logged_in   = 0;
            shm->players[i].in_battle      = 0;
            shm->players[i].in_matchmaking = 0;
        }
        printf("[ORION] Shared memory loaded (%d players)\n", shm->player_count);
    }

     // inisialisasi massage queue
    massage_queue_id = msgget(MSG_KEY, IPC_CREAT | 0666);
    if (massage_queue_id < 0) {
        perror("[ORION] msgget failed");
        cleanup(0);
    }

    // inisialisai semaphore
    semaphore_id = semget(SEM_KEY, 1, IPC_CREAT | 0666);
    if (semaphore_id < 0) {
        perror("[ORION] semget failed");
        cleanup(0);
    }

    union semun su;
    su.val = 1; // Inisialisasi nilai semaphore ke 1 (unlocked) 
    semctl(semaphore_id, 0, SETVAL, su);

    // start matchmaking thread
    pthread_t mm_tid;
    pthread_create(&mm_tid, NULL, matchmaking_thread, NULL);
    pthread_detach(mm_tid);

    printf("[ORION] Orion is ready (PID: %d)\n", getpid());
    printf("[ORION] Waiting for connections...\n\n");

    Message msg;
    while (1) {
        memset(&msg, 0, sizeof(msg));

        // tunggu sampai ada pesan dengan msg_type=1
        if (msgrcv(massage_queue_id, &msg, sizeof(msg) - sizeof(long), 1, 0) < 0) {
            if (errno == EINTR) continue;
            perror("[ORION] msgrcv failed");
            continue;
        }

        printf("[ORION] Received action %d from PID %d\n",
               msg.action, msg.sender_pid);

        switch (msg.action) {
            case MSG_PING:
                send_response(msg.sender_pid, 1, "PONG");
                break;
            case MSG_REGISTER:
                handle_register(&msg);
                break;
            case MSG_LOGIN:
                handle_login(&msg);
                break;
            case MSG_LOGOUT:
                handle_logout(&msg);
                break;
            case MSG_MATCHMAKING:
                handle_matchmaking(&msg);
                break;
            case MSG_BUY_WEAPON:
                handle_buy_weapon(&msg);
                break;
            case MSG_VIEW_HISTORY:
                handle_view_history(&msg);
                break;
            default:
                break;
        }
    }

    cleanup(0);
    return 0;
}
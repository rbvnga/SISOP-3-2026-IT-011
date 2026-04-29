#include "arena.h"
#include <termios.h> // program membaca input saat tombol ditekan tanpa perlu Enter
#include <fcntl.h>

// Global state client 
int massage_queue_id = -1; // sama dengan server
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

static struct termios original_termios;

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

// membersihkan isi layar terminal
void clear_screen() {
    printf("\033[2J\033[H");
}

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
    printf("  ______ _______ ______ _____  _____ ____  _   _ \n");
    printf(" |  ____|__   __|  ____|  __ \\|_   _/ __ \\| \\ | |\n");
    printf(" | |__     | |  | |__  | |__) | | || |  | |  \\| |\n");
    printf(" |  __|    | |  |  __| |  _  /  | || |  | | . ` |\n");
    printf(" | |____   | |  | |____| | \\ \\ _| || |__| | |\\  |\n");
    printf(" |______|  |_|  |______|_|  \\_\\____|\\____/|_| \\_|\n");
    printf(RESET);
    printf("\n         -- Eternal Battle Arena --\n\n");
}

 // Mengirim pesan ke server
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
    msgsnd(massage_queue_id, &msg, sizeof(msg) - sizeof(long), 0);
}

// mengirim pesan aksi saat pertandinggan (battle) berlangsung
void send_battle_action(int action) {
    Message msg;
    memset(&msg, 0, sizeof(msg));
    msg.msg_type = BATTLE_MTYPE_BASE + (long)getpid(); 
    msg.action = action;
    msg.sender_pid = getpid();
    strncpy(msg.username, current_user, MAX_USERNAME - 1);
    msgsnd(massage_queue_id, &msg, sizeof(msg) - sizeof(long), 0);
}

 // menerima pesan dari server
int recv_from_server(Message *resp, int timeout_sec) {
    if (timeout_sec <= 0) {
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

// Untuk cek koneksi ke server
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

            battle(opp_name, 1);
            return;
        }

        usleep(500000);
    }
}

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

 // Cleanup saat client keluar
void client_cleanup() {
    if (is_logged_in) {
        send_to_server(MSG_LOGOUT, current_user, NULL, NULL, 0);
    }
    if (shm) shmdt(shm); //memutuskan hubungan antara ruang alamat proses Client dengan blok memori bersama
    disable_raw_mode();
}

void sig_handler(int sig) {
    (void)sig;
    printf("\n  [ETERNAL] Disconnecting...\n");
    client_cleanup();
    exit(0);
}

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
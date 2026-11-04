/* mpo-server: a server for Metal Gear Solid: Portable Ops+ online play
 * (PROTOCOL.md). One file, C99, POSIX sockets or Winsock, no dependencies.
 *
 * Runs the gate (TCP 5731), account (5732) and lobby (5734) servers, STUN
 * (UDP 3478) and the web pages (HTTP, port 80 by default) in one select()
 * loop. --address is what clients are told to connect to; the default is this
 * host's address on the default route. DIR (default server-data) holds
 * accounts.txt and the operator's content: terms.txt, news.txt, faq.txt,
 * ngwords.txt, banner.bin, weapon_params.bin; any of them may be absent,
 * except that the game will not go on line without a weapon_params.bin. With
 * --game (the game's decrypted executable: EBOOT.ELF from an installed port,
 * or PSP_GAME/SYSDIR/BOOT.BIN) one that changes nothing is made from the
 * game's own defaults.
 *
 * accounts.txt has one account per line: id, login name, password, player
 * name, separated by tabs. Passwords are stored as given: the login proof is
 * an HMAC keyed by the plain password.
 */
#ifdef _WIN32
#define _CRT_RAND_S
#define FD_SETSIZE 256
#include <winsock2.h>
#include <ws2tcpip.h>
#include <direct.h>
#define strncasecmp _strnicmp
#define make_dir(path) _mkdir(path)
typedef SOCKET sock_t;
typedef int socklen_t;
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>
#include <strings.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#define make_dir(path) mkdir(path, 0755)
#define closesocket close
#define INVALID_SOCKET (-1)
typedef int sock_t;
#endif
#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GATE_PORT 5731
#define ACCOUNT_PORT 5732
#define LOBBY_PORT 5734
#define STUN_PORT 3478
/* The bit SaveMGO's plugin sets on the user id in the lobby join (savempo.hpp). */
#define SAVEMPO_MARK 0x10000000u
#define MAX_CLIENTS 200
#define BUFFER_SIZE (24 + 65535 + 1)

static const char USAGE[] =
    "usage: mpo-server [--address PUBLIC_IP] [--web-port PORT] [--data DIR] [--game EBOOT]\n"
    "       mpo-server [--data DIR] adduser NAME PASSWORD PLAYER_NAME\n"
    "\n"
    "Runs the gate (TCP 5731), account (5732) and lobby (5734) servers, STUN\n"
    "(UDP 3478) and the web pages. DIR (default server-data) holds accounts.txt\n"
    "and the operator's content: terms.txt, news.txt (blocks separated by blank\n"
    "lines, each \"date | title\" then the body), faq.txt, ngwords.txt,\n"
    "banner.bin and weapon_params.bin, which the game needs to go on line;\n"
    "--game EBOOT makes a neutral one from the game's decrypted executable.\n"
    "Point a client at it with <PREFIX>_ONLINE_SERVER=host[:web port].\n";

static const uint8_t MASK[4] = {0x5a, 0x70, 0x85, 0xaf};
static const uint8_t MAC_KEY[] = "Z7/biJ46TzGF-8yx";
/* The game's Blowfish key (EBOOT 0x16d998, 56 bytes). */
static const uint8_t CIPHER_KEY[56] = {
    0x53, 0x7e, 0xf9, 0x41, 0xc6, 0x27, 0x3a, 0x8b, 0xdd, 0x0c, 0x65, 0x4c, 0xa5, 0x16,
    0x94, 0xb1, 0x20, 0x72, 0xed, 0x41, 0xb9, 0x48, 0xb6, 0x18, 0x0f, 0x98, 0x43, 0x46,
    0xfc, 0xb7, 0x09, 0x72, 0x65, 0xcb, 0x4a, 0x37, 0x98, 0x5b, 0x1a, 0xc8, 0xdf, 0xed,
    0x51, 0x27, 0x10, 0x3f, 0x77, 0x0e, 0xc0, 0xa9, 0x16, 0x5c, 0xd9, 0x38, 0xe4, 0xf2,
};

static void die(const char *what) {
    perror(what);
    exit(1);
}

static void put_u32(uint8_t *p, uint32_t v) {
    p[0] = v >> 24, p[1] = v >> 16, p[2] = v >> 8, p[3] = v;
}

static uint32_t get_u32(const uint8_t *p) {
    return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3];
}

static void put_le32(uint8_t *p, uint32_t v) {
    p[0] = v, p[1] = v >> 8, p[2] = v >> 16, p[3] = v >> 24;
}

static uint32_t get_le32(const uint8_t *p) {
    return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24;
}

/* A NUL-terminated field of `size` bytes in a zeroed buffer. */
static void put_fixed(uint8_t *dst, const char *text, size_t size) {
    size_t n = strlen(text);
    memcpy(dst, text, n < size - 1 ? n : size - 1);
}

static void random_bytes(uint8_t *out, size_t n) {
#ifdef _WIN32
    for (size_t i = 0; i < n; i++) {
        unsigned v;
        rand_s(&v);
        out[i] = (uint8_t)v;
    }
#else
    FILE *f = fopen("/dev/urandom", "rb");
    if (!f || fread(out, 1, n, f) != n)
        die("/dev/urandom");
    fclose(f);
#endif
}

/* --- MD5 (RFC 1321) and HMAC-MD5 ------------------------------------------ */

static const uint32_t MD5_K[64] = {
    0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
    0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
    0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
    0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
    0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
    0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
    0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
    0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391,
};
static const uint8_t MD5_R[16] = {7, 12, 17, 22, 5, 9, 14, 20, 4, 11, 16, 23, 6, 10, 15, 21};

typedef struct {
    uint32_t h[4];
    uint64_t length;
    uint8_t block[64];
} md5_t;

static void md5_block(uint32_t h[4], const uint8_t *p) {
    uint32_t w[16], a = h[0], b = h[1], c = h[2], d = h[3];
    for (int i = 0; i < 16; i++)
        w[i] = get_le32(p + 4 * i);
    for (int i = 0; i < 64; i++) {
        uint32_t f;
        int g;
        if (i < 16)
            f = (b & c) | (~b & d), g = i;
        else if (i < 32)
            f = (d & b) | (~d & c), g = (5 * i + 1) & 15;
        else if (i < 48)
            f = b ^ c ^ d, g = (3 * i + 5) & 15;
        else
            f = c ^ (b | ~d), g = (7 * i) & 15;
        f += a + MD5_K[i] + w[g];
        int r = MD5_R[(i >> 4) * 4 + (i & 3)];
        a = d, d = c, c = b, b += f << r | f >> (32 - r);
    }
    h[0] += a, h[1] += b, h[2] += c, h[3] += d;
}

static void md5_init(md5_t *m) {
    m->h[0] = 0x67452301, m->h[1] = 0xefcdab89, m->h[2] = 0x98badcfe, m->h[3] = 0x10325476;
    m->length = 0;
}

static void md5_update(md5_t *m, const void *data, size_t n) {
    const uint8_t *p = data;
    while (n--) {
        m->block[m->length++ % 64] = *p++;
        if (m->length % 64 == 0)
            md5_block(m->h, m->block);
    }
}

static void md5_final(md5_t *m, uint8_t out[16]) {
    uint64_t bits = m->length * 8;
    uint8_t byte = 0x80;
    md5_update(m, &byte, 1);
    byte = 0;
    while (m->length % 64 != 56)
        md5_update(m, &byte, 1);
    for (int i = 0; i < 8; i++) {
        byte = (uint8_t)(bits >> (8 * i));
        md5_update(m, &byte, 1);
    }
    for (int i = 0; i < 16; i++)
        out[i] = (uint8_t)(m->h[i / 4] >> (8 * (i % 4)));
}

/* HMAC-MD5 under the game's key over the concatenation a + b + c. */
static void hmac_md5(uint8_t out[16], const void *a, size_t an, const void *b, size_t bn,
                     const void *c, size_t cn) {
    uint8_t pad[64];
    md5_t m;
    memset(pad, 0x36, 64);
    for (int i = 0; i < 16; i++)
        pad[i] ^= MAC_KEY[i];
    md5_init(&m);
    md5_update(&m, pad, 64);
    md5_update(&m, a, an);
    md5_update(&m, b, bn);
    md5_update(&m, c, cn);
    md5_final(&m, out);
    memset(pad, 0x5c, 64);
    for (int i = 0; i < 16; i++)
        pad[i] ^= MAC_KEY[i];
    md5_init(&m);
    md5_update(&m, pad, 64);
    md5_update(&m, out, 16);
    md5_final(&m, out);
}

/* --- Blowfish (Schneier, 1993), ECB, each 8-byte block as two big-endian
 * words. The initial P-array and S-boxes are the fractional hexadecimal
 * digits of pi, computed at startup by Machin's formula. ------------------- */

static uint32_t bf_p[18], bf_s[4][256];

static void big_div(uint32_t *a, int n, uint32_t d) {
    uint64_t rest = 0;
    for (int i = 0; i < n; i++) {
        uint64_t v = rest << 32 | a[i];
        a[i] = (uint32_t)(v / d), rest = v % d;
    }
}

static void big_add(uint32_t *a, const uint32_t *b, int n, int subtract) {
    uint64_t carry = 0;
    for (int i = n - 1; i >= 0; i--) {
        uint64_t v = subtract ? (uint64_t)a[i] - b[i] - carry : (uint64_t)a[i] + b[i] + carry;
        a[i] = (uint32_t)v, carry = subtract ? v >> 63 : v >> 32;
    }
}

/* atan(1/x) as a fixed-point number: word 0 is the integer part. */
static void arctan_inverse(uint32_t *total, int n, uint32_t x) {
    uint32_t *term = calloc(n, 4), *part = malloc(n * 4);
    memset(total, 0, n * 4);
    term[0] = 1;
    big_div(term, n, x);
    for (uint32_t k = 1, subtract = 0;; k += 2, subtract ^= 1) {
        int zero = 1;
        for (int i = 0; i < n && zero; i++)
            zero = !term[i];
        if (zero)
            break;
        memcpy(part, term, n * 4);
        big_div(part, n, k);
        big_add(total, part, n, subtract);
        big_div(term, n, x * x);
    }
    free(term);
    free(part);
}

static void bf_encrypt(uint32_t *l, uint32_t *r);

static void blowfish_init(const uint8_t *key, size_t key_length) {
    enum { WORDS = 18 + 4 * 256, N = WORDS + 3 }; /* integer word, two guard words */
    uint32_t *a = malloc(N * 4), *b = malloc(N * 4);
    arctan_inverse(a, N, 5);
    arctan_inverse(b, N, 239);
    /* pi = 16 atan(1/5) - 4 atan(1/239) */
    for (int i = 0; i < 2; i++)
        big_add(a, a, N, 0);
    big_add(a, b, N, 1);
    big_add(a, a, N, 0);
    big_add(a, a, N, 0);
    memcpy(bf_p, a + 1, sizeof bf_p);
    memcpy(bf_s, a + 1 + 18, sizeof bf_s);
    free(a);
    free(b);
    for (int i = 0; i < 18; i++) {
        uint32_t word = 0;
        for (int j = 0; j < 4; j++)
            word = word << 8 | key[(i * 4 + j) % key_length];
        bf_p[i] ^= word;
    }
    uint32_t l = 0, r = 0;
    for (int i = 0; i < 18; i += 2)
        bf_encrypt(&l, &r), bf_p[i] = l, bf_p[i + 1] = r;
    for (int box = 0; box < 4; box++)
        for (int i = 0; i < 256; i += 2)
            bf_encrypt(&l, &r), bf_s[box][i] = l, bf_s[box][i + 1] = r;
}

static uint32_t bf_f(uint32_t x) {
    return ((bf_s[0][x >> 24] + bf_s[1][x >> 16 & 255]) ^ bf_s[2][x >> 8 & 255]) + bf_s[3][x & 255];
}

static void bf_encrypt(uint32_t *l, uint32_t *r) {
    uint32_t left = *l, right = *r, t;
    for (int i = 0; i < 16; i++)
        left ^= bf_p[i], right ^= bf_f(left), t = left, left = right, right = t;
    *l = right ^ bf_p[17], *r = left ^ bf_p[16];
}

static void bf_decrypt(uint32_t *l, uint32_t *r) {
    uint32_t left = *l, right = *r, t;
    for (int i = 17; i > 1; i--)
        left ^= bf_p[i], right ^= bf_f(left), t = left, left = right, right = t;
    *l = right ^ bf_p[0], *r = left ^ bf_p[1];
}

/* Whole blocks in place; a trailing partial block is left as it is. */
static void blowfish(uint8_t *data, size_t n, int decrypt) {
    for (size_t at = 0; at + 8 <= n; at += 8) {
        uint32_t l = get_u32(data + at), r = get_u32(data + at + 4);
        (decrypt ? bf_decrypt : bf_encrypt)(&l, &r);
        put_u32(data + at, l), put_u32(data + at + 4, r);
    }
}

/* --- Data directory ------------------------------------------------------- */

static char data_dir[1024] = "server-data";
static char address[64];

/* The whole of DIR/name, NUL-terminated, or NULL. */
static uint8_t *read_file(const char *path, size_t *size) {
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    size_t capacity = 4096, n = 0, got;
    uint8_t *data = malloc(capacity + 1);
    while ((got = fread(data + n, 1, capacity - n, f)) > 0)
        if ((n += got) == capacity)
            data = realloc(data, (capacity *= 2) + 1);
    fclose(f);
    data[n] = 0;
    if (size)
        *size = n;
    return data;
}

static uint8_t *content(const char *name, size_t *size) {
    char path[1200];
    snprintf(path, sizeof path, "%s/%s", data_dir, name);
    return read_file(path, size);
}

static void make_dirs(char *path) {
    for (char *p = path + 1;; p++) {
        if (*p == '/' || *p == '\\' || !*p) {
            char c = *p;
            *p = 0;
            int failed = make_dir(path) && errno != EEXIST;
            if (!(*p = c)) {
                if (failed)
                    die(path);
                break;
            }
        }
    }
}

typedef struct {
    uint32_t id;
    char name[17], password[65], player[17], token[17];
} user_t;

static user_t *users;
static int user_count;
static uint32_t next_id = 1;

static void load_accounts(void) {
    char path[1200], line[256];
    snprintf(path, sizeof path, "%s/accounts.txt", data_dir);
    FILE *f = fopen(path, "r");
    if (!f)
        return;
    while (fgets(line, sizeof line, f)) {
        user_t u = {0};
        if (sscanf(line, "%u\t%16[^\t\n]\t%64[^\t\n]\t%16[^\t\r\n]", &u.id, u.name, u.password,
                   u.player) != 4)
            continue;
        users = realloc(users, (user_count + 1) * sizeof *users);
        users[user_count++] = u;
        if (u.id >= next_id)
            next_id = u.id + 1;
    }
    fclose(f);
}

static int add_user(const char *name, const char *password, const char *player) {
    if (strlen(name) > 16 || strlen(password) > 64 || strlen(player) > 16 ||
        strpbrk(name, "\t\r\n") || strpbrk(password, "\t\r\n") || strpbrk(player, "\t\r\n")) {
        fprintf(stderr, "names are at most 16 characters, passwords 64, without tabs\n");
        return 1;
    }
    for (int i = 0; i < user_count; i++)
        if (!strcmp(users[i].name, name)) {
            printf("exists %s\n", name);
            return 1;
        }
    char path[1200];
    snprintf(path, sizeof path, "%s/accounts.txt", data_dir);
    FILE *f = fopen(path, "a");
    if (!f)
        die(path);
    fprintf(f, "%u\t%s\t%s\t%s\n", next_id, name, password, player);
    fclose(f);
    printf("added %s\n", name);
    return 0;
}

static user_t *user_by_id(uint32_t id) {
    for (int i = 0; i < user_count; i++)
        if (users[i].id == id)
            return &users[i];
    return NULL;
}

/* --- Weapon parameters ------------------------------------------------------ */

static uint32_t crc32(const uint8_t *p, size_t n) {
    uint32_t crc = 0xFFFFFFFF;
    while (n--) {
        crc ^= *p++;
        for (int k = 0; k < 8; k++)
            crc = crc >> 1 ^ (0xEDB88320 & -(crc & 1));
    }
    return ~crc;
}

/* The keystream the game decrypts weapon_params.bin's sections with (the
 * online menu stage, set up at 0x8b182dc, applied at 0x8b18308); it is its own
 * inverse. `key` is the file's three header words at 8. */
static void weapon_params_cipher(uint8_t *data, size_t n, const uint8_t *key) {
    uint32_t a = get_le32(key) ^ get_le32(key + 4), c = get_le32(key + 8);
    uint32_t state = a | (a ^ 0x6576) << 16, step = a * c;
    for (size_t at = 0; at + 4 <= n; at += 4) {
        put_le32(data + at, get_le32(data + at) ^ state);
        state = state * 0x02E90EDD + step;
    }
}

/* A weapon_params.bin that leaves the game as it is. The game reads five
 * sections, each {size, CRC-32 of the plain data, offset} from 0x14, after the
 * magic 0x00ed617a, a zero word and a 12-byte key. Four are copied to override
 * buffers with their size, so empty ones override nothing. The fifth is copied
 * over a 0xd0-byte table whatever its size (EBOOT 0xa1cdc), so it carries
 * that table as the executable has it: 0x3f90 into the data segment (US). */
static void make_weapon_params(const char *executable) {
    char path[1200];
    snprintf(path, sizeof path, "%s/weapon_params.bin", data_dir);
    FILE *out = fopen(path, "rb");
    if (out) {
        fclose(out);
        return;
    }
    size_t size;
    uint8_t *exe = read_file(executable, &size);
    if (!exe)
        die(executable);
    uint32_t phoff = size >= 0x20 ? get_le32(exe + 0x1C) : 0;
    uint32_t data = size >= phoff + 40 ? get_le32(exe + phoff + 32 + 4) : 0; /* second header's p_offset */
    if (!phoff || (uint64_t)data + 0x3F90 + 0xD0 > size) {
        fprintf(stderr, "%s: not the game's executable\n", executable);
        exit(1);
    }
    enum { HEADER = 0x14 + 6 * 12, TABLE = 0xD0 };
    uint8_t file[HEADER + TABLE] = {0};
    put_le32(file, 0x00ED617A);
    memcpy(file + 8, "NEUTRAL", 7);
    for (int i = 0; i < 6; i++)
        put_le32(file + 0x14 + 12 * i + 8, i < 5 ? HEADER : HEADER + TABLE);
    memcpy(file + HEADER, exe + data + 0x3F90, TABLE);
    put_le32(file + 0x14 + 12 * 4, TABLE);
    put_le32(file + 0x14 + 12 * 4 + 4, crc32(file + HEADER, TABLE));
    weapon_params_cipher(file + HEADER, TABLE, file + 8);
    free(exe);
    if (!(out = fopen(path, "wb")) || fwrite(file, 1, sizeof file, out) != sizeof file)
        die(path);
    fclose(out);
    printf("made %s from %s\n", path, executable);
}

/* --- Connections ------------------------------------------------------------ */

enum kind { GATE, ACCOUNT, LOBBY, WEB };
static const char *KIND_NAMES[] = {"gate", "account", "lobby", "web"};

typedef struct {
    sock_t fd;
    enum kind kind;
    char peer[32];
    uint8_t *buffer;
    size_t have;
    uint32_t sequence;
    user_t *user;
    uint8_t challenge[16];
    size_t challenge_length;
    char ip[16];      /* the peer's address: a host's public address in 4321 */
    uint8_t lan[18];  /* from 4700: LAN address[16], u16 game port */
} client_t;

static client_t clients[MAX_CLIENTS];
static int client_count;

/* A hosted game: the host's 4b10 settings, plain, as the host sent them. */
#define SETTINGS_SIZE 315
#define MAX_GAMES 64
typedef struct {
    uint32_t id;
    sock_t host;  /* the host's lobby connection; the game ends with it */
    user_t *user;
    char ip[16];
    uint8_t lan[18];
    uint8_t settings[SETTINGS_SIZE];
} game_t;

static game_t games[MAX_GAMES];
static int game_count;
static uint32_t next_game = 1;

static game_t *game_by_id(uint32_t id) {
    for (int i = 0; i < game_count; i++)
        if (games[i].id == id)
            return &games[i];
    return NULL;
}

static void send_all(sock_t fd, const void *data, size_t n) {
    const char *p = data;
    while (n) {
        int sent = send(fd, p, (int)n, 0);
        if (sent <= 0)
            return; /* the receive side notices and closes */
        p += sent, n -= sent;
    }
}

static void mask(uint8_t *p, size_t n) {
    for (size_t i = 0; i < n; i++)
        p[i] ^= MASK[i % 4];
}

static void send_message(client_t *c, uint16_t command, const void *payload, size_t length) {
    static uint8_t m[24 + 65535];
    if (length > 65535)
        length = 65535;
    m[0] = command >> 8, m[1] = command, m[2] = length >> 8, m[3] = length;
    put_u32(m + 4, ++c->sequence);
    if (length)
        memcpy(m + 24, payload, length);
    hmac_md5(m + 8, m, 8, m + 24, length, NULL, 0);
    mask(m, 24 + length);
    send_all(c->fd, m, 24 + length);
}

static void send_u32(client_t *c, uint16_t command, uint32_t value) {
    uint8_t p[4];
    put_u32(p, value);
    send_message(c, command, p, 4);
}

static char *trim(char *s) {
    while (isspace((unsigned char)*s))
        s++;
    for (char *e = s + strlen(s); e > s && isspace((unsigned char)e[-1]);)
        *--e = 0;
    return s;
}

/* --- Gate ------------------------------------------------------------------- */

static int gate(client_t *c, uint16_t command, const uint8_t *payload, size_t length) {
    (void)payload, (void)length;
    if (command == 0x2805) {
        static const struct { const char *name; uint16_t port, kind; } servers[] = {
            {"GATE", GATE_PORT, 1}, {"ACCOUNT", ACCOUNT_PORT, 2}, {"SOLID_SNAKE", LOBBY_PORT, 4}};
        uint8_t list[160] = {0};
        send_u32(c, 0x2802, 0);
        for (int i = 0; i < 3; i++) {
            /* 49 bytes, unaligned: the address field is 15 bytes. */
            uint8_t *e = list + 49 * i;
            put_u32(e, i), put_u32(e + 4, i);
            put_fixed(e + 8, servers[i].name, 16);
            put_fixed(e + 24, address, 15);
            e[39] = servers[i].port >> 8, e[40] = (uint8_t)servers[i].port;
            e[42] = 1, e[44] = (uint8_t)servers[i].kind;
        }
        send_message(c, 0x2803, list, sizeof list);
        send_u32(c, 0x2804, 0);
    } else if (command == 0x2008) {
        /* news.txt: blocks separated by blank lines, each "date | title" then the body. */
        char *text = (char *)content("news.txt", NULL), *blocks[64];
        int count = 0;
        for (char *at = text; at && *at && count < 64;) {
            while (*at == '\n' || *at == '\r' || *at == ' ' || *at == '\t')
                at++;
            if (!*at)
                break;
            blocks[count++] = at;
            char *end = strstr(at, "\n\n");
            if (!end)
                break;
            *end = 0, at = end + 2;
        }
        send_u32(c, 0x2009, 0);
        for (int i = 0; i < count; i++) {
            static uint8_t item[65535];
            memset(item, 0, 4 + 1 + 19 + 128);
            char *line = blocks[i], *body = strchr(line, '\n'), *bar;
            if (body)
                *body++ = 0;
            else
                body = "";
            char *title = (bar = strchr(line, '|')) ? (*bar = 0, bar + 1) : "";
            line = trim(line), title = trim(title), body = trim(body);
            put_u32(item, count - i);
            item[4] = 1;
            put_fixed(item + 5, line, 19);
            put_fixed(item + 24, title, 128);
            size_t n = 4 + 1 + 19 + 128;
            for (char *p = body; *p && n < sizeof item - 2; p++) {
                if (*p == '\r')
                    continue;
                if (*p == '\n')
                    item[n++] = '\r';
                item[n++] = *p;
            }
            item[n++] = 0;
            send_message(c, 0x200a, item, n);
        }
        free(text);
        send_u32(c, 0x200b, 0);
    } else {
        return 0;
    }
    return 1;
}

/* --- Account ---------------------------------------------------------------- */

static int account(client_t *c, uint16_t command, const uint8_t *payload, size_t length) {
    if (command == 0x3001) {
        random_bytes(c->challenge, 16);
        c->challenge_length = 16;
        send_message(c, 0x3002, c->challenge, 16);
    } else if (command == 0x3003) {
        uint8_t plain[32] = {0}, expected[16], reply[20] = {0};
        char name[17] = {0};
        memcpy(plain, payload, length < 32 ? length : 32);
        blowfish(plain, 32, 1);
        memcpy(name, plain, 16);
        user_t *user = NULL;
        for (int i = 0; i < user_count && !user; i++)
            if (!strcmp(users[i].name, name))
                user = &users[i];
        if (user)
            hmac_md5(expected, name, strlen(name), c->challenge, c->challenge_length,
                     user->password, strlen(user->password));
        if (!user || length < 32 || memcmp(expected, plain + 16, 16)) {
            printf("account %s login refused for '%s'\n", c->peer, name);
            put_u32(reply, 0xFFFFFFFF);
            send_message(c, 0x3004, reply, sizeof reply);
            return 1;
        }
        uint8_t letters[16];
        random_bytes(letters, 16);
        for (int i = 0; i < 16; i++)
            user->token[i] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"[letters[i] % 52];
        c->user = user;
        printf("account %s login '%s' id %u\n", c->peer, name, user->id);
        memcpy(reply + 4, user->token, 16);
        send_message(c, 0x3004, reply, sizeof reply);
    } else if (command == 0x3840 && c->user) {
        uint8_t reply[32] = {0};
        put_u32(reply + 4, c->user->id);
        put_fixed(reply + 8, c->user->player, 16);
        send_message(c, 0x3841, reply, sizeof reply);
    } else if (command == 0x3042) {
        send_u32(c, 0x3043, 0);
        send_u32(c, 0x3045, 0);
    } else {
        return 0;
    }
    return 1;
}

/* --- Lobby ------------------------------------------------------------------ */

/* 4901's and 4903's player block: user id, player name, emblem, stats (zero). */
static void player_record(uint8_t *out, const user_t *user) {
    put_u32(out, user->id);
    put_fixed(out + 4, user->player, 16);
    put_fixed(out + 4 + 16 + 13, "emblem", 16);
}

/* 4b10's settings (EBOOT 0x13c6a4): name[16], comment[128], u8 locked,
 * password[16], u8, then the rules from the settings struct's 0x234 on. */
enum { S_NAME = 0, S_COMMENT = 16, S_LOCKED = 144, S_PASSWORD = 145, S_A7 = 161, S_RULES = 162 };

/* 4b13 (parsed at 0x13d1f8) carries the same rules with fields 4b10 has not:
 * the sizes of the rule fields in order, negative where 4b13 has a field of
 * that size that 4b10 lacks (struct 0x266, 0x26c, 0x272, 0x278, 0x2e8, 0x2ec,
 * 0x2f0, 0x2f8; sent as zero). */
static const signed char DETAIL_RULES[] = {
    32, 1, 1, 1, 4, 4, 4, 1, 1, -1, 4, -4, 2, -2, 4, -4, 2, 1, 4, 75, -2, -4, -1, 2, 1, 2, 2, -4, 5};

/* 4b13: u32 result, u32 game id, then the game's settings and players. */
static size_t game_details(uint8_t *out, const game_t *g) {
    const uint8_t *s = g->settings;
    put_u32(out + 4, g->id);
    memcpy(out + 8, s + S_NAME, 16);
    memcpy(out + 24, s + S_COMMENT, 128);
    out[152] = s[S_LOCKED], out[153] = s[S_A7];
    size_t n = 154 + 12, from = S_RULES; /* struct 0x304, 0x308, 0x30c: zero */
    for (size_t i = 0; i < sizeof DETAIL_RULES; i++) {
        int size = DETAIL_RULES[i];
        if (size > 0)
            memcpy(out + n, s + from, size), from += size;
        n += size > 0 ? size : -size;
    }
    /* Players, up to nine, until the payload ends: u32 user id, name[16], u8,
     * five u32. Only the host so far; the u8 and the last u32 are SaveMGO's
     * values for its host (0xfe, 100), meaning unknown. */
    put_u32(out + n, g->user->id);
    put_fixed(out + n + 4, g->user->player, 16);
    out[n + 20] = 0xfe;
    put_u32(out + n + 37, 100);
    return n + 41;
}

/* One 4b02 entry (parsed at 0x13ac24 into 0x44-byte slots): u32 game id,
 * name[16], u8 locked, u8 (struct 0xa7), three u32, four u8, bytes[2]
 * (struct 0x2f1), u8, u32, two u8, u32, u16, u32, u8. Read by the online
 * menu, a stage module; beyond what matches the struct, from SaveMGO's one
 * listed game: the first two u8 are the round's (mode, map) pair, as the
 * struct's 0x234/0x244 (SaveMGO's game was in its second round). */
static void game_entry(uint8_t *out, const game_t *g) {
    const uint8_t *s = g->settings;
    put_u32(out, g->id);
    memcpy(out + 4, s + S_NAME, 16);
    out[20] = s[S_LOCKED], out[21] = s[S_A7];
    out[34] = s[S_RULES], out[35] = s[S_RULES + 1];
    out[37] = 7;                  /* SaveMGO's value, meaning unknown */
    memcpy(out + 38, s + 303, 2); /* struct 0x2f1 */
    out[40] = 1;                  /* SaveMGO: 1, one player? */
    put_u32(out + 41, 100);       /* SaveMGO's value, as its host player's last u32 */
}

static void end_games(sock_t host) {
    for (int i = game_count - 1; i >= 0; i--)
        if (games[i].host == host) {
            printf("lobby game %u ended\n", games[i].id);
            games[i] = games[--game_count];
        }
}

/* The three 4904 and 4905 pages (700 bytes, u32 page first; 4905's at 4)
 * that follow 4901 and 4903: contents not understood, zero. */
static void send_pages(client_t *c, uint8_t *buffer) {
    for (uint32_t page = 0; page < 3; page++) {
        memset(buffer, 0, 700);
        put_u32(buffer, page);
        send_message(c, 0x4904, buffer, 700);
        put_u32(buffer, 0), put_u32(buffer + 4, page);
        send_message(c, 0x4905, buffer, 700);
    }
}

static int lobby(client_t *c, uint16_t command, const uint8_t *payload, size_t length) {
    static uint8_t reply[1024];
    memset(reply, 0, sizeof reply);
    if (command == 0x3003) {
        uint8_t plain[24] = {0};
        memcpy(plain, payload, length < 24 ? length : 24);
        blowfish(plain, 24, 1);
        uint32_t user_id = get_u32(plain) & ~SAVEMPO_MARK;
        user_t *user = user_by_id(user_id);
        if (!user || !user->token[0] || memcmp(user->token, plain + 4, 16)) {
            printf("lobby %s join refused for id %u\n", c->peer, user_id);
            put_u32(reply, 0xFFFFFFFF);
            send_message(c, 0x3004, reply, 256);
            return 1;
        }
        c->user = user;
        memcpy(reply + 4, user->token, 16);
        put_fixed(reply + 20, "RECORDING", 16);
        send_message(c, 0x3004, reply, 256);
    } else if (!c->user) {
        return 0;
    } else if (command == 0x4900) {
        player_record(reply, c->user);
        send_message(c, 0x4901, reply, 1024);
        send_pages(c, reply);
    } else if (command == 0x4700) {
        /* u16, LAN address[16], u16 game port, u16 */
        if (length >= 20)
            memcpy(c->lan, payload + 2, 18);
        send_u32(c, 0x4701, 0);
    } else if (command == 0x4702) {
        send_u32(c, 0x4703, 0);
    } else if (command == 0x4b00) {
        /* u32 result, then games (one 4b02 each here; the client reads
         * entries until a message ends), then u32 result. */
        send_u32(c, 0x4b01, 0);
        for (int i = 0; i < game_count; i++) {
            game_entry(reply, &games[i]);
            send_message(c, 0x4b02, reply, 58);
            memset(reply, 0, 58);
        }
        send_u32(c, 0x4b03, 0);
    } else if (command == 0x4b10) {
        /* Create a game: Blowfish(settings), 320 bytes; 4b11 is u32 result. */
        if (length < 320 || game_count == MAX_GAMES) {
            send_u32(c, 0x4b11, 0xFFFFFFFF);
            return 1;
        }
        end_games(c->fd);
        game_t *g = &games[game_count++];
        uint8_t plain[320];
        memcpy(plain, payload, 320);
        blowfish(plain, 320, 1);
        memset(g, 0, sizeof *g);
        g->id = next_game++, g->host = c->fd, g->user = c->user;
        memcpy(g->ip, c->ip, 16), memcpy(g->lan, c->lan, 18);
        memcpy(g->settings, plain, SETTINGS_SIZE);
        printf("lobby %s game %u '%.16s' by %s\n", c->peer, g->id, (char *)plain, c->user->player);
        send_u32(c, 0x4b11, 0);
    } else if (command == 0x4b12) {
        game_t *g = length >= 4 ? game_by_id(get_u32(payload)) : NULL;
        if (g)
            send_message(c, 0x4b13, reply, game_details(reply, g));
        else
            send_u32(c, 0x4b13, 1);
    } else if (command == 0x4320) {
        /* Join: Blowfish(u32 game id, password[16]); 4321 is u32 result, the
         * host's public address[16] and port, LAN address[16] and port. */
        uint8_t plain[24] = {0};
        memcpy(plain, payload, length < 24 ? length : 24);
        blowfish(plain, 24, 1);
        game_t *g = game_by_id(get_u32(plain));
        if (!g || (g->settings[S_LOCKED] == 1 &&
                   strncmp((char *)plain + 4, (char *)g->settings + S_PASSWORD, 16))) {
            printf("lobby %s join of game %u refused\n", c->peer, get_u32(plain));
            send_u32(c, 0x4321, 1);
            return 1;
        }
        printf("lobby %s joins game %u\n", c->peer, g->id);
        /* Behind the same address as its host (one machine, one NAT), a
         * joiner can only reach the host on its LAN: the joiner tries the
         * public address alone, about eight times over 15 s, then sends 4322. */
        memcpy(reply + 4, strcmp(g->ip, c->ip) ? g->ip : (char *)g->lan, 16);
        memcpy(reply + 20, g->lan + 16, 2); /* the host's game port, as on its LAN */
        memcpy(reply + 22, g->lan, 18);
        send_message(c, 0x4321, reply, 40);
    } else if (command == 0x4340 || command == 0x4344) {
        /* The host, about a player: 4340 u32 user id when one joins (0x13dd98),
         * 4344 u32 user id and u8 (0x13e210). The reply, u32 result and u32
         * user id, must name the player: the host matches it against its
         * pending requests (0x13de8c, 0x13e320) before answering the joiner. */
        if (length >= 4)
            memcpy(reply + 4, payload, 4);
        send_message(c, command + 1, reply, 8);
    } else if (command == 0x4316 || command == 0x4398) {
        /* Hosting began; the host's periodic u32 and (user id, value) pairs. */
        send_u32(c, command + 1, 0);
    } else if (command == 0x4902) {
        user_t *user = length >= 4 ? user_by_id(get_u32(payload)) : NULL;
        if (user)
            player_record(reply + 4, user);
        send_message(c, 0x4903, reply, 512);
        send_pages(c, reply); /* SaveMGO follows 4903 with them too; the client waits for them */
    } else if (command == 0x43b0 || command == 0x43b2 || command == 0x43b4) {
        send_u32(c, command + 1, 0);
    } else if (command == 0x43a0) {
        send_message(c, 0x43a1, NULL, 0);
    } else if (command == 0x4b04) {
        /* The host's saved game settings (read at EBOOT 0x13b5a4): none yet. */
        uint8_t settings[0x318] = {0};
        blowfish(settings, sizeof settings, 0);
        send_message(c, 0x4b05, settings, sizeof settings);
    } else if (command == 0x4b06) {
        /* More saved settings (0x13be6c), unencrypted: none yet. */
        send_message(c, 0x4b07, reply, 0xd8);
    } else if (command >= 0x4000 && command < 0x5000 && command % 2 == 0) {
        /* Not understood yet: answer "ok" with nothing, and say so. */
        printf("lobby %s auto-reply %04x to %04x ", c->peer, command + 1, command);
        for (size_t i = 0; i < length; i++)
            printf("%02x", payload[i]);
        printf("\n");
        send_message(c, command + 1, reply, 0x100);
    } else {
        return 0;
    }
    return 1;
}

static int (*const HANDLERS[])(client_t *, uint16_t, const uint8_t *, size_t) = {gate, account, lobby};

/* Handles the complete messages in the buffer; 0 closes the connection. */
static int game_input(client_t *c) {
    while (c->have >= 24) {
        uint8_t *m = c->buffer, head[4];
        memcpy(head, m, 4);
        mask(head, 4);
        size_t length = head[2] << 8 | head[3];
        if (c->have < 24 + length)
            break;
        mask(m, 24 + length);
        uint16_t command = m[0] << 8 | m[1];
        uint8_t mac[16];
        hmac_md5(mac, m, 8, m + 24, length, NULL, 0);
        if (memcmp(mac, m + 8, 16)) {
            printf("%s %s bad MAC on %04x\n", KIND_NAMES[c->kind], c->peer, command);
            return 0;
        }
        if (command == 0x0003)
            return 0;
        if (command == 0x0005) {
            send_message(c, 0x0005, NULL, 0);
        } else if (!HANDLERS[c->kind](c, command, m + 24, length)) {
            printf("%s %s unhandled %04x ", KIND_NAMES[c->kind], c->peer, command);
            for (size_t i = 0; i < length; i++)
                printf("%02x", m[24 + i]);
            printf("\n");
        }
        memmove(m, m + 24 + length, c->have -= 24 + length);
    }
    return 1;
}

/* --- Web -------------------------------------------------------------------- */

static void web_reply(client_t *c, const char *status, const void *body, size_t n, const char *type) {
    char head[256];
    int h = snprintf(head, sizeof head,
                     "HTTP/1.0 %s\r\nContent-Type: %s\r\nContent-Length: %lu\r\nConnection: close\r\n\r\n",
                     status, type, (unsigned long)n);
    send_all(c->fd, head, h);
    send_all(c->fd, body, n);
}

static int starts(const char *text, const char *prefix) {
    return !strncmp(text, prefix, strlen(prefix));
}

/* Answers once the request is complete; 0 closes the connection. */
static int web_input(client_t *c) {
    char *request = (char *)c->buffer, *end = strstr(request, "\r\n\r\n");
    if (!end)
        return 1;
    size_t body_at = end + 4 - request, body_length = 0;
    for (char *line = strstr(request, "\r\n"); line && line < end; line = strstr(line + 2, "\r\n"))
        if (!strncasecmp(line + 2, "Content-Length:", 15))
            body_length = strtoul(line + 17, NULL, 10);
    if (c->have < body_at + body_length)
        return 1;
    char method[8] = "", path[1024] = "";
    sscanf(request, "%7s %1023s", method, path);
    path[strcspn(path, "?")] = 0;
    const char *name = strrchr(path, '/') ? strrchr(path, '/') + 1 : path;
    uint8_t *data = NULL;
    size_t size = 0;
    const char *binary = "application/octet-stream";
    if (!strcmp(method, "POST")) {
        printf("web POST %s %.*s\n", path, (int)(body_length < 200 ? body_length : 200), request + body_at);
        web_reply(c, "404 Not Found", "", 0, binary);
    } else if (!strcmp(method, "GET") && starts(name, "policy")) {
        if ((data = content("terms.txt", &size)))
            web_reply(c, "200 OK", data, size, "text/plain");
        else
            web_reply(c, "200 OK", "Play fair.\n", 11, "text/plain");
    } else if (!strcmp(method, "GET") && !strcmp(name, "list_ngword_utf8_lf.bin")) {
        /* Observed empty form: u32 count (little-endian), u32 24 (header size),
         * 16 zero bytes, then the words, one per line. */
        char *words = (char *)content("ngwords.txt", &size);
        uint8_t *list = calloc(24 + size + 1, 1);
        size_t n = 24;
        uint32_t count = 0;
        for (char *w = words ? strtok(words, " \t\r\n") : NULL; w; w = strtok(NULL, " \t\r\n")) {
            size_t len = strlen(w);
            memcpy(list + n, w, len);
            list[n + len] = '\n';
            n += len + 1, count++;
        }
        put_le32(list, count), put_le32(list + 4, 24);
        web_reply(c, "200 OK", list, n, binary);
        free(words);
        free(list);
    } else if (!strcmp(method, "GET") && ((!strcmp(name, "weapon_params.bin") && (data = content("weapon_params.bin", &size))) ||
                                          (starts(name, "lobby") && (data = content("banner.bin", &size))))) {
        web_reply(c, "200 OK", data, size, binary);
    } else if (!strcmp(method, "GET") && starts(name, "online_faq") && (data = content("faq.txt", &size))) {
        web_reply(c, "200 OK", data, size, "text/plain");
    } else {
        printf("web not found %s %s\n", method, path);
        web_reply(c, "404 Not Found", "", 0, binary);
    }
    free(data);
    return 0;
}

/* --- STUN (RFC 5389 binding requests only) ------------------------------------ */

static void stun(sock_t fd) {
    uint8_t in[512], out[20 + 24];
    struct sockaddr_in from;
    socklen_t from_length = sizeof from;
    int n = recvfrom(fd, (char *)in, sizeof in, 0, (struct sockaddr *)&from, &from_length);
    if (n < 20 || in[0] != 0 || in[1] != 1 || from.sin_family != AF_INET)
        return;
    uint32_t ip = ntohl(from.sin_addr.s_addr), cookie = 0x2112A442;
    uint16_t port = ntohs(from.sin_port), xport = port ^ (cookie >> 16);
    memset(out, 0, sizeof out);
    out[1] = 0x01, out[0] = 0x01, out[3] = 24;
    memcpy(out + 4, in + 4, 16);
    uint8_t *a = out + 20;
    a[1] = 0x01, a[3] = 8, a[5] = 1, a[6] = port >> 8, a[7] = (uint8_t)port, put_u32(a + 8, ip);
    a += 12;
    a[1] = 0x20, a[3] = 8, a[5] = 1, a[6] = xport >> 8, a[7] = (uint8_t)xport, put_u32(a + 8, ip ^ cookie);
    sendto(fd, (const char *)out, sizeof out, 0, (struct sockaddr *)&from, from_length);
}

/* --- Main ------------------------------------------------------------------- */

static sock_t listen_on(int type, uint16_t port) {
    struct sockaddr_in at = {0};
    at.sin_family = AF_INET, at.sin_port = htons(port), at.sin_addr.s_addr = htonl(INADDR_ANY);
    sock_t fd = socket(AF_INET, type, 0);
#ifndef _WIN32
    int on = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on);
#endif
    if (fd == INVALID_SOCKET || bind(fd, (struct sockaddr *)&at, sizeof at) ||
        (type == SOCK_STREAM && listen(fd, 16))) {
        fprintf(stderr, "port %u: ", port);
        die("bind");
    }
    return fd;
}

static void default_address(void) {
    struct sockaddr_in probe = {0}, local;
    socklen_t length = sizeof local;
    probe.sin_family = AF_INET, probe.sin_port = htons(9), probe.sin_addr.s_addr = htonl(0xC0000201);
    sock_t fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (connect(fd, (struct sockaddr *)&probe, sizeof probe) ||
        getsockname(fd, (struct sockaddr *)&local, &length))
        die("finding this host's address (use --address)");
    snprintf(address, sizeof address, "%s", inet_ntoa(local.sin_addr));
    closesocket(fd);
}

static void close_client(int i) {
    printf("%s %s closed\n", KIND_NAMES[clients[i].kind], clients[i].peer);
    end_games(clients[i].fd);
    closesocket(clients[i].fd);
    free(clients[i].buffer);
    clients[i] = clients[--client_count];
}

int main(int argc, char **argv) {
    const char *game = NULL, *positional[4];
    int web_port = 80, positionals = 0;
    setvbuf(stdout, NULL, _IONBF, 0);
    for (int i = 1; i < argc; i++) {
        const char *value = i + 1 < argc ? argv[i + 1] : NULL;
        if (!strcmp(argv[i], "--data") && value)
            snprintf(data_dir, sizeof data_dir, "%s", value), i++;
        else if (!strcmp(argv[i], "--game") && value)
            game = value, i++;
        else if (!strcmp(argv[i], "--address") && value)
            snprintf(address, sizeof address, "%s", value), i++;
        else if (!strcmp(argv[i], "--web-port") && value)
            web_port = atoi(value), i++;
        else if (argv[i][0] != '-' && positionals < 4)
            positional[positionals++] = argv[i];
        else
            return fputs(USAGE, stderr), 2;
    }
    if (positionals && !(positionals == 4 && !strcmp(positional[0], "adduser")))
        return fputs(USAGE, stderr), 2;
    make_dirs(data_dir);
    load_accounts();
    if (positionals)
        return add_user(positional[1], positional[2], positional[3]);
    if (game)
        make_weapon_params(game);
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa))
        die("WSAStartup");
#else
    signal(SIGPIPE, SIG_IGN);
#endif
    blowfish_init(CIPHER_KEY, sizeof CIPHER_KEY);
    if (!address[0])
        default_address();
    sock_t listeners[4] = {listen_on(SOCK_STREAM, GATE_PORT), listen_on(SOCK_STREAM, ACCOUNT_PORT),
                           listen_on(SOCK_STREAM, LOBBY_PORT), listen_on(SOCK_STREAM, (uint16_t)web_port)};
    sock_t udp = listen_on(SOCK_DGRAM, STUN_PORT);
    printf("serving as %s: gate %d, account %d, lobby %d, STUN %d, web %d; %d accounts\n", address,
           GATE_PORT, ACCOUNT_PORT, LOBBY_PORT, STUN_PORT, web_port, user_count);
    for (;;) {
        fd_set ready;
        FD_ZERO(&ready);
        sock_t top = udp;
        FD_SET(udp, &ready);
        for (int i = 0; i < 4; i++) {
            FD_SET(listeners[i], &ready);
            top = listeners[i] > top ? listeners[i] : top;
        }
        for (int i = 0; i < client_count; i++) {
            FD_SET(clients[i].fd, &ready);
            top = clients[i].fd > top ? clients[i].fd : top;
        }
        if (select((int)top + 1, &ready, NULL, NULL, NULL) < 0) {
#ifndef _WIN32
            if (errno == EINTR)
                continue;
#endif
            die("select");
        }
        if (FD_ISSET(udp, &ready))
            stun(udp);
        for (int k = 0; k < 4; k++) {
            if (!FD_ISSET(listeners[k], &ready))
                continue;
            struct sockaddr_in from;
            socklen_t length = sizeof from;
            sock_t fd = accept(listeners[k], (struct sockaddr *)&from, &length);
            if (fd == INVALID_SOCKET)
                continue;
            if (client_count == MAX_CLIENTS) {
                closesocket(fd);
                continue;
            }
            client_t *c = &clients[client_count++];
            memset(c, 0, sizeof *c);
            c->fd = fd, c->kind = (enum kind)k, c->buffer = malloc(BUFFER_SIZE);
            snprintf(c->peer, sizeof c->peer, "%s:%u", inet_ntoa(from.sin_addr), ntohs(from.sin_port));
            snprintf(c->ip, sizeof c->ip, "%s", inet_ntoa(from.sin_addr));
            if (k != WEB)
                printf("%s %s connected\n", KIND_NAMES[k], c->peer);
        }
        for (int i = client_count - 1; i >= 0; i--) {
            client_t *c = &clients[i];
            if (!FD_ISSET(c->fd, &ready))
                continue;
            int n = recv(c->fd, (char *)c->buffer + c->have, (int)(BUFFER_SIZE - 1 - c->have), 0);
            if (n > 0) {
                c->have += n;
                c->buffer[c->have] = 0;
            }
            if (n <= 0 || !(c->kind == WEB ? web_input(c) : game_input(c)) || c->have == BUFFER_SIZE - 1) {
                if (c->kind == WEB) {
                    closesocket(c->fd);
                    free(c->buffer);
                    clients[i] = clients[--client_count];
                } else {
                    close_client(i);
                }
            }
        }
    }
}

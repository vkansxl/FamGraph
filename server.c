#define _POSIX_C_SOURCE 200809L
#include "family.h"
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>

/* A small, sequential HTTP/1.1 server for a LOCAL classroom demo.
   Bind to loopback only. One process owns one pointer-based C tree. */
static Family family = {NULL, 0, 1};
static const char *data_path = "family.tsv";
static volatile sig_atomic_t running = 1;
static void stop_server(int sig) { (void)sig; running = 0; }

static int send_all(int fd, const char *data, size_t size) {
    while (size) {
        ssize_t n = send(fd, data, size, 0);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return 0;
        data += n; size -= (size_t)n;
    }
    return 1;
}
static void respond(int fd, int status, const char *type, FILE *body) {
    if (fflush(body) || ferror(body) || fseek(body, 0, SEEK_END)) return;
    long size = ftell(body);
    if (size < 0) return;
    rewind(body);
    char header[1024];
    int len = snprintf(header, sizeof header,
        "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %ld\r\n"
        "Connection: close\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\n"
        "Content-Security-Policy: default-src 'self'; style-src 'self'; script-src 'self'; img-src 'self' data:; object-src 'none'; frame-ancestors 'none'\r\n\r\n",
        status, status == 200 ? "OK" : "Error", type, size);
    if (!send_all(fd, header, (size_t)len)) return;
    char buffer[4096]; size_t n;
    while ((n = fread(buffer, 1, sizeof buffer, body))) if (!send_all(fd, buffer, n)) break;
}
static void error_response(int fd, int status, const char *message) {
    FILE *body = tmpfile(); if (!body) return;
    fputs("{\"error\":", body); json_string(body, message); fputc('}', body);
    respond(fd, status, "application/json; charset=utf-8", body); fclose(body);
}
static int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
/* Parse one URL-encoded field, rejecting malformed escapes and embedded NUL. */
static int field(const char *form, const char *key, char *out, size_t cap) {
    size_t k = strlen(key);
    for (const char *s = form; *s;) {
        const char *end = strchr(s, '&'); if (!end) end = s + strlen(s);
        if ((size_t)(end - s) > k && !strncmp(s, key, k) && s[k] == '=') {
            size_t n = 0;
            for (s += k + 1; s < end; s++) {
                int c = (unsigned char)*s;
                if (c == '+') c = ' ';
                else if (c == '%') {
                    if (end - s < 3 || hex_digit(s[1]) < 0 || hex_digit(s[2]) < 0) return 0;
                    c = hex_digit(s[1]) * 16 + hex_digit(s[2]); s += 2;
                }
                if (!c || n + 1 >= cap) return 0;
                out[n++] = (char)c;
            }
            out[n] = 0; return 1;
        }
        s = *end ? end + 1 : end;
    }
    return 0;
}
static int number(const char *s, unsigned *result) {
    if (!*s) return 0;
    for (const char *p = s; *p; p++) if (!isdigit((unsigned char)*p)) return 0;
    errno = 0; char *end;
    unsigned long n = strtoul(s, &end, 10);
    if (errno || *end || n > UINT_MAX) return 0;
    *result = (unsigned)n; return 1;
}
static void route(int fd, char *method, char *target, const char *form, int trusted) {
    char *query = strchr(target, '?');
    if (query) *query++ = 0; else query = "";
    if (!strcmp(method, "GET") && strncmp(target, "/api/", 5)) {
        const char *path = NULL, *type = NULL;
        if (!strcmp(target, "/") || !strcmp(target, "/index.html")) { path = "public/index.html"; type = "text/html; charset=utf-8"; }
        if (!strcmp(target, "/style.css")) { path = "public/style.css"; type = "text/css; charset=utf-8"; }
        if (!strcmp(target, "/app.js")) { path = "public/app.js"; type = "text/javascript; charset=utf-8"; }
        if (!path) { error_response(fd, 404, "File not found."); return; }
        FILE *f = fopen(path, "rb");
        if (!f) { error_response(fd, 404, "Start the server from the project folder so public/ is available."); return; }
        respond(fd, 200, type, f); fclose(f); return;
    }
    if (!strcmp(method, "POST") && !trusted) { error_response(fd, 403, "Missing X-FamGraph request header."); return; }
    FILE *out = tmpfile();
    if (!out) { error_response(fd, 500, "Unable to create response buffer."); return; }
    int status = 200; const char *error = NULL;
    if (!strcmp(method, "GET") && !strcmp(target, "/api/tree")) write_tree(out, &family);
    else if (!strcmp(method, "GET") && !strcmp(target, "/api/relations")) {
        char name[MAX_NAME + 1], id_string[16]; Person *p = NULL; unsigned id;
        if (field(query, "id", id_string, sizeof id_string) && number(id_string, &id)) p = find_id(family.root, id);
        else if (field(query, "name", name, sizeof name)) p = find_name(family.root, name);
        if (!p) { error = "Person not found. Search for the complete name."; status = 404; }
        else write_relations(out, p);
    } else if (!strcmp(method, "POST") && (!strcmp(target, "/api/root") || !strcmp(target, "/api/child"))) {
        char name[MAX_NAME + 1], parent_string[16]; unsigned parent_id = 0;
        if (!field(form, "name", name, sizeof name)) error = "Supply a valid name (up to 100 UTF-8 bytes).";
        if (!error && !strcmp(target, "/api/child") && (!field(form, "parentId", parent_string, sizeof parent_string) || !number(parent_string, &parent_id) || !parent_id)) error = "Select a valid parent.";
        if (!error) {
            Person *p = add_person(&family, parent_id, name, &error);
            if (p) {
                if (!save_family(&family, data_path)) { undo_add(&family, p); error = "Could not save data. Check folder permissions."; status = 500; }
                else fprintf(out, "{\"id\":%u}", p->id);
            }
        }
        if (error && status == 200) status = 400;
    } else if (!strcmp(method, "POST") && !strcmp(target, "/api/reset")) {
        Family empty = {NULL, 0, 1};
        if (!save_family(&empty, data_path)) { error = "Could not save the reset."; status = 500; }
        else { free_family(family.root); family = empty; fputs("{\"ok\":true}", out); }
    } else { error = "Route or method not supported."; status = 404; }
    if (error) { fclose(out); error_response(fd, status, error); }
    else { respond(fd, status, "application/json; charset=utf-8", out); fclose(out); }
}
static void handle_client(int fd, unsigned port) {
    char request[16384]; size_t used = 0; char *split = NULL;
    while (!split && used < sizeof request - 1) {
        ssize_t n = recv(fd, request + used, sizeof request - 1 - used, 0);
        if (n <= 0) return;
        used += (size_t)n; request[used] = 0;
        split = strstr(request, "\r\n\r\n");
    }
    if (!split) { error_response(fd, 413, "Request too large."); return; }
    size_t body_start = (size_t)(split - request) + 4;
    *split = 0;
    char method[16], target[2048], version[16], extra;
    char *line_end = strstr(request, "\r\n");
    if (!line_end) return;
    *line_end = 0;
    if (sscanf(request, "%15s %2047s %15s %c", method, target, version, &extra) != 3 || strcmp(version, "HTTP/1.1")) { error_response(fd, 400, "Invalid HTTP request."); return; }
    size_t content_length = 0; int has_length = 0, trusted = 0, valid_host = 0;
    char expected_a[64], expected_b[64];
    snprintf(expected_a, sizeof expected_a, "127.0.0.1:%u", port);
    snprintf(expected_b, sizeof expected_b, "localhost:%u", port);
    char *save = NULL;
    for (char *line = strtok_r(line_end + 2, "\r\n", &save); line; line = strtok_r(NULL, "\r\n", &save)) {
        char *colon = strchr(line, ':'); if (!colon) { error_response(fd, 400, "Invalid header."); return; }
        *colon++ = 0; while (*colon == ' ' || *colon == '\t') colon++;
        if (!strcasecmp(line, "Host")) valid_host = !strcmp(colon, expected_a) || !strcmp(colon, expected_b);
        if (!strcasecmp(line, "X-FamGraph")) trusted = !strcmp(colon, "1");
        if (!strcasecmp(line, "Transfer-Encoding")) { error_response(fd, 400, "Chunked requests are not supported."); return; }
        if (!strcasecmp(line, "Content-Length")) {
            unsigned n;
            if (has_length || !number(colon, &n) || n > 4096) { error_response(fd, 400, "Invalid content length."); return; }
            content_length = n; has_length = 1;
        }
    }
    if (!valid_host) { error_response(fd, 403, "Use the local URL printed by the server."); return; }
    if (!strcmp(method, "POST") && !has_length) { error_response(fd, 411, "Content-Length required."); return; }
    if (body_start + content_length >= sizeof request) { error_response(fd, 413, "Request too large."); return; }
    while (used < body_start + content_length) {
        ssize_t n = recv(fd, request + used, body_start + content_length - used, 0);
        if (n <= 0) return;
        used += (size_t)n;
    }
    request[body_start + content_length] = 0;
    if (memchr(request + body_start, 0, content_length)) { error_response(fd, 400, "Invalid request body."); return; }
    route(fd, method, target, request + body_start, trusted);
}
int main(int argc, char **argv) {
    unsigned port = 8080;
    if (argc > 3 || (argc > 1 && (!number(argv[1], &port) || port < 1024 || port > 65535))) {
        fprintf(stderr, "Usage: %s [port 1024-65535] [data-file]\n", argv[0]); return 1;
    }
    if (argc == 3) data_path = argv[2];
    if (!load_family(&family, data_path)) { fprintf(stderr, "Cannot read %s: unreadable or invalid data. Existing data was not changed.\n", data_path); return 1; }
    signal(SIGPIPE, SIG_IGN);
    struct sigaction sa; memset(&sa, 0, sizeof sa); sa.sa_handler = stop_server;
    sigaction(SIGINT, &sa, NULL); sigaction(SIGTERM, &sa, NULL);
    int server = socket(AF_INET, SOCK_STREAM, 0);
    if (server < 0) { perror("socket"); free_family(family.root); return 1; }
    int reuse = 1; setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof reuse);
    struct sockaddr_in address; memset(&address, 0, sizeof address);
    address.sin_family = AF_INET; address.sin_port = htons((unsigned short)port);
    inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
    if (bind(server, (struct sockaddr *)&address, sizeof address) || listen(server, 16)) {
        perror("Cannot start server (try another port)"); close(server); free_family(family.root); return 1;
    }
    printf("FamGraph C backend is running.\nOpen http://127.0.0.1:%u\nData: %s | People: %u\nStop with Ctrl+C.\n", port, data_path, family.count); fflush(stdout);
    while (running) {
        int client = accept(server, NULL, NULL);
        if (client < 0) { if (errno == EINTR) continue; perror("accept"); break; }
        struct timeval timeout = {3, 0};
        setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout);
        setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof timeout);
        handle_client(client, port); close(client);
    }
    close(server); free_family(family.root); return 0;
}

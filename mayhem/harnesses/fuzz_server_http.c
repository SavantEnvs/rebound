// REBOUND's built-in HTTP visualisation server (src/server.c, -DSERVER): reb_simulation_start_server()
// spawns a thread that accepts TCP connections on 0.0.0.0:<port> and parses one HTTP request per
// connection (request line, headers incl. Content-Length, /simulation, /keyboard/<n>, /screenshot
// POST bodies). The request handler is static and only reachable through the socket, so the harness
// starts the server once and sends each fuzz input as one request over a loopback connection, then
// reads the response until the server closes the connection.
//
// The harness frames every input as a complete HTTP header block by appending "\r\n\r\n": server.c's
// header loop (server.c:310-318) only stops on a bare "\r\n" line and spins forever once fgets() hits
// EOF without one. That hang is a known upstream DoS (reported with this integration); without the
// framing nearly every mutation would rediscover it and no input would reach the URI dispatch.
//
// A /screenshot upload is only decoded while the simulation waits for one (status
// REB_STATUS_SCREENSHOT, set by reb_simulation_output_screenshot(), output.c), so a request naming
// /screenshot is sent in that state and every other request in the initial one; any screenshot the
// server decoded is freed after the input, so the next upload is not rejected as "not freed".
#define _GNU_SOURCE
#define SERVER
#include "rebound.h"
#include <pthread.h>
#include "server.h"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static struct reb_simulation* sim;
static int server_port;
static enum REB_STATUS initial_status;

static void start_server(void){
    // server.c tries to `curl` rebound.html into the cwd when it is missing; give it a small local one.
    if (access("rebound.html", F_OK)){
        FILE* f = fopen("rebound.html", "w");
        if (f){ fputs("<html><body>rebound</body></html>\n", f); fclose(f); }
    }
    sim = reb_simulation_create();
    sim->save_messages = 1;
    reb_simulation_add_fmt(sim, "m", 1.0);
    reb_simulation_add_fmt(sim, "m a e", 1e-3, 1.0, 0.1);
    reb_simulation_add_fmt(sim, "m a e inc", 1e-4, 2.0, 0.2, 0.3);
    int base = (int)(getpid() % 40000);
    for (int i = 0; i < 64; i++){
        int port = 20000 + (base + i * 97) % 40000;
        // start_server returns once the thread is listening (or failed to bind: then connect fails).
        if (reb_simulation_start_server(sim, port) == 0){
            int fd = socket(AF_INET, SOCK_STREAM, 0);
            struct sockaddr_in a;
            memset(&a, 0, sizeof(a));
            a.sin_family = AF_INET;
            a.sin_port = htons((uint16_t)port);
            a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            int ok = fd >= 0 && connect(fd, (struct sockaddr*)&a, sizeof(a)) == 0;
            if (ok){
                // a complete harmless request, so the server thread is back in accept()
                const char req[] = "GET /favicon.ico HTTP/1.0\r\n\r\n";
                send(fd, req, sizeof(req) - 1, MSG_NOSIGNAL);
                shutdown(fd, SHUT_WR);
                char buf[4096];
                while (recv(fd, buf, sizeof(buf), 0) > 0) {}
            }
            if (fd >= 0) close(fd);
            if (ok){ server_port = port; initial_status = sim->status; return; }
        }
        reb_simulation_stop_server(sim);
    }
    fprintf(stderr, "fuzz_server_http: could not start the REBOUND server\n");
    abort();
}

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size){
    if (!sim) start_server();
    // /keyboard/<n> requests change sim->status (pause/run/quit); restore it so every input starts from
    // the same server state. The server thread is idle in accept() between inputs.
    // A /screenshot request sees the state reb_simulation_output_screenshot() leaves (see header).
    sim->status = memmem(data, size, "/screenshot", 11) ? REB_STATUS_SCREENSHOT : initial_status;
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return 0;
    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port = htons((uint16_t)server_port);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(fd, (struct sockaddr*)&a, sizeof(a)) != 0){ close(fd); return 0; }
    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
    uint8_t* req = malloc(size + 4);
    if (!req){ close(fd); return 0; }
    if (size) memcpy(req, data, size);
    memcpy(req + size, "\r\n\r\n", 4);
    size_t off = 0;
    while (off < size + 4){
        ssize_t w = send(fd, req + off, size + 4 - off, MSG_NOSIGNAL);
        if (w <= 0) break;
        off += (size_t)w;
    }
    free(req);
    shutdown(fd, SHUT_WR);
    char buf[4096];
    while (recv(fd, buf, sizeof(buf), 0) > 0) {}
    close(fd);
    // The server has closed the connection, so its thread is back in accept(): drop the decoded
    // screenshot (as the consumer of reb_simulation_output_screenshot() would).
    free(sim->server_data->screenshot);
    sim->server_data->screenshot = NULL;
    sim->server_data->N_screenshot = 0;
    return 0;
}

// Shared helper: REBOUND only exposes the Simulationarchive parser through a file name
// (reb_simulationarchive_create_from_file / reb_simulation_create_from_file), so each input is
// written to a fresh mkstemp() file under $TMPDIR and removed afterwards.
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int fuzz_write_tmp(const uint8_t* data, size_t size, char* path, size_t pathlen){
    const char* dir = getenv("TMPDIR");
    if (!dir || !*dir) dir = "/tmp";
    snprintf(path, pathlen, "%s/rebound-fuzz-XXXXXX", dir);
    int fd = mkstemp(path);
    if (fd < 0) return -1;
    size_t off = 0;
    while (off < size){
        ssize_t w = write(fd, data + off, size - off);
        if (w <= 0){ close(fd); unlink(path); return -1; }
        off += (size_t)w;
    }
    close(fd);
    return 0;
}

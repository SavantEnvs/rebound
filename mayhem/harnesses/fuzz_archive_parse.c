// Parse-only fuzzing of the REBOUND Simulationarchive / binary restart format.
// reb_simulationarchive_create_from_file() scans the blob index (including the corrupt-file
// recovery path), then every snapshot is decoded with reb_simulation_create_from_simulationarchive()
// (binarydata.c field decoder, particle / integrator state tables). No integration is done, so
// shallow integrator crashes cannot mask parser bugs.
#include "rebound.h"
#include "simulationarchive.h" // public REB_API header: struct reb_simulationarchive (nblobs)
#include "fuzz_common.h"

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size){
    char path[4096];
    if (fuzz_write_tmp(data, size, path, sizeof(path)) != 0) return 0;
    struct reb_simulationarchive* sa = reb_simulationarchive_create_from_file(path);
    if (sa){
        int64_t n = sa->nblobs;
        if (n > 16) n = 16; // bound work per input; snapshot -1 below still reaches the last blob
        for (int64_t i = 0; i < n; i++){
            struct reb_simulation* r = reb_simulation_create_from_simulationarchive(sa, i);
            if (r) reb_simulation_free(r);
        }
        struct reb_simulation* r = reb_simulation_create_from_simulationarchive(sa, -1);
        if (r) reb_simulation_free(r);
        reb_simulationarchive_free(sa);
    }
    unlink(path);
    return 0;
}

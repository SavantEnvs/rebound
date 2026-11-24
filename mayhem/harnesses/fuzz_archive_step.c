// Load a simulation from an untrusted Simulationarchive file (reb_simulation_create_from_file, last
// snapshot) and then USE the deserialized state: a few integrator steps with whatever integrator,
// gravity, collision and boundary modules the file selects, a save back to the binary format, and a
// deep copy.
#include "rebound.h"
#include "fuzz_common.h"

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size){
    char path[4096], out[4096];
    if (fuzz_write_tmp(data, size, path, sizeof(path)) != 0) return 0;
    struct reb_simulation* r = reb_simulation_create_from_file(path, -1);
    unlink(path);
    if (!r) return 0;
    r->save_messages = 1; // keep warnings off stdout (as the Python wrapper does)
    // Resource bound, not a validity check: gravity is O(N^2) per step, so a file declaring
    // thousands of particles would only measure CPU time. Loading/parsing is still fuzzed fully.
    if (r->N <= 64){
        reb_simulation_steps(r, 2);
        if (fuzz_write_tmp(NULL, 0, out, sizeof(out)) == 0){
            unlink(out); // save_to_file appends to an existing file; start from a fresh name
            reb_simulation_save_to_file(r, out);
            unlink(out);
        }
        struct reb_simulation* r2 = reb_simulation_copy(r);
        if (r2) reb_simulation_free(r2);
    }
    reb_simulation_free(r);
    return 0;
}

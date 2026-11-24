// Build a small simulation from fuzzer-chosen numbers through the public API and integrate it:
// orbit <-> cartesian conversion (tools.c), every built-in integrator's step code, and the WHFast
// Kepler solver on arbitrary (also non-finite) doubles.
//
// Layout: [0] integrator, [1] particle count, [2] N_active (1..N, or default), [3] testparticle_type,
//         [4] per-particle mode bits (1 = orbital elements, 0 = cartesian), then dt (double),
//         then 7 doubles per particle: m x y z vx vy vz  or  m a e inc Omega omega f.
#include <math.h>
#include <stdint.h>
#include <string.h>
#include "rebound.h"

static const char* integrators[] = {
    "ias15", "whfast", "sei", "leapfrog", "janus", "mercurius", "saba", "eos", "bs", "trace", "none",
};

static double rd(const uint8_t** p, size_t* n){
    double d = 0;
    if (*n >= sizeof(d)){ memcpy(&d, *p, sizeof(d)); *p += sizeof(d); *n -= sizeof(d); }
    return d;
}

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size){
    if (size < 5 + sizeof(double)) return -1;
    const char* integ = integrators[data[0] % (sizeof(integrators)/sizeof(integrators[0]))];
    unsigned np = 1 + data[1] % 8;
    int tpt = data[3] & 1;
    uint8_t modes = data[4];
    const uint8_t* p = data + 5;
    size_t n = size - 5;

    struct reb_simulation* r = reb_simulation_create();
    r->save_messages = 1;
    reb_simulation_set_integrator(r, integ);
    r->dt = rd(&p, &n);
    for (unsigned i = 0; i < np; i++){
        double v[7];
        for (int k = 0; k < 7; k++) v[k] = rd(&p, &n);
        if (i > 0 && (modes >> (i % 8)) & 1){
            reb_simulation_add_fmt(r, "m a e inc Omega omega f", v[0], v[1], v[2], v[3], v[4], v[5], v[6]);
        }else{
            reb_simulation_add_fmt(r, "m x y z vx vy vz", v[0], v[1], v[2], v[3], v[4], v[5], v[6]);
        }
    }
    // N_active must be in 1..N (rebound.h: "Number of active (i.e. not test-particle) particles";
    // particle 0 is the central body and always active: the Jacobi transformations in
    // transformations.c loop down to N_active-1 on a size_t, which 0 wraps). The largest value keeps
    // the default (SIZE_MAX = all active).
    unsigned na = 1 + data[2] % (r->N + 1);
    if (na <= r->N) r->N_active = na;
    r->testparticle_type = tpt;
    reb_simulation_steps(r, 50);
    if (r->N > 1){
        for (size_t i = 1; i < r->N; i++){
            struct reb_orbit o = reb_orbit_from_particle(r->G, r->particles[i], r->particles[0]);
            (void)o;
        }
    }
    reb_simulation_free(r);
    return 0;
}

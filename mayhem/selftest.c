// Known-answer functional self-test for librebound (run by mayhem/test.sh).
// Each check prints "PASS <name>" or "FAIL <name> <detail>"; test.sh counts the lines and requires
// every expected check to report PASS.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "rebound.h"
#include "simulationarchive.h"

static int failures = 0;
#define REPORT(name, ok, val) do{ if(ok) printf("PASS %s\n", name); else { printf("FAIL %s value=%.17g\n", name, (double)(val)); failures++; } }while(0)

// Two-body Kepler orbit: after exactly one period the planet is back where it started.
static void test_kepler_period(const char* integ, double tol){
    struct reb_simulation* r = reb_simulation_create();
    reb_simulation_set_integrator(r, integ);
    reb_simulation_add_fmt(r, "m", 1.0);
    reb_simulation_add_fmt(r, "m a e", 1e-6, 1.0, 0.1);
    double x0 = r->particles[1].x - r->particles[0].x, y0 = r->particles[1].y - r->particles[0].y;
    double P = 2.0*M_PI*sqrt(1.0/(1.0+1e-6));
    r->dt = P/1000.0;
    r->exact_finish_time = 1;
    reb_simulation_integrate(r, P);
    double dx = r->particles[1].x - r->particles[0].x - x0, dy = r->particles[1].y - r->particles[0].y - y0;
    double err = sqrt(dx*dx+dy*dy);
    char name[64]; snprintf(name, sizeof(name), "kepler_period_%s", integ);
    REPORT(name, err < tol && fabs(r->t - P) < 1e-9, err);
    reb_simulation_free(r);
}

// Orbital elements -> cartesian -> orbital elements round trip.
static void test_orbit_roundtrip(void){
    struct reb_particle star = {0}; star.m = 1.0;
    struct reb_particle p = reb_particle_from_orbit(1.0, star, 1e-3, 2.5, 0.3, 0.2, 0.7, 1.1, 0.4);
    struct reb_orbit o = reb_orbit_from_particle(1.0, p, star);
    double err = fabs(o.a-2.5)+fabs(o.e-0.3)+fabs(o.inc-0.2)+fabs(o.Omega-0.7)+fabs(o.omega-1.1)+fabs(o.f-0.4);
    REPORT("orbit_roundtrip", err < 1e-10, err);
    // circular orbit: r = a, speed = sqrt(G(M+m)/a)
    struct reb_particle c = reb_particle_from_orbit(1.0, star, 0.0, 4.0, 0.0, 0.0, 0.0, 0.0, 0.0);
    double d = sqrt(c.x*c.x+c.y*c.y+c.z*c.z), v = sqrt(c.vx*c.vx+c.vy*c.vy+c.vz*c.vz);
    REPORT("orbit_circular", fabs(d-4.0) < 1e-12 && fabs(v-0.5) < 1e-12, d);
}

// IAS15 conserves energy of a 3-body system to near machine precision.
static void test_energy_ias15(void){
    struct reb_simulation* r = reb_simulation_create();
    reb_simulation_add_fmt(r, "m", 1.0);
    reb_simulation_add_fmt(r, "m a e", 1e-3, 1.0, 0.05);
    reb_simulation_add_fmt(r, "m a e inc", 1e-3, 1.6, 0.1, 0.1);
    reb_simulation_move_to_com(r);
    double e0 = reb_simulation_energy(r);
    reb_simulation_integrate(r, 100.0);
    double e1 = reb_simulation_energy(r);
    double rel = fabs((e1-e0)/e0);
    REPORT("energy_ias15", rel < 1e-12 && e0 < 0 && r->N == 3, rel);
    reb_simulation_free(r);
}

// Simulationarchive: two snapshots written, read back with matching state.
static void test_archive_roundtrip(void){
    char path[] = "/tmp/rebound-selftest-XXXXXX";
    int fd = mkstemp(path); close(fd); unlink(path);
    struct reb_simulation* r = reb_simulation_create();
    reb_simulation_set_integrator(r, "whfast");
    reb_simulation_add_fmt(r, "m", 1.0);
    reb_simulation_add_fmt(r, "m a e", 1e-3, 1.0, 0.1);
    reb_simulation_add_fmt(r, "m a e", 2e-3, 2.0, 0.2);
    r->dt = 0.01;
    reb_simulation_save_to_file(r, path);
    double x0 = r->particles[2].x;
    reb_simulation_steps(r, 25);
    reb_simulation_save_to_file(r, path);
    double t1 = r->t, x1 = r->particles[2].x;
    struct reb_simulationarchive* sa = reb_simulationarchive_create_from_file(path);
    int ok = sa && sa->nblobs == 2;
    REPORT("archive_nblobs", ok, sa ? (double)sa->nblobs : -1.0);
    struct reb_simulation* a = sa ? reb_simulation_create_from_simulationarchive(sa, 0) : NULL;
    struct reb_simulation* b = sa ? reb_simulation_create_from_simulationarchive(sa, -1) : NULL;
    REPORT("archive_snapshot0", a && a->N == 3 && a->t == 0.0 && a->particles[2].x == x0
           && strcmp(a->integrator.name, "whfast") == 0, a ? a->particles[2].x : -1);
    REPORT("archive_snapshot_last", b && b->N == 3 && b->t == t1 && b->particles[2].x == x1, b ? b->t : -1);
    // continuing the loaded simulation must match continuing the original bit for bit
    if (b){ reb_simulation_steps(b, 10); }
    reb_simulation_steps(r, 10);
    REPORT("archive_continue", b && b->particles[1].x == r->particles[1].x, b ? b->particles[1].x - r->particles[1].x : -1);
    if (a) reb_simulation_free(a);
    if (b) reb_simulation_free(b);
    if (sa) reb_simulationarchive_free(sa);
    reb_simulation_free(r);
    unlink(path);
}

int main(void){
    test_kepler_period("whfast", 1e-9);
    test_kepler_period("ias15", 1e-9);
    test_kepler_period("leapfrog", 1e-3);
    test_kepler_period("saba", 1e-6);
    test_orbit_roundtrip();
    test_energy_ias15();
    test_archive_roundtrip();
    printf("DONE failures=%d\n", failures);
    return failures ? 1 : 0;
}

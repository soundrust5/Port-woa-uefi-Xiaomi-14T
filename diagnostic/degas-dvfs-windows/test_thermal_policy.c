#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "degas_thermal_policy.h"

static DEGAS_THERMAL_POLICY state;
static int temp[8];
static unsigned int rawcap[3] = {2200000, 3200000, 3350000};
static unsigned int cap;
static int temperature;
static void poll(unsigned long long now, int value)
{
    unsigned int i;
    for (i = 0; i < 8; ++i) temp[i] = value + (int)i;
    DegasThermalPolicyPoll(&state, now, temp, rawcap);
}
static int read(unsigned long long now, unsigned int domain)
{
    return DegasThermalPolicyRead(&state, now, domain, &cap, &temperature);
}
static void fresh(void)
{
    memset(&state, 0, sizeof(state));
    poll(0, 40000); poll(100, 40010); poll(200, 40020);
    assert(read(200, 0) && cap == 2200000 && temperature == 40023);
    assert(read(200, 1) && cap == 3200000 && temperature == 40026);
    assert(read(200, 2) && cap == 3350000 && temperature == 40027);
}
int main(void)
{
    unsigned int i;
    assert(!read(0, 0));
    poll(0, 40000); assert(!read(0, 0));
    poll(10, 40010); poll(20, 40020);
    assert(!read(100, 0)); /* Two changes compressed below100ms cannot qualify. */
    poll(100, 40030); assert(read(100, 0));
    fresh();
    assert(read(2199, 0)); assert(!read(2200, 0)); /* Strict2s change-age cutoff. */
    poll(2200, 41000); assert(!read(2200, 0)); /* Requalify after stale gap. */
    poll(2300, 41010); assert(!read(2300, 0));
    poll(2400, 41020); assert(read(2400, 0));
    fresh();
    for (i = 0; i < 8; ++i) {
        fresh(); temp[i] = -274000;
        DegasThermalPolicyPoll(&state, 300, temp, rawcap);
        assert(!read(300, 0) && !read(300, 1) && !read(300, 2));
    }
    fresh(); temp[7] = 115001;
    DegasThermalPolicyPoll(&state, 300, temp, rawcap); assert(!read(300, 0));
    fresh(); temp[0] = -1;
    DegasThermalPolicyPoll(&state, 300, temp, rawcap); assert(!read(300, 0));
    fresh();
    poll(300, 60000);
    assert(read(300, 0) && cap == 1300000);
    assert(read(300, 1) && cap == 1300000);
    assert(read(300, 2) && cap == 1200000);
    poll(400, 56000); assert(read(400, 0) && cap == 1300000);
    poll(500, 54990); assert(read(500, 0) && cap == 2200000);
    poll(600, 70000);
    assert(read(600, 0) && cap == 480000);
    assert(read(600, 1) && cap == 400000);
    assert(read(600, 2) && cap == 400000);
    poll(700, 66000); assert(read(700, 0) && cap == 480000);
    poll(800, 64990); assert(read(800, 0) && cap == 1300000);
    poll(900, 56000); assert(read(900, 0) && cap == 1300000);
    poll(1000, 54990); assert(read(1000, 0) && cap == 2200000);
    temp[0] = 70000; /* A hot little core must cap its whole domain only. */
    DegasThermalPolicyPoll(&state, 1050, temp, rawcap);
    assert(read(1050, 0) && cap == 480000);
    assert(read(1050, 1) && cap == 3200000);
    assert(read(1050, 2) && cap == 3350000);
    rawcap[0] = 800000;
    poll(1100, 60000); assert(read(1100, 0) && cap == 800000); /* Never raise OEM cap. */
    rawcap[0] = 2200001;
    poll(1200, 50000); assert(!read(1200, 0));
    rawcap[0] = 479999;
    poll(1300, 50010); assert(!read(1300, 0));
    rawcap[0] = 2200000;
    fresh();
    temp[7] = 60000;
    DegasThermalPolicyPoll(&state, 300, temp, rawcap);
    assert(read(300, 2) && cap == 1200000);
    temp[7] = 55001;
    DegasThermalPolicyPoll(&state, 400, temp, rawcap);
    assert(read(400, 2) && cap == 1200000);
    temp[7] = 55000;
    DegasThermalPolicyPoll(&state, 500, temp, rawcap);
    assert(read(500, 2) && cap == 3350000);
    temp[7] = 70000;
    DegasThermalPolicyPoll(&state, 600, temp, rawcap);
    assert(read(600, 2) && cap == 400000);
    temp[7] = 65001;
    DegasThermalPolicyPoll(&state, 700, temp, rawcap);
    assert(read(700, 2) && cap == 400000);
    temp[7] = 65000;
    DegasThermalPolicyPoll(&state, 800, temp, rawcap);
    assert(read(800, 2) && cap == 1200000);
    fresh();
    for (i = 0; i < 8; ++i) temp[i] += (i == 7 ? 0 : 10);
    DegasThermalPolicyPoll(&state, 2100, temp, rawcap);
    assert(read(2199, 0)); assert(!read(2200, 0)); /* One frozen core closes all domains. */
    assert(!read(199, 0)); /* Monotonic clock rollback rejected. */
    assert(!read(2100, 3));
    DegasThermalPolicyInvalidate(&state); assert(!read(2100, 0));
    puts("PASS: startup, all8-core freshness, sentinels, stale/requalification, caps and hysteresis");
    return 0;
}

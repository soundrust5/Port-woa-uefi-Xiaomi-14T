#ifndef DEGAS_THERMAL_POLICY_H
#define DEGAS_THERMAL_POLICY_H
/* Shared implementation used by the kernel observer and mock-time host tests.
 * Times are monotonic milliseconds. No firmware writes or OS dependencies. */
typedef struct {
    int LastTemperature;
    unsigned int Changes;
    unsigned long long FirstMs, LastChangeMs;
    int Initialized;
} DEGAS_THERMAL_CORE;
typedef struct {
    DEGAS_THERMAL_CORE Cores[8];
    unsigned int Ceiling[3], Stage[3];
    unsigned long long PollMs;
    int HavePoll;
} DEGAS_THERMAL_POLICY;

static __inline void DegasThermalPolicyInvalidate(DEGAS_THERMAL_POLICY *State)
{
    unsigned int i;
    State->HavePoll = 0;
    for (i = 0; i < 8; ++i) State->Cores[i].Initialized = 0;
}

static __inline void DegasThermalPolicyPoll(DEGAS_THERMAL_POLICY *State,
    unsigned long long NowMs, const int Temperature[8], const unsigned int Ceiling[3])
{
    unsigned int i;
    static const unsigned int first[3] = {0, 4, 7}, count[3] = {4, 3, 1};
    for (i = 0; i < 8; ++i) {
        DEGAS_THERMAL_CORE *core = &State->Cores[i];
        int value = Temperature[i];
        if (value < 0 || value > 115000) {
            core->Initialized = 0; /* Includes firmware invalid -274000. */
            continue;
        }
        if (!core->Initialized || NowMs < core->LastChangeMs ||
            NowMs - core->LastChangeMs >= 2000) {
            core->Initialized = 1;
            core->FirstMs = core->LastChangeMs = NowMs;
            core->Changes = 0;
        } else if (value != core->LastTemperature) {
            core->LastChangeMs = NowMs;
            if (core->Changes < 2) ++core->Changes;
        }
        core->LastTemperature = value;
    }
    for (i = 0; i < 3; ++i) {
        unsigned int j;
        int max = -1;
        State->Ceiling[i] = Ceiling[i];
        for (j = first[i]; j < first[i] + count[i]; ++j) {
            if (!State->Cores[j].Initialized) { max = 115001; break; }
            if (Temperature[j] > max) max = Temperature[j];
        }
        if (max >= 70000) State->Stage[i] = 2;
        else if (State->Stage[i] == 2 && max > 65000) { /* Retain hot stage. */ }
        else if (max >= 60000) State->Stage[i] = 1;
        else if (State->Stage[i] != 0 && max > 55000) State->Stage[i] = 1;
        else State->Stage[i] = 0;
    }
    State->PollMs = NowMs;
    State->HavePoll = 1;
}

static __inline int DegasThermalPolicyRead(const DEGAS_THERMAL_POLICY *State,
    unsigned long long NowMs, unsigned int Domain, unsigned int *CeilingKhz,
    int *TemperatureMilliC)
{
    static const unsigned int first[3] = {0, 4, 7}, count[3] = {4, 3, 1};
    static const unsigned int minimum[3] = {480000, 400000, 400000};
    static const unsigned int maximum[3] = {2200000, 3200000, 3350000};
    static const unsigned int soft[3] = {1300000, 1300000, 1200000};
    unsigned int i, cap;
    int max = -1;
    *CeilingKhz = 0; *TemperatureMilliC = 0;
    if (Domain >= 3 || !State->HavePoll || NowMs < State->PollMs ||
        NowMs - State->PollMs >= 2000) return 0;
    /* All8 sources were demonstrated live in the Windows qualification run.
     * Require that complete source set, even for a single domain request. */
    for (i = 0; i < 8; ++i) {
        const DEGAS_THERMAL_CORE *core = &State->Cores[i];
        if (!core->Initialized || core->Changes < 2 || NowMs < core->FirstMs ||
            core->LastChangeMs < core->FirstMs ||
            core->LastChangeMs - core->FirstMs < 100 || NowMs < core->LastChangeMs ||
            NowMs - core->LastChangeMs >= 2000) return 0;
    }
    cap = State->Ceiling[Domain];
    if (cap < minimum[Domain] || cap > maximum[Domain]) return 0;
    for (i = first[Domain]; i < first[Domain] + count[Domain]; ++i)
        if (State->Cores[i].LastTemperature > max) max = State->Cores[i].LastTemperature;
    if (State->Stage[Domain] == 2 && cap > minimum[Domain]) cap = minimum[Domain];
    else if (State->Stage[Domain] == 1 && cap > soft[Domain]) cap = soft[Domain];
    *CeilingKhz = cap; *TemperatureMilliC = max;
    return 1;
}
#endif

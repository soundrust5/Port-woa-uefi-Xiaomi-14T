#include "degas_protocol.h"

int DegasDecodeProcessorPath(const unsigned short *Path, unsigned int Characters)
{
    unsigned int at;
    if (!Path || (Characters != 9 && Characters != 10)) return -1;
    if (Path[0] != '\\' || Path[1] != '_' || Path[2] != 'S' || Path[3] != 'B')
        return -1;
    at = 4;
    if (Characters == 10) {
        if (Path[at++] != '_') return -1;
    }
    if (Path[at++] != '.' || Path[at++] != 'C' || Path[at++] != '0' ||
        Path[at++] != '0' || Path[at] < '0' || Path[at] > '7') return -1;
    return (int)(Path[at] - '0');
}

unsigned int DegasDomainFromUid(unsigned int Uid)
{
    if (Uid < 4) return 0;
    if (Uid < 7) return 1;
    if (Uid == 7) return 2;
    return 3; /* Invalid domain. */
}

int DegasSelectRequest(unsigned int HardwareMinimum, unsigned int HardwareMaximum,
    unsigned int CurrentCeiling, unsigned int RequestedMinimum,
    unsigned int RequestedMaximum, unsigned int Desired,
    unsigned int *Selected)
{
    unsigned int lo, hi;
    if (!Selected || HardwareMinimum == 0 || HardwareMinimum > HardwareMaximum ||
        RequestedMinimum > RequestedMaximum) return 0;
    lo = RequestedMinimum > HardwareMinimum ? RequestedMinimum : HardwareMinimum;
    hi = RequestedMaximum < HardwareMaximum ? RequestedMaximum : HardwareMaximum;
    if (CurrentCeiling < hi) hi = CurrentCeiling;
    if (lo > hi) return 0;
    if (Desired < lo) Desired = lo;
    if (Desired > hi) Desired = hi;
    *Selected = Desired;
    return 1;
}

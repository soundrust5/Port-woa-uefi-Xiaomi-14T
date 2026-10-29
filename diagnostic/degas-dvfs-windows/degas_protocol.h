#ifndef DEGAS_PROTOCOL_H
#define DEGAS_PROTOCOL_H
/* Shared by the kernel provider and native host tests; no Windows dependencies. */
int DegasDecodeProcessorPath(const unsigned short *Path, unsigned int Characters);
unsigned int DegasDomainFromUid(unsigned int Uid);
int DegasSelectRequest(unsigned int HardwareMinimum, unsigned int HardwareMaximum,
    unsigned int CurrentCeiling, unsigned int RequestedMinimum,
    unsigned int RequestedMaximum, unsigned int Desired,
    unsigned int *Selected);
#endif

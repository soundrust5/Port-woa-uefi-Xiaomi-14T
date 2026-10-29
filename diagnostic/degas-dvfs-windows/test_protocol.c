#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "degas_protocol.h"

static int path(const char *s)
{
    unsigned short utf16[64];
    unsigned int i, n = (unsigned int)strlen(s);
    assert(n < 64);
    for (i = 0; i < n; ++i) utf16[i] = (unsigned char)s[i];
    return DegasDecodeProcessorPath(utf16, n);
}

int main(void)
{
    unsigned int uid, selected = 0xabcdef;
    char name[] = "\\_SB.C000";
    for (uid = 0; uid < 8; ++uid) {
        name[8] = (char)('0' + uid);
        assert(path(name) == (int)uid);
        assert(DegasDomainFromUid(uid) == (uid < 4 ? 0U : uid < 7 ? 1U : 2U));
    }
    assert(path("\\_SB_.C007") == 7);
    assert(path("\\_SB.C008") == -1);
    assert(path("\\_SB.C000.extra") == -1);
    assert(path("\\_SB.C00") == -1);
    assert(path("\\_SB.P000") == -1);
    assert(path("ACPI\\ACPI0007\\0") == -1);
    assert(DegasDomainFromUid(8) == 3);
    assert(DegasSelectRequest(480000,2200000,2200000,480000,2200000,1400000,&selected));
    assert(selected == 1400000);
    assert(DegasSelectRequest(480000,2200000,1800000,480000,2200000,2200000,&selected));
    assert(selected == 1800000);
    assert(DegasSelectRequest(400000,3350000,3350000,400000,3350000,3350000,&selected));
    assert(selected == 3350000);
    assert(DegasSelectRequest(400000,3200000,3200000,400000,3200000,0,&selected));
    assert(selected == 400000);
    selected = 0xabcdef;
    assert(!DegasSelectRequest(480000,2200000,400000,480000,2200000,2000000,&selected));
    assert(selected == 0xabcdef); /* Invalid request cannot produce an output. */
    assert(!DegasSelectRequest(480000,2200000,2200000,2200000,480000,1000000,&selected));
    assert(!DegasSelectRequest(0,2200000,2200000,0,2200000,0,&selected));
    puts("PASS: eight exact CPU identities, 4/3/1 domains, bounds, thermal ceiling, stock maxima, malformed requests");
    return 0;
}

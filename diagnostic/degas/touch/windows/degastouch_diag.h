#ifndef DEGAS_TOUCH_DIAG_H
#define DEGAS_TOUCH_DIAG_H
#include <stdint.h>
#define DEGAS_TOUCH_DIAG_MAGIC 0x31544744u
typedef struct {
 uint32_t Magic,Size,Version,Stage,StartStatus,IoError,CleanupError;
 uint32_t Reads,Good,Bad,Reports,Polls,Active,RawMode,Id,LastX,LastY,LastEvent;
} DEGAS_TOUCH_DIAGNOSTIC;
#endif

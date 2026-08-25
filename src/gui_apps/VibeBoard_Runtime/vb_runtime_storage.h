#ifndef VB_RUNTIME_STORAGE_H
#define VB_RUNTIME_STORAGE_H

#include <rtthread.h>

/* The SiFli SPI-MSD driver has a known unreliable multi-block read path. */
#define VB_RUNTIME_STORAGE_IO_CHUNK_BYTES 512u

int vb_runtime_storage_take(uint32_t timeout_ms);
void vb_runtime_storage_release(void);

#endif

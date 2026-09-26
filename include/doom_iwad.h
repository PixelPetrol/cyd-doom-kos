#ifndef DOOM_IWAD_H
#define DOOM_IWAD_H

// On the ESP32 the IWAD lives in a memory-mapped flash partition
// (see src/port/wad_mmap.c) rather than a linked-in array.
extern const unsigned char* doom_iwad;
extern unsigned int doom_iwad_len;

#endif // DOOM_IWAD_H

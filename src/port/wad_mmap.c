// Zmienione dla K-OS 2026-09-24 (galaz kos, na bazie HenrysCat/cyd-doom 1c58bf4, GPL-2.0): dane gry z ogona biegnacej partycji zamiast partycji "wad".
// Memory-maps the game data and exposes it as doom_iwad, replacing GBADoom's linked-in C array.
// K-OS: dane z ogona wlasnego slotu (kos.cpp), nie z partycji "wad" upstreamu.
#include <stdint.h>
#include "esp_partition.h"
#include "spi_flash_mmap.h"
#include "esp_ota_ops.h"
#include "kos.h"
#include "esp_log.h"
#include "doomport.h"

const unsigned char* doom_iwad = 0;
unsigned int doom_iwad_len = 0;

extern void I_Error(const char* error, ...);

void WadInit(void)
{
    // K-OS: dane leza w ogonie BIEGNACEJ partycji (ota_0), za obrazem aplikacji - nie ma
    // osobnej partycji "wad", bo programowi K-OS nie wolno zmieniac tablicy partycji.
    const esp_partition_t* part = esp_ota_get_running_partition();
    uint32_t off = 0, len = 0;
    KosDataWhere(&off, &len);

    if (!part || !len)
        I_Error("WadInit: brak danych gry w slocie");

    const void* ptr = 0;
    esp_partition_mmap_handle_t handle;

    esp_err_t err = esp_partition_mmap(part, off, len,
                                       ESP_PARTITION_MMAP_DATA, &ptr, &handle);
    if (err != ESP_OK)
        I_Error("WadInit: mmap failed (%d)", (int)err);

    const unsigned char* p = (const unsigned char*)ptr;

    if (p[0] != 'I' || p[1] != 'W' || p[2] != 'A' || p[3] != 'D')
        I_Error("WadInit: dane w slocie to nie IWAD");

    doom_iwad = p;
    doom_iwad_len = len;

    ESP_LOGI("wad", "IWAD mapped at %p (%u bytes)", ptr, (unsigned)len);
}

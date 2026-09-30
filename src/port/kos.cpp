// DOOM dla K-OS - (c) 2026 Piotr Korona. Licencja: GPL-2.0 lub pozniejsza (jak silnik, patrz LICENSE).
// Warstwa K-OS portu DOOM - opis w kos.h, liczby i uzasadnienie w README-KOS.md.
#include <Arduino.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <esp_partition.h>
#include <esp_ota_ops.h>
#include <esp_image_format.h>
#include <esp_flash.h>
#include <esp_flash_internal.h>
#include <esp_rom_crc.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <spi_flash_mmap.h>
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "doomport.h"
#include "kos.h"
#include "safewrite.h"

#define SD_SCK  18
#define SD_MISO 19
#define SD_MOSI 23
#define SD_CS    5

// Naglowek danych gry w ogonie slotu. Lezy NA NAJNIZSZYM adresie obszaru danych i jest
// zapisywany JAKO OSTATNI. Kazdy inny program K-OS i samoaktualizacja K-OS pisza slot od
// zera w gore jednym ciaglym kawalkiem (esp_ota_begin kasuje dlugosc obrazu zaokraglona
// do 4 kB), wiec obraz, ktory siegnie naszych danych, ZAWSZE najpierw zamaze naglowek.
// Stad naglowek wystarcza jako czujka "ktos tu pisal". CRC calosci liczymy i tak - kosztuje
// ulamek sekundy, a uszkodzony WAD w silniku to wywrotka, ktorej nikt by nie zdiagnozowal.
#define KD_MAGIC "KDOOMDT1"
#define KD_VER   1u
#define KD_HDR_SECTOR 4096u

// Format pliku /doom/doom.kwad (kos/wad2kos.py): IWAD, w nim lump KOSINFO zaczynajacy sie od
// KOS_KWAD_MAGIC i lump KOSSTBAR (pasek stanu, st_stuff.c), katalog lumpow na koncu, a za nim
// 4 B CRC-32 (LE) wszystkiego przed nimi. CRC calego pliku razem z tym dopiskiem daje wtedy
// zawsze te sama reszte, wiec plik uszkodzony na karcie (przerwane kopiowanie, zly sektor)
// wychodzi przy instalacji jednym porownaniem, zamiast jako wywrotka silnika w srodku gry.
#define KOS_KWAD_MAGIC   "KOSDOOM2"
#define KD_CRC_RESIDUE   0x2144DF1Cu
#define KD_MAX_LUMPS     8192u
#define KOS_STBAR_MAGIC  "KOSSTBR1"
#define KOS_STBAR_LUMPLEN (64u + 240u * 32u)   // musi sie zgadzac z KOS_STBAR_LEN w st_stuff.c

typedef struct {
    char     magic[8];
    uint32_t ver;
    uint32_t dataOff;     // polozenie naglowka w partycji (musi sie zgadzac z biezacym obrazem)
    uint32_t wadLen;
    uint32_t wadCrc;
    uint32_t srcSize;     // plik na karcie, z ktorego dane skopiowano
    uint32_t srcMtime;
    uint32_t hdrCrc;      // CRC wszystkich pol powyzej
} kd_hdr_t;

kos_cfg_t g_kos;
int g_kosSaveFailed = 0;

static sdmmc_card_t* s_card = nullptr;
static bool s_mounted = false;
static uint32_t s_dataOff = 0, s_dataLen = 0;
static bool s_ui = false;
static bool s_boot = false;   // miedzy KosPrepare a KosPrepareDone karta jest juz sprawdzona
static uint32_t s_mountFailAt = 0;   // ostatnie nieudane montowanie w trakcie gry

// =======================================================================================
// Etap startu w logu (0.1.1). W 0.1.0 po "[lcd] using 40 MHz" bywala minuta ciszy: ekrany
// K-OS czekaly na dotyk bez slowa na Serial, a kopia danych pisala dopiero na koncu. Teraz
// esp_timer co 3 s mowi, gdzie program jest i ile zrobil - kazde czekanie widac w logu.
// =======================================================================================
static const char* volatile s_stage = nullptr;
static volatile uint32_t s_stageAt = 0, s_stageDone = 0, s_stageTotal = 0;
static esp_timer_handle_t s_stageTimer = nullptr;

static void stageTick(void*)
{
    const char* st = s_stage;
    if (!st) return;
    uint32_t t = (millis() - s_stageAt) / 1000;
    if (s_stageTotal)
        Serial.printf("[doom] etap: %s (%u s) %u / %u B\n", st, (unsigned)t, (unsigned)s_stageDone, (unsigned)s_stageTotal);
    else
        Serial.printf("[doom] etap: %s (%u s)\n", st, (unsigned)t);
}

extern "C" void KosStage(const char* name)
{
    if (!name) {
        if (s_stageTimer) { esp_timer_stop(s_stageTimer); esp_timer_delete(s_stageTimer); s_stageTimer = nullptr; }
        s_stage = nullptr;
        return;
    }
    s_stageDone = s_stageTotal = 0;
    s_stageAt = millis();
    s_stage = name;
    Serial.printf("[doom] etap: %s\n", name);
    if (!s_stageTimer) {
        esp_timer_create_args_t a = {};
        a.callback = stageTick;
        a.name = "doometap";
        if (esp_timer_create(&a, &s_stageTimer) == ESP_OK) esp_timer_start_periodic(s_stageTimer, 3000000);
        else s_stageTimer = nullptr;
    }
}

extern "C" void KosStageProgress(uint32_t done, uint32_t total)
{
    s_stageDone = done; s_stageTotal = total;
}

// =======================================================================================
// Model B
// =======================================================================================
void KosModelB(void)
{
    const esp_partition_t* od = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_OTA, NULL);
    if (od) esp_partition_erase_range(od, 0, od->size);
}

void KosExitToMenu(void)
{
    Serial.println("K-OS: powrot do menu");
    Serial.flush();
    delay(50);
    esp_restart();
}

// =======================================================================================
// Karta SD (VSPI). Montowanie na zadanie - patrz kos.h.
// =======================================================================================
static bool sdMount(void)
{
    if (s_mounted) return true;
    // Bez karty kazda zmiana w menu ustawien to 3 proby po 300 ms zawieszenia gry. Po porazce
    // dajemy karcie 20 s spokoju - wlozona pozniej i tak zostanie zauwazona przy nastepnym zapisie.
    if (s_mountFailAt && millis() - s_mountFailAt < 20000) return false;
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SPI3_HOST;
    host.max_freq_khz = 4000;          // jak w K-OS i grach: 20 MHz wywala send_scr na tej plytce
    spi_bus_config_t bus = {};
    bus.mosi_io_num = SD_MOSI; bus.miso_io_num = SD_MISO; bus.sclk_io_num = SD_SCK;
    bus.quadwp_io_num = -1; bus.quadhd_io_num = -1; bus.max_transfer_sz = 4000;
    esp_err_t e = spi_bus_initialize((spi_host_device_t)host.slot, &bus, SDSPI_DEFAULT_DMA);
    if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) {
        Serial.printf("SD: spi_bus_initialize blad 0x%x\n", (unsigned)e);
        return false;
    }
    sdspi_device_config_t dev = SDSPI_DEVICE_CONFIG_DEFAULT();
    dev.gpio_cs = (gpio_num_t)SD_CS;
    dev.host_id = (spi_host_device_t)host.slot;
    esp_vfs_fat_sdmmc_mount_config_t mc = {};
    mc.format_if_mount_failed = false;   // karta uzytkownika ze stanem K-OS - nigdy nie formatujemy
    // IDF 5.5 bierze kontekst FAT JEDNYM blokiem (vfs_fat.c): FATFS z buforem sektora 4 kB,
    // bufory sciezek i max_files x FIL (~4,6 kB kazdy). Przy 1 pliku to ~13 kB ciaglej sterty
    // w trakcie gry, przy 2 juz ~18 kB. safewrite otwiera jeden plik naraz, wiec 1 wystarcza.
    mc.max_files = 1;
    mc.allocation_unit_size = 16 * 1024;
    esp_err_t me = ESP_FAIL;
    // Po NAGLYM resecie karta potrafi odpowiadac, ale nie zainicjowac sie - trzy proby jak w Office.
    for (int i = 1; i <= 3; i++) {
        me = esp_vfs_fat_sdspi_mount("/sd", &host, &dev, &mc, &s_card);
        if (me == ESP_OK) break;
        Serial.printf("SD: montowanie nieudane (proba %d/3, blad 0x%x)\n", i, (unsigned)me);
        if (i < 3) delay(300);
    }
    if (me != ESP_OK) { spi_bus_free(SPI3_HOST); s_mountFailAt = millis() | 1; return false; }
    s_mountFailAt = 0;
    s_mounted = true;
    return true;
}

static void sdUnmount(void)
{
    if (!s_mounted) return;
    esp_vfs_fat_sdcard_unmount("/sd", s_card);
    s_card = nullptr;
    spi_bus_free(SPI3_HOST);
    s_mounted = false;
}

// Jedna linia bez znakow konca; false = koniec pliku.
static bool readLine(FILE* f, char* line, size_t n)
{
    if (!fgets(line, (int)n, f)) return false;
    size_t l = strlen(line);
    while (l && (line[l - 1] == '\n' || line[l - 1] == '\r')) line[--l] = 0;
    return true;
}

// =======================================================================================
// Ustawienia K-OS (/sd/korona/ustawienia.txt) - te same klucze co kos_shared.h::kosSettingsLoad.
// Wlasny czytnik, bo kos_shared.h ciagnie WiFi.h (+77 kB obrazu, zmierzone w grach), a kazdy
// kilobajt obrazu to kilobajt mniej na dane gry w ogonie slotu.
// =======================================================================================
static void settingsLoad(void)
{
    g_kos.bg = 0x0000; g_kos.txt = 0x0620; g_kos.txt2 = 0x0360; g_kos.acc = 0x07E0; g_kos.err = 0xF800;
    g_kos.bright = 90; g_kos.invert = 0; g_kos.en = 0;
    FILE* f = fopen("/sd/korona/ustawienia.txt", "r");
    if (!f) return;
    char line[160];
    while (readLine(f, line, sizeof(line))) {
        if (line[0] == '#' || !line[0]) continue;
        char* eq = strchr(line, '='); if (!eq) continue;
        *eq = 0; const char* k = line; const char* v = eq + 1;
        long n = strtol(v, nullptr, 0);
        if      (!strcmp(k, "bg"))   g_kos.bg = (uint16_t)n;
        else if (!strcmp(k, "txt"))  g_kos.txt = (uint16_t)n;
        else if (!strcmp(k, "txt2")) g_kos.txt2 = (uint16_t)n;
        else if (!strcmp(k, "acc"))  g_kos.acc = (uint16_t)n;
        else if (!strcmp(k, "err"))  g_kos.err = (uint16_t)n;
        else if (!strcmp(k, "jasnosc")) g_kos.bright = (uint8_t)(n < 5 ? 5 : n > 100 ? 100 : n);
        else if (!strcmp(k, "odwrocenie")) g_kos.invert = (n != 0);
        else if (!strcmp(k, "jezyk")) g_kos.en = (strncmp(v, "en", 2) == 0);
    }
    fclose(f);
}

// =======================================================================================
// Kalibracja dotyku - ta sama logika co kos_shared.h::kosTouchLoad (te same trzy sciezki,
// te same odrzucenia), bez WiFi.h.
// =======================================================================================
static void touchCalLoad(void)
{
#if defined(CYD_BOARD_28)
    static const uint16_t def[5] = { 300, 3500, 300, 3500, 1 };   // zgadywanka z kos_board.h
#else
    static const uint16_t def[5] = { 425, 3360, 392, 3504, 3 };   // sprawdzona na 2.4"
#endif
    memcpy(g_kos.cal, def, sizeof(def));
    g_kos.calFromCard = 0;
    static const char* paths[3] = {
        "/sd/korona/" KOS_BOARD_ID "/dotyk.txt",
        "/sd/korona/dotyk-" KOS_BOARD_ID ".txt",
        "/sd/korona/dotyk.txt",
    };
    FILE* f = nullptr;
    for (int i = 0; i < 3 && !f; i++) f = fopen(paths[i], "r");
    if (!f) { Serial.println("dotyk: brak pliku kalibracji K-OS - domyslne liczby"); return; }
    char line[160];
    while (readLine(f, line, sizeof(line))) {
        if (line[0] == '#' || !line[0]) continue;
        long v[6]; int k = 0;
        for (char* t = strtok(line, "\t "); t && k < 6; t = strtok(nullptr, "\t ")) v[k++] = strtol(t, nullptr, 10);
        if (k != 6 || v[0] < 0 || v[0] > 3 || v[2] < 100 || v[4] < 100) continue;
        // Przeliczenie na poziom w input.cpp zaklada kalibracje z rotacji 0 - tak zapisuje K-OS.
        if (v[0] != 0) { Serial.printf("dotyk: kalibracja dla rotacji %ld - pomijam\n", v[0]); continue; }
        for (int i = 0; i < 5; i++) g_kos.cal[i] = (uint16_t)v[i + 1];
        g_kos.calFromCard = 1;
        break;
    }
    fclose(f);
    Serial.printf("dotyk: kalibracja {%u,%u,%u,%u,%u} %s\n", g_kos.cal[0], g_kos.cal[1], g_kos.cal[2],
                  g_kos.cal[3], g_kos.cal[4], g_kos.calFromCard ? "z karty" : "domyslna");
}

// =======================================================================================
// /sd/doom/ekran.txt - reczne ustawienie orientacji obrazu, bez przebudowy programu.
// Jedna linia "madctl=0xNN" (tylko wartosci poziome - z bitem MV 0x20). Dotyk odbija sie razem
// z obrazem (input.cpp). Przyklady dla ILI9341 (cyd24/cyd28; domyslnie 0x28):
//   0xA8 = odbicie lewo-prawo, 0x68 = odbicie gora-dol, 0xE8 = obrot o 180 stopni.
// =======================================================================================
static void ekranLoad(void)
{
    g_kos.madctl = 0;
    FILE* f = fopen("/sd/doom/ekran.txt", "r");
    if (!f) return;
    char line[96];
    while (readLine(f, line, sizeof(line))) {
        if (line[0] == '#' || strncmp(line, "madctl=", 7)) continue;
        long v = strtol(line + 7, nullptr, 0);
        if (v > 0 && v < 256 && (v & 0x20)) g_kos.madctl = (uint8_t)v;
        else Serial.printf("EKRAN: madctl=%s odrzucone (potrzebny bit MV 0x20, np. 0x28 0xA8 0x68 0xE8)\n", line + 7);
    }
    fclose(f);
    if (g_kos.madctl) Serial.printf("EKRAN: MADCTL 0x%02x z /doom/ekran.txt\n", g_kos.madctl);
}

// =======================================================================================
// Ekrany (320x240, font 5x7)
// =======================================================================================
static void uiBegin(void)
{
    if (!s_ui) { DisplayInit(); InputInit(); s_ui = true; }
    DisplayFillRect(0, 0, 320, 240, g_kos.bg);
}

// Zwalnia bufory DMA ekranu i magistrale, zeby Z_Init dostal ten sam najwiekszy blok sterty,
// co przy starcie bez ekranow. Upstream: "Z_Init must run before display allocations".
static void uiEnd(void)
{
    if (!s_ui) return;
    InputDeinit();
    DisplayDeinit();
    s_ui = false;
}

static void uiText(int x, int y, int scale, uint16_t fg, const char* s)
{
    DisplayDrawString(x, y, s, scale, fg, g_kos.bg);
}

static void uiCenter(int y, int scale, uint16_t fg, const char* s)
{
    int w = (int)strlen(s) * 6 * scale;
    int x = (320 - w) / 2; if (x < 0) x = 0;
    uiText(x, y, scale, fg, s);
}

static void uiBar(int y, uint32_t done, uint32_t total)
{
    static int last = -1;
    int w = total ? (int)((uint64_t)done * 280 / total) : 0;
    if (w == last) return;
    last = w;
    DisplayFillRect(20, y, 280, 12, g_kos.txt2);
    DisplayFillRect(21, y + 1, 278, 10, g_kos.bg);
    if (w > 2) DisplayFillRect(21, y + 1, w - 2, 10, g_kos.acc);
}

// Czekanie na dotyk albo BOOT. 0.1.1: slad w logu co 5 s (nacisk z XPT2046 - gdyby dotyk nie
// dzialal, widac to od razu) i limit 3 min, po ktorym program sam wraca do K-OS. RST tez wraca
// do K-OS (Model B), wiec plytka nigdy nie zostaje "zawieszona" na tym ekranie.
static void uiWaitTouch(const char* why)
{
    int x, y;
    Serial.printf("EKRAN: %s - czekam na dotyk albo BOOT (RST tez wraca do K-OS)\n", why);
    KosStage(why);
    // Najpierw puszczenie: dotyk, ktorym uruchomiono program w K-OS, nie moze od razu wyjsc.
    uint32_t t0 = millis();
    while ((InputRawTouch(&x, &y) > 350 || InputBootPressed()) && millis() - t0 < 3000) delay(20);
    uint32_t lastLog = millis();
    for (;;) {
        int z = InputRawTouch(&x, &y);
        if (z > 350) { Serial.printf("EKRAN: dotyk (z=%d x=%d y=%d) - powrot do K-OS\n", z, x, y); break; }
        if (InputBootPressed()) { Serial.println("EKRAN: BOOT - powrot do K-OS"); break; }
        if (millis() - lastLog >= 5000) {
            lastLog = millis();
            Serial.printf("EKRAN: nadal czekam (%u s), dotyk z=%d\n", (unsigned)((lastLog - t0) / 1000), z);
        }
        if (millis() - t0 > 180000) { Serial.println("EKRAN: 3 min bez dotyku - sam wracam do K-OS"); break; }
        delay(30);
    }
}

// Ekran konca drogi: komunikat i powrot do K-OS po dotyku. Nie wraca.
static void uiFatal(const char* l1, const char* l2, const char* l3, const char* l4)
{
    Serial.printf("BLAD: %s | %s | %s | %s\n", l1, l2 ? l2 : "", l3 ? l3 : "", l4 ? l4 : "");
    uiBegin();
    uiCenter(18, 3, g_kos.acc, "DOOM");
    uiCenter(62, 2, g_kos.err, l1);
    if (l2) uiCenter(94, 1, g_kos.txt, l2);
    if (l3) uiCenter(110, 1, g_kos.txt, l3);
    if (l4) uiCenter(126, 1, g_kos.txt2, l4);
    uiCenter(214, 1, g_kos.txt2, g_kos.en ? "Touch the screen or BOOT to return to K-OS" : "Dotknij ekranu (albo BOOT) - powrot do K-OS");
    uiText(4, 230, 1, g_kos.txt2, "v" KOS_DOOM_WERSJA);
    uiWaitTouch(l1);
    KosExitToMenu();
}

// =======================================================================================
// Dane gry w ogonie wlasnego slotu
// =======================================================================================
static uint32_t alignUp(uint32_t v, uint32_t a) { return (v + a - 1) & ~(a - 1); }

static uint32_t hdrCrc(const kd_hdr_t* h)
{
    return esp_rom_crc32_le(0, (const uint8_t*)h, offsetof(kd_hdr_t, hdrCrc));
}

static bool flashCrc(const esp_partition_t* p, uint32_t off, uint32_t len, uint32_t* out)
{
    const void* ptr = nullptr;
    esp_partition_mmap_handle_t h;
    if (esp_partition_mmap(p, off, len, ESP_PARTITION_MMAP_DATA, &ptr, &h) != ESP_OK) return false;
    uint32_t crc = 0;
    const uint8_t* b = (const uint8_t*)ptr;
    // Kawalkami z oddaniem czasu: 1,9 MB przez pamiec podreczna flasha to kilkaset ms.
    for (uint32_t o = 0; o < len; o += 65536) {
        uint32_t n = len - o < 65536 ? len - o : 65536;
        crc = esp_rom_crc32_le(crc, b + o, n);
        vTaskDelay(1);
    }
    esp_partition_munmap(h);
    *out = crc;
    return true;
}

enum { SRC_OK = 0, SRC_NOT_KWAD, SRC_OLD };

// Plik na karcie sprawdzany PRZED skasowaniem czegokolwiek w slocie. Tylko plik z kos/wad2kos.py
// w tej wersji formatu ma silnikowy uklad map (GbaWadUtil) i pasek KOSSTBAR - surowy doom1.wad
// wywrocilby silnik, a plik ze starszego wad2kos.py nie ma paska stanu.
static int srcCheck(FILE* f, uint32_t size)
{
    uint8_t h[12];
    if (size < 16 || fseek(f, 0, SEEK_SET) || fread(h, 1, 12, f) != 12) return SRC_NOT_KWAD;
    if (memcmp(h, "IWAD", 4)) return SRC_NOT_KWAD;
    uint32_t n, dir;
    memcpy(&n, h + 4, 4); memcpy(&dir, h + 8, 4);
    if (!n || n > KD_MAX_LUMPS || dir > size || (uint64_t)dir + (uint64_t)n * 16 > size) return SRC_NOT_KWAD;
    if (fseek(f, dir, SEEK_SET)) return SRC_NOT_KWAD;
    // Katalog porcjami po 32 wpisy: karta chodzi na 4 MHz, a fread po 16 B przy ~2000 wpisow
    // to kilkaset malych odczytow przez bufor FILE.
    uint8_t e[32 * 16];
    uint32_t infoPos = 0, infoLen = 0;
    bool info = false;
    for (uint32_t i = 0; i < n; ) {
        const uint32_t k = n - i < 32 ? n - i : 32;
        if (fread(e, 16, k, f) != k) return SRC_NOT_KWAD;
        for (uint32_t j = 0; j < k; j++) {
            // Ostatni wpis o tej nazwie wygrywa - tak samo szuka silnik (w_wad.c, od konca).
            if (!memcmp(e + 16 * j + 8, "KOSINFO\0", 8)) {
                memcpy(&infoPos, e + 16 * j, 4); memcpy(&infoLen, e + 16 * j + 4, 4); info = true;
            }
        }
        i += k;
    }
    if (!info) return SRC_NOT_KWAD;
    // Od KOSDOOM2 za katalogiem sa jeszcze dokladnie 4 B CRC - starszy plik ich nie ma.
    char magic[8];
    if ((uint64_t)dir + (uint64_t)n * 16 + 4 != size || infoLen < 8 || infoPos > size - 8
        || fseek(f, infoPos, SEEK_SET) || fread(magic, 1, 8, f) != 8 || memcmp(magic, KOS_KWAD_MAGIC, 8))
        return SRC_OLD;
    return SRC_OK;
}

// Kopia pliku z karty do ogona slotu. Zwraca NULL albo opis bledu.
static const char* copyToSlot(const esp_partition_t* run, uint32_t base, uint32_t cap,
                              const char* path, uint32_t srcSize, uint32_t srcMtime)
{
    KosStage("DANE: sprawdzanie pliku na karcie");
    FILE* f = fopen(path, "rb");
    if (!f) { Serial.printf("DANE: nie moge otworzyc %s\n", path); return "nie moge otworzyc pliku"; }
    const int sc = srcCheck(f, srcSize);
    Serial.printf("DANE: plik %s, %u B: %s\n", path, (unsigned)srcSize,
                  sc == SRC_OK ? "format KOSDOOM2 OK" : sc == SRC_OLD ? "STARY format" : "to nie plik z wad2kos.py");
    if (sc != SRC_OK) { fclose(f); return sc == SRC_OLD ? "STARY" : "KOSINFO"; }
    if (srcSize > cap) { fclose(f); Serial.printf("DANE: za duzy (%u > %u B)\n", (unsigned)srcSize, (unsigned)cap); return "ZA_DUZY"; }
    uint8_t* buf = (uint8_t*)malloc(4096);
    if (!buf) { fclose(f); return "brak pamieci na bufor 4 kB"; }

    uiBegin();
    uiCenter(18, 3, g_kos.acc, "DOOM");
    uiCenter(70, 2, g_kos.txt, g_kos.en ? "Installing game data" : "Instaluje dane gry");
    uiCenter(100, 1, g_kos.txt2, g_kos.en ? "only once - next start is instant" : "tylko raz - kolejny start bez czekania");
    char info[64];
    snprintf(info, sizeof(info), "%u kB / %u kB", (unsigned)(srcSize / 1024), (unsigned)(cap / 1024));
    uiCenter(116, 1, g_kos.txt2, info);

    // Pisanie do BIEGNACEJ partycji IDF blokuje (esp_partition_main_flash_region_safe) - ten
    // sam przelacznik zdejmuje samo IDF przy OTA bootloadera. Granice pilnujemy sami: tylko
    // [base, base+4096+len) i tylko w naszym slocie; esp_partition_* i tak nie wyjdzie poza partycje.
    const uint32_t eraseLen = KD_HDR_SECTOR + alignUp(srcSize, 4096);
    if (base < KD_HDR_SECTOR || base + eraseLen > run->size) { free(buf); fclose(f); return "zakres poza slotem"; }
    esp_flash_set_dangerous_write_protection(esp_flash_default_chip, false);
    const char* err = nullptr;

    uiCenter(140, 1, g_kos.txt, g_kos.en ? "erasing" : "kasowanie");
    uint32_t tPhase = millis();
    KosStage("DANE: kasowanie flasha");
    Serial.printf("DANE: kasuje %u B od +0x%06x\n", (unsigned)eraseLen, (unsigned)base);
    for (uint32_t o = 0; o < eraseLen && !err; o += 65536) {
        uint32_t n = eraseLen - o < 65536 ? eraseLen - o : 65536;
        esp_err_t ee = esp_partition_erase_range(run, base + o, n);
        if (ee != ESP_OK) { err = "kasowanie flasha nieudane"; Serial.printf("DANE: kasowanie +0x%06x blad 0x%x\n", (unsigned)(base + o), (unsigned)ee); }
        KosStageProgress(o + n, eraseLen);
        uiBar(160, o + n, eraseLen);
        vTaskDelay(1);   // kasowanie parkuje oba rdzenie - IDLE0 musi dostac czas, inaczej watchdog
    }
    Serial.printf("DANE: kasowanie %s w %u ms\n", err ? "PRZERWANE" : "OK", (unsigned)(millis() - tPhase));

    uint32_t done = 0, crc = 0;
    if (!err) {
        DisplayFillRect(0, 140, 320, 10, g_kos.bg);
        uiCenter(140, 1, g_kos.txt, g_kos.en ? "copying" : "kopiowanie");
        tPhase = millis();
        KosStage("DANE: kopiowanie z karty");
        fseek(f, 0, SEEK_SET);
        size_t r;
        // Nie wiecej niz srcSize: plik, ktory urosl w trakcie kopii, nie moze pisac za skasowany zakres.
        while (!err && done < srcSize && (r = fread(buf, 1, srcSize - done < 4096 ? srcSize - done : 4096, f)) > 0) {
            if (esp_partition_write(run, base + KD_HDR_SECTOR + done, buf, r) != ESP_OK) { err = "zapis flasha nieudany"; break; }
            crc = esp_rom_crc32_le(crc, buf, r);
            done += r;
            KosStageProgress(done, srcSize);
            if ((done & 0xFFFF) < 4096) { uiBar(160, done, srcSize); vTaskDelay(1); }
        }
        Serial.printf("DANE: kopiowanie %u B w %u ms\n", (unsigned)done, (unsigned)(millis() - tPhase));
        if (!err && (ferror(f) || done != srcSize)) err = "blad odczytu karty";
        // Naglowek slotu nie zostanie zapisany, wiec uszkodzony plik nie udaje dobrych danych.
        if (!err && crc != KD_CRC_RESIDUE) err = "USZKODZONY";
    }
    free(buf);
    fclose(f);

    if (!err) {
        DisplayFillRect(0, 140, 320, 10, g_kos.bg);
        uiCenter(140, 1, g_kos.txt, g_kos.en ? "verifying" : "sprawdzanie");
        KosStage("DANE: sprawdzanie flasha");
        uint32_t back = 0;
        if (!flashCrc(run, base + KD_HDR_SECTOR, srcSize, &back) || back != crc) err = "flash po zapisie inny niz plik";
    }
    if (!err) {
        kd_hdr_t h = {};
        memcpy(h.magic, KD_MAGIC, 8);
        h.ver = KD_VER; h.dataOff = base; h.wadLen = srcSize; h.wadCrc = crc;
        h.srcSize = srcSize; h.srcMtime = srcMtime;
        h.hdrCrc = hdrCrc(&h);
        // Naglowek OSTATNI: przerwany zapis (brak pradu, wyjeta karta) zostawia obszar bez
        // naglowka, czyli "danych nie ma" - a nie polowe WAD-u udajaca caly.
        if (esp_partition_write(run, base, &h, sizeof(h)) != ESP_OK) err = "zapis naglowka nieudany";
    }
    esp_flash_set_dangerous_write_protection(esp_flash_default_chip, true);
    Serial.printf("DANE: kopia %s (%u B, crc %08x)\n", err ? err : "OK", (unsigned)done, (unsigned)crc);
    return err;
}

// Dane w slocie tuz przed oddaniem ich silnikowi. CRC mowi tylko, ze slot = plik z karty; tu
// sprawdzamy sam plik: silnik (w_wad.c) wierzy katalogowi lumpow na slowo, a lump za koncem
// mapowania to wyjatek procesora, lump spod nierownego adresu - wyjatek przy pierwszym slowie
// (Xtensa), a brak KOSSTBAR - wywrotka dopiero po starcie gry. Kilka ms na ~2000 wpisow.
static const char* dataCheck(const esp_partition_t* run, uint32_t off, uint32_t len)
{
    const void* ptr = nullptr;
    esp_partition_mmap_handle_t mh;
    if (len < 16) return "ZLE";
    // Tu blad to brak stron MMU albo flash, nie zly plik - inny komunikat niz "przygotuj jeszcze raz".
    if (esp_partition_mmap(run, off, len, ESP_PARTITION_MMAP_DATA, &ptr, &mh) != ESP_OK) return "mapowanie flasha nieudane";
    const uint8_t* p = (const uint8_t*)ptr;
    const char* err = nullptr;
    uint32_t n, dir;
    memcpy(&n, p + 4, 4); memcpy(&dir, p + 8, 4);
    if (memcmp(p, "IWAD", 4) || !n || n > KD_MAX_LUMPS || (dir & 3) || dir < 12
        || (uint64_t)dir + (uint64_t)n * 16 + 4 != len)
        err = "ZLE";
    bool info = false, bar = false;
    for (uint32_t i = 0; !err && i < n; i++) {
        const uint8_t* e = p + dir + 16 * i;
        uint32_t fp, sz;
        memcpy(&fp, e, 4); memcpy(&sz, e + 4, 4);
        // Lumpy leza miedzy naglowkiem a katalogiem; pusty lump tez musi wskazywac do srodka,
        // bo silnik i tak zrobi z jego polozenia wskaznik.
        if (fp > dir || (sz && (fp < 12 || (fp & 3) || sz > dir - fp))) err = "ZLE";
        else if (!memcmp(e + 8, "KOSINFO\0", 8)) info = sz >= 8 && !memcmp(p + fp, KOS_KWAD_MAGIC, 8);
        else if (!memcmp(e + 8, "KOSSTBAR", 8)) bar = sz == KOS_STBAR_LUMPLEN && !memcmp(p + fp, KOS_STBAR_MAGIC, 8);
    }
    if (!err && (!info || !bar)) err = "STARY";
    esp_partition_munmap(mh);
    if (err) Serial.printf("DANE: sprawdzenie katalogu: %s\n", err);
    return err;
}

// Ekran "brak danych": co zrobic krok po kroku (to samo co kos/INSTRUKCJA.md, w skrocie).
// Tylko ASCII - font 5x7 nie ma polskich liter. Nie wraca.
static void uiNoData(bool noCard, uint32_t cap)
{
    static const char* const pl[] = {
        "DOOM potrzebuje danych gry (plik WAD).",
        "1. Pobierz darmowy Freedoom: freedoom.github.io",
        "   (plik freedoom1.wad) albo wez wlasny DOOM:",
        "   doom1.wad (shareware) lub doom.wad (pelny).",
        "2. Na komputerze przerob go skryptem wad2kos.py:",
        "   python3 wad2kos.py freedoom1.wad doom.kwad",
        "   (wad2kos.py: link przy DOOM w sklepie K-OS)",
        "3. Skopiuj doom.kwad na karte SD do folderu doom:",
        "   /doom/doom.kwad",
        "4. Wloz karte i uruchom DOOM jeszcze raz.",
    };
    static const char* const en[] = {
        "DOOM needs game data (a WAD file).",
        "1. Get free Freedoom: freedoom.github.io",
        "   (file freedoom1.wad) or use your own DOOM:",
        "   doom1.wad (shareware) or doom.wad (full).",
        "2. On a computer convert it with wad2kos.py:",
        "   python3 wad2kos.py freedoom1.wad doom.kwad",
        "   (wad2kos.py: link at DOOM in the K-OS store)",
        "3. Copy doom.kwad to the SD card, folder doom:",
        "   /doom/doom.kwad",
        "4. Insert the card and start DOOM again.",
    };
    const char* const* t = g_kos.en ? en : pl;
    Serial.printf("BLAD: %s - brak %s (DOOM szuka dokladnie tej sciezki na karcie)\n",
                  noCard ? "brak karty SD" : "brak danych gry", KOS_DATA_PATH);
    uiBegin();
    uiCenter(8, 3, g_kos.acc, "DOOM");
    uiCenter(38, 2, g_kos.err, noCard ? (g_kos.en ? "No SD card" : "Brak karty SD")
                                      : (g_kos.en ? "No game data" : "Brak danych gry"));
    for (int i = 0; i < 10; i++)
        uiText(8, 64 + 12 * i, 1, (i == 5 || i == 8) ? g_kos.acc : g_kos.txt, t[i]);
    char l[48];
    snprintf(l, sizeof(l), g_kos.en ? "space for data: %u kB" : "miejsce na dane: %u kB", (unsigned)(cap / 1024));
    uiCenter(190, 1, g_kos.txt2, l);
    uiCenter(214, 1, g_kos.txt2, g_kos.en ? "Touch the screen or BOOT to return to K-OS" : "Dotknij ekranu (albo BOOT) - powrot do K-OS");
    uiText(4, 230, 1, g_kos.txt2, "v" KOS_DOOM_WERSJA);
    uiWaitTouch(noCard ? "ekran: brak karty SD" : "ekran: brak danych gry");
    KosExitToMenu();
}

// Koniec drogi dla danych, ktorych silnik nie przyjmie (zly plik, stara wersja, uszkodzony).
static void uiBadData(const char* err, uint32_t cap)
{
    char l4[64];
    snprintf(l4, sizeof(l4), g_kos.en ? "space for data: %u kB" : "miejsce na dane: %u kB", (unsigned)(cap / 1024));
    if (!strcmp(err, "STARY"))
        uiFatal(g_kos.en ? "Old data format" : "Stary format danych",
                g_kos.en ? "/doom/doom.kwad was made by an older" : "/doom/doom.kwad zrobil starszy",
                g_kos.en ? "wad2kos.py - make it again with the new one" : "wad2kos.py - przygotuj go nowym jeszcze raz", l4);
    if (!strcmp(err, "USZKODZONY"))
        uiFatal(g_kos.en ? "Data file damaged" : "Plik danych uszkodzony",
                g_kos.en ? "/doom/doom.kwad has a wrong checksum" : "/doom/doom.kwad ma zla sume kontrolna",
                g_kos.en ? "copy it to the card again" : "skopiuj go na karte jeszcze raz", l4);
    if (!strcmp(err, "KOSINFO"))
        uiFatal(g_kos.en ? "Wrong data file" : "Zly plik danych",
                g_kos.en ? "/doom/doom.kwad was not made by" : "/doom/doom.kwad nie pochodzi ze",
                g_kos.en ? "wad2kos.py (a raw WAD file?)" : "skryptu wad2kos.py (surowy WAD?)", l4);
    if (!strcmp(err, "ZLE"))
        uiFatal(g_kos.en ? "Bad game data" : "Zle dane gry",
                g_kos.en ? "lump directory is damaged" : "katalog lumpow jest uszkodzony",
                g_kos.en ? "make /doom/doom.kwad again" : "przygotuj /doom/doom.kwad jeszcze raz", l4);
    uiFatal(g_kos.en ? "Copy failed" : "Kopiowanie nieudane", err, nullptr, l4);
}

static void dataPrepare(void)
{
    const esp_partition_t* run = esp_ota_get_running_partition();
    if (!run) uiFatal(g_kos.en ? "No partition" : "Brak partycji", "esp_ota_get_running_partition", nullptr, nullptr);

    esp_partition_pos_t pos = { run->address, run->size };
    esp_image_metadata_t md = {};
    if (esp_image_get_metadata(&pos, &md) != ESP_OK || !md.image_len)
        uiFatal(g_kos.en ? "Bad image" : "Zly obraz", "esp_image_get_metadata", nullptr, nullptr);

    // 64 kB, bo tyle ma strona MMU - dane zaczynaja sie od czystej strony, a kolejna, wieksza
    // wersja silnika i tak zamaze naglowek i dane zostana skopiowane od nowa.
    const uint32_t base = alignUp(md.image_len, 65536);
    const uint32_t cap = run->size > base + KD_HDR_SECTOR ? run->size - base - KD_HDR_SECTOR : 0;
    Serial.printf("DANE: partycja %s @0x%06x, %u B; obraz %u B; dane od +0x%06x, miejsce %u B\n",
                  run->label, (unsigned)run->address, (unsigned)run->size, (unsigned)md.image_len,
                  (unsigned)base, (unsigned)cap);

    kd_hdr_t h = {};
    bool hdrOk = cap > 0 && esp_partition_read(run, base, &h, sizeof(h)) == ESP_OK
                 && !memcmp(h.magic, KD_MAGIC, 8) && h.ver == KD_VER && h.dataOff == base
                 && h.wadLen >= 12 && h.wadLen <= cap && h.hdrCrc == hdrCrc(&h);
    uint32_t crcNow = 0;
    bool dataOk = hdrOk && flashCrc(run, base + KD_HDR_SECTOR, h.wadLen, &crcNow) && crcNow == h.wadCrc;
    if (hdrOk && !dataOk) Serial.println("DANE: naglowek jest, ale CRC danych sie nie zgadza");

    struct stat st;
    bool src = g_kos.sdOk && stat(KOS_DATA_PATH, &st) == 0 && st.st_size > 0;
    uint32_t srcSize = src ? (uint32_t)st.st_size : 0, srcMtime = src ? (uint32_t)st.st_mtime : 0;

    // Plik na karcie jest zrodlem prawdy: inny rozmiar albo data = uzytkownik podmienil dane.
    // Bez pliku (karta wyjeta) gramy tym, co juz lezy w slocie.
    bool need = !dataOk || (src && (srcSize != h.srcSize || srcMtime != h.srcMtime));
    Serial.printf("DANE: w slocie %s; na karcie %s (%u B); %s\n",
                  dataOk ? "sa (CRC OK)" : hdrOk ? "USZKODZONE" : "brak",
                  src ? KOS_DATA_PATH : !g_kos.sdOk ? "brak karty" : "brak pliku " KOS_DATA_PATH, (unsigned)srcSize,
                  !need ? "gram na danych ze slotu" : src ? "kopiuje z karty" : "nie ma skad wziac danych");
    if (dataOk && !need) {
        const char* bad = dataCheck(run, base + KD_HDR_SECTOR, h.wadLen);
        if (bad) uiBadData(bad, cap);
        s_dataOff = base + KD_HDR_SECTOR; s_dataLen = h.wadLen;
        Serial.printf("DANE: w slocie, %u B - bez kopiowania\n", (unsigned)h.wadLen);
        return;
    }

    if (!src) uiNoData(!g_kos.sdOk, cap);
    const char* err = copyToSlot(run, base, cap, KOS_DATA_PATH, srcSize, srcMtime);
    // Nowy plik odrzucony PRZED kasowaniem (zly format, stara wersja, za duzy), a w slocie leza
    // dobre dane: grajmy nimi zamiast wyrzucac gracza do menu.
    const bool before = err && (!strcmp(err, "KOSINFO") || !strcmp(err, "STARY") || !strcmp(err, "ZA_DUZY"));
    if (before && dataOk && !dataCheck(run, base + KD_HDR_SECTOR, h.wadLen)) {
        uiBegin();
        uiCenter(18, 3, g_kos.acc, "DOOM");
        uiCenter(70, 2, g_kos.err, g_kos.en ? "New data rejected" : "Nowe dane odrzucone");
        uiCenter(100, 1, g_kos.txt, !strcmp(err, "ZA_DUZY") ? (g_kos.en ? "/doom/doom.kwad is too big" : "/doom/doom.kwad jest za duzy")
                                   : !strcmp(err, "STARY") ? (g_kos.en ? "/doom/doom.kwad is from an older wad2kos.py" : "/doom/doom.kwad ze starszego wad2kos.py")
                                                           : (g_kos.en ? "/doom/doom.kwad is not from wad2kos.py" : "/doom/doom.kwad nie jest z wad2kos.py"));
        uiCenter(116, 1, g_kos.txt2, g_kos.en ? "playing with the installed data" : "gram na zainstalowanych danych");
        Serial.printf("DANE: nowy plik odrzucony (%s) - gram na danych ze slotu\n", err);
        delay(3000);
        s_dataOff = base + KD_HDR_SECTOR; s_dataLen = h.wadLen;
        return;
    }
    if (err && !strcmp(err, "ZA_DUZY")) {
        char l3[64], l4[64];
        snprintf(l3, sizeof(l3), g_kos.en ? "file: %u kB" : "plik: %u kB", (unsigned)(srcSize / 1024));
        snprintf(l4, sizeof(l4), g_kos.en ? "space for data: %u kB" : "miejsce na dane: %u kB", (unsigned)(cap / 1024));
        uiFatal(g_kos.en ? "Data too big" : "Dane za duze", l3, l4,
                g_kos.en ? "wad2kos.py --maps with fewer maps" : "wad2kos.py --maps z mniejsza liczba map");
    }
    if (err) uiBadData(err, cap);
    const char* bad = dataCheck(run, base + KD_HDR_SECTOR, srcSize);
    if (bad) uiBadData(bad, cap);
    s_dataOff = base + KD_HDR_SECTOR; s_dataLen = srcSize;
    Serial.printf("DANE: zainstalowane, %u B\n", (unsigned)srcSize);
}

void KosDataWhere(uint32_t* offset, uint32_t* length)
{
    *offset = s_dataOff; *length = s_dataLen;
}

// =======================================================================================
// Zapisy gry
// =======================================================================================
// Sprzatanie po przerwanym zapisie. safewrite.h odmawia pisania, gdy ".part" albo ".bak"
// istnieje, bo w Office pod ta nazwa moze lezec plik UZYTKOWNIKA. /doom/zapisy.sav.* pisze
// wylacznie ten program, wiec to na pewno nasze slady - i bez sprzatania kazdy nastepny zapis
// konczylby sie SW_ERR_BUSY do konca swiata.
static void saveRecover(void)
{
    const char* part = KOS_SAVE_PATH ".part";
    const char* bak = KOS_SAVE_PATH ".bak";
    struct stat st;
    bool haveMain = stat(KOS_SAVE_PATH, &st) == 0;
    if (stat(part, &st) == 0) { remove(part); Serial.println("ZAPISY: usuniety .part po przerwanym zapisie"); }
    if (stat(bak, &st) == 0) {
        if (haveMain) remove(bak);                      // podmiana doszla do konca, zostal tylko slad
        else if (rename(bak, KOS_SAVE_PATH) == 0)       // podmiana urwana miedzy krokami 3 i 4
            Serial.println("ZAPISY: przywrocone z .bak");
    }
}

int KosSaveLoad(unsigned char* buf, unsigned int size)
{
    bool was = s_mounted;
    // Przy starcie KosPrepare juz probowal (3 proby po 300 ms) - drugi raz tylko wydluzylby start.
    if (!was && (s_boot || !sdMount())) return -1;
    saveRecover();
    int res = 0;
    struct stat st;
    bool exists = stat(KOS_SAVE_PATH, &st) == 0;
    FILE* f = exists && (unsigned)st.st_size == size ? fopen(KOS_SAVE_PATH, "rb") : nullptr;
    if (f) {
        // Dlugosc sie zgadza, wiec krotki odczyt to blad KARTY, nie zly plik: -1 i nic nie ruszamy.
        res = fread(buf, 1, size, f) == size ? 1 : -1;
        fclose(f);
    } else if (exists && (unsigned)st.st_size != size) {
        // Plik zlej dlugosci nie jest naszym zapisem (dlugosc z katalogu FAT, nie z odczytu -
        // chwilowy blad karty tu nie trafi). Odkladamy go na bok zamiast kasowac i zaczynamy
        // od pustych slotow - inaczej kazdy nastepny zapis odbijalby sie od niego na zawsze.
        remove(KOS_SAVE_PATH ".zly");
        rename(KOS_SAVE_PATH, KOS_SAVE_PATH ".zly");
        res = 0;
        Serial.println("ZAPISY: plik zlej dlugosci odlozony jako zapisy.sav.zly");
    } else if (exists) {
        res = -1;   // jest, ma dobra dlugosc, a fopen zawiodl - karta, nie plik
    }
    if (!was) sdUnmount();
    Serial.printf("ZAPISY: %s\n", res == 1 ? "wczytane z karty" : res == 0 ? "brak pliku - pusto" : "blad odczytu karty");
    return res;
}

int KosSaveStore(const unsigned char* buf, unsigned int size)
{
    uint32_t t0 = millis();
    bool was = s_mounted;
    if (!was && !sdMount()) {
        g_kosSaveFailed = 1;
        Serial.printf("ZAPISY: brak karty albo pamieci (sterta %u B, najwiekszy blok %u B)\n",
                      (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
                      (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
        return 0;
    }
    mkdir("/sd/doom", 0777);   // EEXIST jest w porzadku
    saveRecover();
    SwRes r = safeWriteBytes(KOS_SAVE_PATH, buf, size);
    if (!was) sdUnmount();
    g_kosSaveFailed = (r != SW_OK);
    Serial.printf("ZAPISY: %s (%u ms)\n", r == SW_OK ? "zapisane" : swErrText(r), (unsigned)(millis() - t0));
    return r == SW_OK;
}

// =======================================================================================
// Start
// =======================================================================================
void KosPrepare(void)
{
    memset(&g_kos, 0, sizeof(g_kos));
    s_boot = true;
    KosStage("karta SD");
    g_kos.sdOk = sdMount();
    Serial.printf("SD: %s\n", g_kos.sdOk ? "zamontowana" : "BRAK KARTY");
    settingsLoad();
    touchCalLoad();
    ekranLoad();
    // Zapisy PRZED ekranami K-OS: w cieniu zapisow siedzi tez wynik kreatora (orientacja),
    // a ekran "instaluje dane" ma stac w tej samej orientacji co potem gra.
    SramInit();
    KosStage("DANE: przygotowanie");
    dataPrepare();
}

// Odmontowanie po SramInit; wolane z main.cpp przed Z_Init, zeby FAT nie zjadal strefy.
extern "C" void KosPrepareDone(void)
{
    s_boot = false;
    sdUnmount();
    uiEnd();
    KosStage("silnik: Z_Init / R_Init / plansza");
    Serial.printf("sterta po K-OS: wolne %u B, najwiekszy blok %u B\n",
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
                  (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
}

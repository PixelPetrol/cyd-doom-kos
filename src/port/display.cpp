// Zmienione dla K-OS 2026-09-24/30 (galaz kos, na bazie HenrysCat/cyd-doom 1c58bf4, GPL-2.0): profil panelu K-OS (MADCTL/INVON, pelny init jak TFT_eSPI, MADCTL sprawdzany po przywroceniu, nadpisanie z /doom/ekran.txt), jasnosc PWM, napisy 5x7, DisplayDeinit, pin podswietlenia per plytka, oczekiwanie na DMA z limitem.
// ILI9341/ST7789 SPI display driver for the CYD (ESP32-2432S028R).
//
// The engine renders 240x160 in 8bpp (as 120x160 uint16 with each byte a
// pixel). We palette-convert to RGB565 and nearest-neighbour scale to
// 320x213, centred vertically, pushing line chunks over SPI with DMA.
//
// Hardware notes learned the hard way on this board:
//  - Exactly ONE spi device may own the CS pin: two devices sharing
//    spics_io_num route CS to whichever was added last, silently
//    deselecting the panel for the other device's transactions.
//  - The panel ignores MADCTL/COLMOD while asleep: sleep-out first.
//  - All verification/readback is done bit-banged before the SPI bus is
//    claimed, so it cannot lie about driver-level problems.
#include <Arduino.h>
#include <string.h>
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "doomport.h"
#include "kos.h"
#include "kos_font.h"

#define PIN_TFT_MISO 12
#define PIN_TFT_MOSI 13
#define PIN_TFT_SCLK 14
#define PIN_TFT_CS   15
#define PIN_TFT_DC    2
// K-OS: podswietlenie z profilu plytki (build.sh): 2.8" = 21 jak w upstream, 2.4" = 27.
#ifdef CYD_BL_PIN
#define PIN_TFT_BL   CYD_BL_PIN
#else
#define PIN_TFT_BL   21
#endif

#define SRC_W 240
#define SRC_H 160
#define OUT_W 320
#define OUT_H 213                 // 160 * 4/3
#define OUT_Y0 ((240 - OUT_H) / 2)

#define ROWS_PER_CHUNK 8
#define CHUNK_BYTES (OUT_W * ROWS_PER_CHUNK * 2)

// K-OS: poziom = DOKLADNIE rotacja 1 z TFT_eSPI dla danego panelu (ILI9341: MV|BGR = 0x28,
// ST7789 z TFT_RGB_ORDER=TFT_BGR: MX|MV|BGR = 0x68) - tak rysuja poziomo SkyCYD i gry K-OS,
// sprawdzone na cyd24. Tylko wtedy kalibracja dotyku zrobiona w K-OS (rotacja 0) przelicza sie
// na poziom wzorem z input.cpp (gry/gry/engine.cpp, rawToScreen). Upstream mial tu 0x60
// i obowiazkowy kreator; pod K-OS kreator jest tylko awaryjny (BOOT).
// 0.1.1: panel dostaje TEN SAM pelny init co w K-OS (TFT_eSPI ILI9341_2_DRIVER / ST7789_DRIVER),
// a nie tylko reset programowy + MADCTL - wtedy MADCTL znaczy dokladnie to samo co w K-OS.
// Kolejnosc waznosci MADCTL: /doom/ekran.txt (madctl=0x..) > kreator (BOOT) > profil plytki.
// Odwrocenie kolorow: K-OS inicjuje ILI9341 z TFT_INVERSION_ON, a ST7789 z TFT_INVERSION_OFF
// (ST7789_Init.h w TFT_eSPI wysyla INVON bezwarunkowo - stad jawne OFF w K-OS). Ustawienie
// "odwrocenie" z K-OS odwraca ten stan, dokladnie jak applyTheme() w ladowarce.
#if defined(CYD_PANEL_ST7789)
#define KOS_MADCTL_LAND 0x68   // MX|MV|BGR (K-OS buduje ST7789 z TFT_RGB_ORDER=TFT_BGR)
#define KOS_PANEL_INVON 0
#define KOS_PANEL_NAME "ST7789"
#else
#define KOS_MADCTL_LAND 0x28   // MV|BGR
#define KOS_PANEL_INVON 1
#define KOS_PANEL_NAME "ILI9341"
#endif
static unsigned char madctlLand(void)
{
    if (g_kos.madctl) return g_kos.madctl;
    if (g_dispCfg.magic == DISPCFG_MAGIC) return g_dispCfg.madctl;
    return KOS_MADCTL_LAND;
}
static const char* madctlSource(void)
{
    if (g_kos.madctl) return "plik /doom/ekran.txt";
    if (g_dispCfg.magic == DISPCFG_MAGIC) return "kreator (BOOT)";
    return "profil plytki = rotacja 1 K-OS";
}
#define MADCTL_LANDSCAPE madctlLand()
#define COLMOD_16BPP     0x55

static spi_device_handle_t s_spi;
static uint16_t s_lut[256];              // RGB565, already byte-swapped for SPI
static uint8_t s_colmap[OUT_W];
static uint8_t s_rowmap[OUT_H];

static uint8_t* s_chunk[2];              // DMA-capable line chunk buffers
static bool s_chunkInFlight[2];
static spi_transaction_t s_chunkTrans[2];

static inline void dc(int level)
{
    gpio_set_level((gpio_num_t)PIN_TFT_DC, level);
}

// ---------------------------------------------------------------- bit-bang --

static void bbPinSetup(void)
{
#if !defined(CYD_BOARD_28)
    // K-OS 2.4": dotyk XPT2046 wisi na TEJ SAMEJ magistrali (CS 33). Z plywajacym CS odpowiadalby
    // na bit-bang i odczyt MADCTL walczylby z nim o MISO.
    pinMode(33, OUTPUT);
    digitalWrite(33, HIGH);
#endif
    pinMode(PIN_TFT_CS, OUTPUT);
    pinMode(PIN_TFT_SCLK, OUTPUT);
    pinMode(PIN_TFT_MOSI, OUTPUT);
    pinMode(PIN_TFT_MISO, INPUT);
    pinMode(PIN_TFT_DC, OUTPUT);
    digitalWrite(PIN_TFT_CS, HIGH);
    digitalWrite(PIN_TFT_SCLK, LOW);
    digitalWrite(PIN_TFT_DC, HIGH);
}

static void bbWriteByte(uint8_t b)
{
    for (int i = 7; i >= 0; i--) {
        digitalWrite(PIN_TFT_SCLK, LOW);
        digitalWrite(PIN_TFT_MOSI, (b >> i) & 1);
        digitalWrite(PIN_TFT_SCLK, HIGH);
    }
    digitalWrite(PIN_TFT_SCLK, LOW);
}

static void bbCmd(uint8_t c)
{
    digitalWrite(PIN_TFT_DC, LOW);
    bbWriteByte(c);
    digitalWrite(PIN_TFT_DC, HIGH);
}

static void bbCmdData(uint8_t c, const uint8_t* d, int n)
{
    digitalWrite(PIN_TFT_CS, LOW);
    bbCmd(c);
    for (int i = 0; i < n; i++)
        bbWriteByte(d[i]);
    digitalWrite(PIN_TFT_CS, HIGH);
}

static void bbCmd1(uint8_t c, uint8_t v)
{
    bbCmdData(c, &v, 1);
}

static uint32_t bbReadReg(uint8_t reg, int bits)
{
    digitalWrite(PIN_TFT_CS, LOW);
    bbCmd(reg);
    uint32_t v = 0;
    for (int i = 0; i < bits; i++) {
        digitalWrite(PIN_TFT_SCLK, LOW);
        digitalWrite(PIN_TFT_SCLK, HIGH);
        v = (v << 1) | (digitalRead(PIN_TFT_MISO) & 1);
    }
    digitalWrite(PIN_TFT_SCLK, LOW);
    digitalWrite(PIN_TFT_CS, HIGH);
    return v;
}

static bool bitsContainByte(uint32_t v, int bits, uint8_t want)
{
    for (int shift = 0; shift + 8 <= bits; shift++) {
        if (((v >> (bits - 8 - shift)) & 0xFF) == want)
            return true;
    }
    return false;
}

// Pelny init panelu bit-bangiem - wolno, ale bez zaleznosci od sterownika SPI. Sekwencje sa
// przepisane z TFT_eSPI (TFT_Drivers/ILI9341_Init.h galaz ILI9341_2_DRIVER i ST7789_Init.h),
// ktorym K-OS i jego programy (SkyCYD, gry) inicjuja ten sam panel. Upstream robil tylko reset
// programowy + MADCTL, a stan pozostalych rejestrow (np. B6h - kierunek skanowania) zalezal
// wtedy od panelu i od tego, co zostawil poprzedni program.
typedef struct { uint8_t cmd, n; uint8_t d[15]; } panel_cmd_t;
#if defined(CYD_PANEL_ST7789)
static const panel_cmd_t kPanelInit[] = {
    { 0x13, 0, {0} },                                   // NORON
    { 0xB6, 2, {0x0A, 0x82} },
    { 0xB0, 2, {0x00, 0xE0} },                          // RAMCTRL
    { 0xB2, 5, {0x0C, 0x0C, 0x00, 0x33, 0x33} },        // PORCTRL
    { 0xB7, 1, {0x35} },                                // GCTRL
    { 0xBB, 1, {0x28} },                                // VCOMS
    { 0xC0, 1, {0x0C} },                                // LCMCTRL
    { 0xC2, 2, {0x01, 0xFF} },                          // VDVVRHEN
    { 0xC3, 1, {0x10} },                                // VRHS
    { 0xC4, 1, {0x20} },                                // VDVSET
    { 0xC6, 1, {0x0F} },                                // FRCTR2
    { 0xD0, 2, {0xA4, 0xA1} },                          // PWCTRL1
    { 0xE0, 14, {0xD0, 0x00, 0x02, 0x07, 0x0A, 0x28, 0x32, 0x44, 0x42, 0x06, 0x0E, 0x12, 0x14, 0x17} },
    { 0xE1, 14, {0xD0, 0x00, 0x02, 0x07, 0x0A, 0x28, 0x31, 0x54, 0x47, 0x0E, 0x1C, 0x17, 0x1B, 0x1E} },
};
#else
static const panel_cmd_t kPanelInit[] = {
    { 0xCF, 3, {0x00, 0xC1, 0x30} },
    { 0xED, 4, {0x64, 0x03, 0x12, 0x81} },
    { 0xE8, 3, {0x85, 0x00, 0x78} },
    { 0xCB, 5, {0x39, 0x2C, 0x00, 0x34, 0x02} },
    { 0xF7, 1, {0x20} },
    { 0xEA, 2, {0x00, 0x00} },
    { 0xC0, 1, {0x10} },                                // PWCTR1
    { 0xC1, 1, {0x00} },                                // PWCTR2
    { 0xC5, 2, {0x30, 0x30} },                          // VMCTR1
    { 0xC7, 1, {0xB7} },                                // VMCTR2
    { 0xB1, 2, {0x00, 0x1A} },                          // FRMCTR1
    { 0xB6, 3, {0x08, 0x82, 0x27} },                    // DFUNCTR: GS=0, SS=0 jak w K-OS
    { 0xF2, 1, {0x00} },
    { 0x26, 1, {0x01} },
    { 0xE0, 15, {0x0F, 0x2A, 0x28, 0x08, 0x0E, 0x08, 0x54, 0xA9, 0x43, 0x0A, 0x0F, 0x00, 0x00, 0x00, 0x00} },
    { 0xE1, 15, {0x00, 0x15, 0x17, 0x07, 0x11, 0x06, 0x2B, 0x56, 0x3C, 0x05, 0x10, 0x0F, 0x3F, 0x3F, 0x0F} },
};
#endif

static void bbPanelInit(void)
{
    digitalWrite(PIN_TFT_CS, LOW);
    bbCmd(0x01);                  // soft reset
    digitalWrite(PIN_TFT_CS, HIGH);
    delay(150);
    digitalWrite(PIN_TFT_CS, LOW);
    bbCmd(0x11);                  // sleep out (config is ignored while asleep)
    digitalWrite(PIN_TFT_CS, HIGH);
    delay(120);

    for (unsigned i = 0; i < sizeof(kPanelInit) / sizeof(kPanelInit[0]); i++)
        bbCmdData(kPanelInit[i].cmd, kPanelInit[i].d, kPanelInit[i].n);

    bbCmd1(0x36, MADCTL_LANDSCAPE);
    bbCmd1(0x3A, COLMOD_16BPP);

    digitalWrite(PIN_TFT_CS, LOW);
    bbCmd((KOS_PANEL_INVON ^ (g_kos.invert ? 1 : 0)) ? 0x21 : 0x20);   // INVON / INVOFF
    digitalWrite(PIN_TFT_CS, HIGH);

    digitalWrite(PIN_TFT_CS, LOW);
    bbCmd(0x29);                  // display on
    digitalWrite(PIN_TFT_CS, HIGH);
}

// ---------------------------------------------------------------- hw spi ----

static void cmd(uint8_t c)
{
    spi_transaction_t t = {};
    t.length = 8;
    t.tx_data[0] = c;
    t.flags = SPI_TRANS_USE_TXDATA;
    dc(0);
    spi_device_transmit(s_spi, &t);
    dc(1);
}

static void data(const uint8_t* d, int len)
{
    // Bounce through DRAM: parameter tables may live in flash, which DMA
    // cannot read.
    uint8_t tmp[32];
    while (len > 0) {
        int n = len > 32 ? 32 : len;
        memcpy(tmp, d, n);
        spi_transaction_t t = {};
        t.length = n * 8;
        t.tx_buffer = tmp;
        spi_device_transmit(s_spi, &t);
        d += n;
        len -= n;
    }
}

static void cmdData(uint8_t c, const uint8_t* d, int len)
{
    cmd(c);
    if (len) data(d, len);
}

static void cmdData1(uint8_t c, uint8_t v)
{
    cmdData(c, &v, 1);
}

static void setWindow(int x0, int y0, int x1, int y1)
{
    if (!s_spi) return;   // DisplayInit bez pamieci DMA - lepiej nic nie rysowac niz wywrocic sie na NULL
    uint8_t ca[4] = { (uint8_t)(x0 >> 8), (uint8_t)x0, (uint8_t)(x1 >> 8), (uint8_t)x1 };
    uint8_t pa[4] = { (uint8_t)(y0 >> 8), (uint8_t)y0, (uint8_t)(y1 >> 8), (uint8_t)y1 };
    cmdData(0x2A, ca, 4);
    cmdData(0x2B, pa, 4);
    cmd(0x2C);
}

static void waitChunk(int i)
{
    if (s_chunkInFlight[i]) {
        spi_transaction_t* r;
        // Z limitem zamiast portMAX_DELAY: paczka, ktora nie wraca, to byla cicha, wieczna petla
        // (tak wygladalo 0.1.0 na cyd24: ekran, a potem cisza). Teraz slad w logu i dalej.
        static uint32_t lost = 0;
        if (spi_device_get_trans_result(s_spi, &r, pdMS_TO_TICKS(1000)) != ESP_OK) {
            if (lost++ < 5) printf("[lcd] paczka DMA %d nie wrocila w 1 s (%u. raz)\n", i, (unsigned)lost);
        }
        s_chunkInFlight[i] = false;
    }
}

static void sendChunk(int i, int bytes)
{
    spi_transaction_t* t = &s_chunkTrans[i];
    memset(t, 0, sizeof(*t));
    t->length = bytes * 8;
    t->tx_buffer = s_chunk[i];
    spi_device_queue_trans(s_spi, t, portMAX_DELAY);
    s_chunkInFlight[i] = true;
}

static void pushFrame(const uint8_t* frame)
{
    if (!s_spi || !s_chunk[0]) return;
    setWindow(0, OUT_Y0, OUT_W - 1, OUT_Y0 + OUT_H - 1);

    int buf = 0;
    int row = 0;
    while (row < OUT_H) {
        int rows = OUT_H - row > ROWS_PER_CHUNK ? ROWS_PER_CHUNK : OUT_H - row;

        waitChunk(buf);
        uint16_t* out = (uint16_t*)s_chunk[buf];
        for (int r = 0; r < rows; r++) {
            const uint8_t* src = frame + s_rowmap[row + r] * SRC_W;
            for (int x = 0; x < OUT_W; x += 4) {
                // colmap advances 3 source pixels per 4 output pixels
                const uint8_t* s3 = src + s_colmap[x];
                *out++ = s_lut[s3[0]];
                *out++ = s_lut[s3[1]];
                *out++ = s_lut[s3[2]];
                *out++ = s_lut[s3[2]];
            }
        }
        sendChunk(buf, rows * OUT_W * 2);
        buf ^= 1;
        row += rows;
    }
    waitChunk(0);
    waitChunk(1);
}

void DisplayPushFrame(const unsigned char* frame)
{
    pushFrame(frame);
}

void DisplaySetPalette(const unsigned char* rgb768)
{
    for (int i = 0; i < 256; i++) {
        unsigned r = *rgb768++;
        unsigned g = *rgb768++;
        unsigned b = *rgb768++;
        uint16_t c = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
        s_lut[i] = (uint16_t)((c >> 8) | (c << 8)); // SPI sends MSB first
    }
}

static void fillScreenBlack(void)
{
    setWindow(0, 0, 319, 239);
    memset(s_chunk[0], 0, CHUNK_BYTES);
    for (int row = 0; row < 240; row += ROWS_PER_CHUNK) {
        waitChunk(0);
        sendChunk(0, CHUNK_BYTES);
    }
    waitChunk(0);
}

static void backlight(int on);

// K-OS: ekran bledu I_Error. Upstream pisal tylko na Serial, a gracz widzial czarny ekran
// i zawieszona plytke - pod K-OS nikt nie ma podlaczonego monitora szeregowego.
void DisplayDrawText(const char* msg)
{
    if (!s_spi || !s_chunk[0]) DisplayInit();
    if (!s_spi || !s_chunk[0]) return;
    DisplayFillRect(0, 0, 320, 240, g_kos.bg);
    DisplayDrawString(8, 10, "DOOM", 3, g_kos.err, g_kos.bg);
    DisplayDrawString(8, 42, g_kos.en ? "engine error:" : "blad silnika:", 2, g_kos.err, g_kos.bg);
    // Zawijanie co 51 znakow fontem 1 (6 px) - komunikaty I_Error bywaja dlugie.
    char line[52];
    int y = 70;
    const char* p = msg ? msg : "";
    while (*p && y < 200) {
        int n = 0;
        while (p[n] && p[n] != '\n' && n < 51) n++;
        memcpy(line, p, n); line[n] = 0;
        DisplayDrawString(8, y, line, 1, g_kos.txt, g_kos.bg);
        y += 11;
        p += n;
        if (*p == '\n') p++;
    }
    DisplayDrawString(8, 222, g_kos.en ? "Touch the screen to return to K-OS" : "Dotknij ekranu - powrot do K-OS",
                      1, g_kos.txt2, g_kos.bg);
    backlight(1);
}

void DisplaySetMadctl(unsigned char v)
{
    cmdData1(0x36, v);
}

void DisplayFillRect(int x, int y, int w, int h, unsigned short rgb565)
{
    if (w <= 0 || h <= 0 || !s_spi || !s_chunk[0]) return;
    setWindow(x, y, x + w - 1, y + h - 1);

    uint16_t swapped = (uint16_t)((rgb565 >> 8) | (rgb565 << 8));
    int total = w * h;
    int chunkPix = CHUNK_BYTES / 2;

    waitChunk(0);
    waitChunk(1);
    uint16_t* p = (uint16_t*)s_chunk[0];
    int fillN = total < chunkPix ? total : chunkPix;
    for (int i = 0; i < fillN; i++) p[i] = swapped;

    while (total > 0) {
        int n = total < chunkPix ? total : chunkPix;
        waitChunk(0);
        sendChunk(0, n * 2);
        total -= n;
    }
    waitChunk(0);
}

// K-OS: napis 5x7 powiekszony `scale` razy, wypychany pasami przez bufor DMA. Jedno okno na
// caly napis zamiast prostokata na piksel - ekran z instrukcja rysuje sie w milisekundy.
void DisplayDrawString(int x, int y, const char* s, int scale, unsigned short fg, unsigned short bg)
{
    if (!s || !*s || scale < 1 || !s_spi || !s_chunk[0]) return;
    int n = (int)strlen(s);
    int w = n * 6 * scale;
    if (x + w > OUT_W) { n = (OUT_W - x) / (6 * scale); w = n * 6 * scale; }
    if (n <= 0) return;
    int h = 8 * scale;
    uint16_t fgs = (uint16_t)((fg >> 8) | (fg << 8));
    uint16_t bgs = (uint16_t)((bg >> 8) | (bg << 8));
    waitChunk(0);
    waitChunk(1);
    setWindow(x, y, x + w - 1, y + h - 1);
    int rowsPerChunk = CHUNK_BYTES / (w * 2);
    int buf = 0;
    for (int row = 0; row < h; ) {
        int rows = h - row < rowsPerChunk ? h - row : rowsPerChunk;
        waitChunk(buf);
        uint16_t* out = (uint16_t*)s_chunk[buf];
        for (int r = 0; r < rows; r++) {
            int gy = (row + r) / scale;             // wiersz glifu 0..7 (7 = odstep miedzy liniami)
            for (int i = 0; i < n; i++) {
                unsigned char c = (unsigned char)s[i];
                if (c < KOS_FONT_FIRST || c > KOS_FONT_LAST) c = '?';
                const uint8_t* g = &kosFont5x7[(c - KOS_FONT_FIRST) * 5];
                for (int gx = 0; gx < 6; gx++) {
                    bool on = gx < 5 && gy < 7 && ((g[gx] >> gy) & 1);
                    for (int k = 0; k < scale; k++) *out++ = on ? fgs : bgs;
                }
            }
        }
        sendChunk(buf, rows * w * 2);
        buf ^= 1;
        row += rows;
    }
    waitChunk(0);
    waitChunk(1);
}

// K-OS: jasnosc z ustawien K-OS przez LEDC zamiast "zawsze pelna". Jedno przypiecie na caly
// program - ponowny ledcAttach na tym samym pinie w rdzeniu 3.x konczy sie bledem w logu.
static void backlight(int on)
{
    static bool attached = false;
    if (!attached) attached = ledcAttach(PIN_TFT_BL, 5000, 8);
    if (!attached) { pinMode(PIN_TFT_BL, OUTPUT); digitalWrite(PIN_TFT_BL, on ? HIGH : LOW); return; }
    int pct = g_kos.bright ? g_kos.bright : 90;
    ledcWrite(PIN_TFT_BL, on ? (uint32_t)pct * 255 / 100 : 0);
}

static void busInit(int hz)
{
    spi_bus_config_t bus = {};
    bus.mosi_io_num = PIN_TFT_MOSI;
    bus.miso_io_num = PIN_TFT_MISO;
    bus.sclk_io_num = PIN_TFT_SCLK;
    bus.quadwp_io_num = -1;
    bus.quadhd_io_num = -1;
    bus.max_transfer_sz = CHUNK_BYTES + 64;
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO));

    spi_device_interface_config_t dev = {};
    dev.clock_speed_hz = hz;
    dev.mode = 0;
    dev.spics_io_num = PIN_TFT_CS;   // single device: sole owner of CS
    dev.queue_size = 4;
    ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &dev, &s_spi));
}

static void busDeinit(void)
{
    if (s_spi) {
        spi_bus_remove_device(s_spi);
        s_spi = nullptr;
    }
    spi_bus_free(SPI2_HOST);
}

void DisplayInit(void)
{
    // Scale maps: out pixel -> source pixel (3/4 ratio)
    for (int x = 0; x < OUT_W; x++) s_colmap[x] = (uint8_t)((x * 3) >> 2);
    for (int y = 0; y < OUT_H; y++) s_rowmap[y] = (uint8_t)((y * 3) >> 2);

    // Grayscale identity palette until the game uploads PLAYPAL
    uint8_t gray[768];
    for (int i = 0; i < 256; i++) { gray[i*3] = gray[i*3+1] = gray[i*3+2] = (uint8_t)i; }
    DisplaySetPalette(gray);

    backlight(0);

    s_chunk[0] = (uint8_t*)heap_caps_malloc(CHUNK_BYTES, MALLOC_CAP_DMA);
    s_chunk[1] = (uint8_t*)heap_caps_malloc(CHUNK_BYTES, MALLOC_CAP_DMA);
    if (!s_chunk[0] || !s_chunk[1]) {
        printf("[lcd] brak pamieci DMA na bufory ekranu (%u B)\n", (unsigned)(2 * CHUNK_BYTES));
        return;
    }

    // Reliable baseline config, bit-banged, with readback verification.
    bbPinSetup();
    bbPanelInit();
    uint32_t rb = bbReadReg(0x0B, 16);
    printf("[lcd] panel %s (%s), MADCTL=0x%02x (%s), INV%s\n", KOS_PANEL_NAME, KOS_BOARD_ID,
           MADCTL_LANDSCAPE, madctlSource(), (KOS_PANEL_INVON ^ (g_kos.invert ? 1 : 0)) ? "ON" : "OFF");
    printf("[lcd] bit-bang init: MADCTL readback=0x%04x (%s)\n", (unsigned)rb,
           bitsContainByte(rb, 16, MADCTL_LANDSCAPE) ? "ok" : "UNEXPECTED");

    // Find the fastest clock whose hardware-SPI command writes verifiably
    // land: write a test MADCTL over hw SPI, release the bus, read it back
    // bit-banged.
    static const int trySpeeds[] = {40, 26, 20, 10};
    int chosen = -1;
    for (unsigned i = 0; i < sizeof(trySpeeds) / sizeof(trySpeeds[0]); i++) {
        uint8_t testVal = (MADCTL_LANDSCAPE == 0x68) ? 0x28 : 0x68;
        busInit(trySpeeds[i] * 1000 * 1000);
        gpio_set_direction((gpio_num_t)PIN_TFT_DC, GPIO_MODE_OUTPUT);
        dc(1);
        cmdData1(0x36, testVal);     // test value != configured value
        busDeinit();

        bbPinSetup();
        uint32_t echo = bbReadReg(0x0B, 16);
        bool ok = bitsContainByte(echo, 16, testVal);
        printf("[lcd] hw spi %d MHz: MADCTL echo=0x%04x %s\n",
               trySpeeds[i], (unsigned)echo, ok ? "OK" : "corrupted");
        bbCmd1(0x36, MADCTL_LANDSCAPE);  // restore
        if (ok) { chosen = trySpeeds[i]; break; }
    }
    if (chosen < 0) {
        chosen = 2;
        printf("[lcd] hw spi failed at all speeds - limping at 2 MHz\n");
    }
    // 0.1.1: przywrocenie MADCTL po tescie predkosci SPRAWDZONE odczytem. W 0.1.0 zostawal tu
    // tylko zapis bez odczytu - gdyby sie nie przyjal, panel zostalby z wartoscia testowa
    // (0x68 na ILI9341 = obraz odbity wzgledem K-OS).
    uint32_t after = 0;
    for (int k = 0; k < 3; k++) {
        bbCmd1(0x36, MADCTL_LANDSCAPE);
        after = bbReadReg(0x0B, 16);
        if (bitsContainByte(after, 16, MADCTL_LANDSCAPE)) break;
    }
    printf("[lcd] MADCTL po tescie=0x%04x (%s)\n", (unsigned)after,
           bitsContainByte(after, 16, MADCTL_LANDSCAPE) ? "ok" : "NIE PRZYJETY");
    printf("[lcd] using %d MHz\n", chosen);

    busInit(chosen * 1000 * 1000);
    gpio_set_direction((gpio_num_t)PIN_TFT_DC, GPIO_MODE_OUTPUT);
    dc(1);
    cmdData1(0x36, MADCTL_LANDSCAPE);   // i jeszcze raz sprzetowym SPI, na wszelki wypadek

    fillScreenBlack();
    backlight(1);
    printf("[lcd] ekran gotowy\n");
}

// Odbicia obrazu wzgledem rotacji 1 K-OS - dotyk (input.cpp) musi odbic sie tak samo.
// W trybie MV: MY odwraca kolejnosc wierszy panelu = os x obrazu (320), MX - kolumn = os y (240).
// Na obu panelach MX to kolumna fizyczna (ILI9341 = ST7789 z odwrotnym MX we wszystkich rotacjach).
void DisplayMirror(int* flipX, int* flipY)
{
    unsigned char d = (unsigned char)(madctlLand() ^ KOS_MADCTL_LAND);
    *flipX = (d & 0x80) ? 1 : 0;
    *flipY = (d & 0x40) ? 1 : 0;
}

void DisplayDeinit(void)
{
    if (s_chunk[0]) waitChunk(0);
    if (s_chunk[1]) waitChunk(1);
    busDeinit();
    heap_caps_free(s_chunk[0]); s_chunk[0] = nullptr;
    heap_caps_free(s_chunk[1]); s_chunk[1] = nullptr;
    // Podswietlenie zostaje: ekran silnika startuje zaraz po tym i nie ma czego ukrywac.
}

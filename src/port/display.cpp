// Zmienione dla K-OS 2026-09-24 (galaz kos, na bazie HenrysCat/cyd-doom 1c58bf4, GPL-2.0): profil panelu K-OS (MADCTL/INVON), jasnosc PWM, napisy 5x7, DisplayDeinit, pin podswietlenia per plytka.
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

// K-OS: poziom = DOKLADNIE rotacja 1 z TFT_eSPI dla danego panelu. Tylko wtedy kalibracja
// dotyku zrobiona w K-OS (rotacja 0) przelicza sie na poziom wzorem z input.cpp - ten sam
// wzor, ktorego uzywa tom gier (gry/gry/engine.cpp, rawToScreen). Upstream mial tu 0x60 i
// obowiazkowy kreator przy pierwszym starcie; pod K-OS kreator jest tylko awaryjny (BOOT).
// Odwrocenie kolorow: K-OS inicjuje ILI9341 z TFT_INVERSION_ON, a ST7789 z TFT_INVERSION_OFF
// (ST7789_Init.h w TFT_eSPI wysyla INVON bezwarunkowo - stad jawne OFF w K-OS). Ustawienie
// "odwrocenie" z K-OS odwraca ten stan, dokladnie jak applyTheme() w ladowarce.
#if defined(CYD_PANEL_ST7789)
#define KOS_MADCTL_LAND 0x68   // MX|MV|BGR (K-OS buduje ST7789 z TFT_RGB_ORDER=TFT_BGR)
#define KOS_PANEL_INVON 0
#else
#define KOS_MADCTL_LAND 0x28   // MV|BGR
#define KOS_PANEL_INVON 1
#endif
#define MADCTL_LANDSCAPE (g_dispCfg.magic == DISPCFG_MAGIC ? g_dispCfg.madctl : KOS_MADCTL_LAND)
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

// Full panel init, bit-banged: slow but unconditionally reliable.
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
        spi_device_get_trans_result(s_spi, &r, portMAX_DELAY);
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
    printf("[lcd] using %d MHz\n", chosen);

    busInit(chosen * 1000 * 1000);
    gpio_set_direction((gpio_num_t)PIN_TFT_DC, GPIO_MODE_OUTPUT);
    dc(1);

    fillScreenBlack();
    backlight(1);
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

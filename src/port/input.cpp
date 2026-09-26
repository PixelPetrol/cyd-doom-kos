// Zmienione dla K-OS 2026-09-24 (galaz kos, na bazie HenrysCat/cyd-doom 1c58bf4, GPL-2.0): odczyt XPT2046 jak w K-OS (2.8" bit-bang, 2.4" na magistrali ekranu), kalibracja z K-OS.
// Input for the CYD port: XPT2046 resistive touch (bit-banged), serial
// keyboard fallback, and the BOOT button.
//
// Touch zones (320x240 landscape):
//   +--------------------------------+
//   | MENU          |        | MAP   |   top strip (y < 48)
//   |---------------+--------+-------|
//   |               |                |
//   |   virtual     |   USE (A)      |   y < 144
//   |   d-pad       |----------------|
//   |   (x < 176)   |   FIRE (B)     |
//   +--------------------------------+
// D-pad: dominant axis from pad centre decides up/down/left/right.
// Serial keys: w/a/s/d move+turn, q/e strafe, f=fire, space=use,
//              m/enter=menu, t/tab=map. Keys auto-release after a timeout.
#include <Arduino.h>
#include "driver/spi_master.h"
#include "doomport.h"
#include "kos.h"

extern "C" {
#include "doomdef.h"
#include "doomtype.h"
#include "d_event.h"
void D_PostEvent(event_t* ev);
}

#define PIN_TOUCH_CLK  25
#define PIN_TOUCH_CS   33
#define PIN_TOUCH_DIN  32
#define PIN_TOUCH_DOUT 39
#define PIN_TOUCH_IRQ  36
#define PIN_BOOT_BTN    0

// K-OS: prog nacisku i wzor z kos_board.h (z = z1 + 4095 - z2, jak TFT_eSPI getTouchRawZ).
// Surowe liczby MUSZA powstawac tak samo jak w K-OS, inaczej kalibracja z dotyk.txt nie pasuje.
#define KOS_TOUCH_Z_MIN 350

// Raw ADC ranges for the 2.8" CYD panel (portrait-native), tuned generously.
#define RAW_MIN 300
#define RAW_MAX 3800

#define SERIAL_KEY_HOLD_MS 180

static void postKey(evtype_t type, int key)
{
    event_t ev;
    ev.type = type;
    ev.data1 = key;
    D_PostEvent(&ev);
}

// ---------------------------------------------------------------- touch ---

static int touchKeyHeld = -1;

#if defined(CYD_BOARD_28)
// 2.8": XPT2046 na wlasnych pinach, bit-bang w trybie SPI 0 - przepisane 1:1 z kos_board.h
// (kosXptXfer_), a nie z upstream: upstream uzywal innych komend (0xD1/0x91, mediana z 3),
// wiec jego surowe liczby nie odpowiadalyby kalibracji zrobionej w K-OS.
static uint16_t xptXfer(uint8_t cmd)
{
    for (int i = 7; i >= 0; i--) {
        digitalWrite(PIN_TOUCH_DIN, (cmd >> i) & 1);
        delayMicroseconds(1);
        digitalWrite(PIN_TOUCH_CLK, HIGH);
        delayMicroseconds(1);
        digitalWrite(PIN_TOUCH_CLK, LOW);
    }
    // DIN nisko przy odczycie - inaczej XPT2046 bierze to za nowa komende (lekcja z upstream).
    digitalWrite(PIN_TOUCH_DIN, LOW);
    uint16_t v = 0;
    for (int i = 0; i < 16; i++) {
        delayMicroseconds(1);
        digitalWrite(PIN_TOUCH_CLK, HIGH);
        delayMicroseconds(1);
        v = (v << 1) | (digitalRead(PIN_TOUCH_DOUT) ? 1 : 0);
        digitalWrite(PIN_TOUCH_CLK, LOW);
    }
    return (v >> 3) & 0x0FFF;
}
static void xptBegin(void) { digitalWrite(PIN_TOUCH_CS, LOW); }
static void xptEnd(void)   { digitalWrite(PIN_TOUCH_CS, HIGH); }
#else
// 2.4": XPT2046 na magistrali ekranu (HSPI, CS 33). Osobne urzadzenie spi_master z WLASNYM CS -
// zasada upstream "jeden CS = jedno urzadzenie" jest zachowana. Transakcje ida z tego samego
// zadania co wypychanie klatki, a pushFrame konczy sie oczekiwaniem na wszystkie paczki DMA,
// wiec dotyk nigdy nie wchodzi w polowe klatki.
static spi_device_handle_t s_touch = nullptr;
static uint16_t xptXfer(uint8_t cmd)
{
    if (!s_touch) return 0;
    // 32 bity, nie 24: dlugosc bedaca wielokrotnoscia 4 B nie wymaga od sterownika bufora
    // posredniego DMA przy kazdej transakcji (alokacja, ktora w grze moze sie nie udac).
    spi_transaction_t t = {};
    t.length = 32;
    t.flags = SPI_TRANS_USE_TXDATA | SPI_TRANS_USE_RXDATA;
    t.tx_data[0] = cmd;
    if (spi_device_polling_transmit(s_touch, &t) != ESP_OK) return 0xFFFF;
    return (uint16_t)((((uint16_t)t.rx_data[1] << 8) | t.rx_data[2]) >> 3) & 0x0FFF;
}
static void xptBegin(void) { if (s_touch) spi_device_acquire_bus(s_touch, portMAX_DELAY); }
static void xptEnd(void)   { if (s_touch) spi_device_release_bus(s_touch); }
#endif

// Surowy nacisk: 0 = nikt nie dotyka. Na 2.8" najpierw PENIRQ, jak w kos_board.h.
static int xptZ(void)
{
#if defined(CYD_BOARD_28)
    if (digitalRead(PIN_TOUCH_IRQ)) return 0;
#endif
#if !defined(CYD_BOARD_28)
    if (!s_touch) return 0;
#endif
    xptBegin();
    uint16_t z1 = xptXfer(0xB0);
    uint16_t z2 = xptXfer(0xC0);
    xptEnd();
    if (z1 == 0xFFFF || z2 == 0xFFFF) return 0;   // nieudana transakcja = brak dotyku, nie wieczny nacisk
    int z = z1 + 4095 - z2;
    // z2 = 0 przy odlaczonym albo milczacym ukladzie daje 4095 - TFT_eSPI (getTouchRawZ) tez to zeruje.
    if (z >= 4095) return 0;
    return z > 0 ? z : 0;
}

// Raw touch sample. Returns pressure (0 when untouched). Srednia z 4 po probce odrzuconej -
// dokladnie kosTouchRawXY, bo na tych liczbach K-OS robil kalibracje.
int InputRawTouch(int* prx, int* pry)
{
    int z = xptZ();
    if (z < KOS_TOUCH_Z_MIN / 2) { *prx = *pry = 0; return z; }
    xptBegin();
    uint32_t sx = 0, sy = 0;
    xptXfer(0xD0);
    for (int i = 0; i < 4; i++) sx += xptXfer(0xD0);
    xptXfer(0x90);
    for (int i = 0; i < 4; i++) sy += xptXfer(0x90);
    xptEnd();
    *prx = (int)(sx / 4);
    *pry = (int)(sy / 4);
    return z;
}

// Kalibracja K-OS (rotacja 0, 240x320) -> wspolrzedne poziome 320x240 silnika.
// KROK 1 to TFT_eSPI::convertRawXY, KROK 2 to przejscie z rotacji 0 na 1 - ten sam wzor co
// gry/gry/engine.cpp (rawToScreen). NIESPRAWDZONE PALCEM: gdyby dotyk wychodzil lustrzanie,
// zbudowac z -DDOOM_TOUCH_ROT1_ALT=1 (druga mozliwa orientacja).
static void kosRawToLand(int rx, int ry, int* ox, int* oy)
{
    const uint16_t* c = g_kos.cal;
    int32_t xx, yy;
    if (!(c[4] & 1)) { xx = ((int32_t)rx - c[0]) * 240 / c[1]; yy = ((int32_t)ry - c[2]) * 320 / c[3]; }
    else             { xx = ((int32_t)ry - c[0]) * 240 / c[1]; yy = ((int32_t)rx - c[2]) * 320 / c[3]; }
    if (c[4] & 2) xx = 240 - xx;
    if (c[4] & 4) yy = 320 - yy;
    if (xx < 0) xx = 0; if (xx > 239) xx = 239;
    if (yy < 0) yy = 0; if (yy > 319) yy = 319;
#if defined(DOOM_TOUCH_ROT1_ALT) && DOOM_TOUCH_ROT1_ALT
    *ox = 319 - yy; *oy = xx;
#else
    *ox = yy; *oy = 239 - xx;
#endif
}

int InputBootPressed(void)
{
    return digitalRead(PIN_BOOT_BTN) == LOW;
}

static bool touchRead(int* sx, int* sy)
{
    int rx, ry;
    int z1 = InputRawTouch(&rx, &ry);

    // Raw diagnostics twice a second while touched
    static unsigned long lastDbg = 0;
    bool touched = z1 >= KOS_TOUCH_Z_MIN;
    unsigned long now = millis();
    if (touched && now - lastDbg > 500) {
        printf("[touchdbg] z1=%d rx=%d ry=%d\n", z1, rx, ry);
        lastDbg = now;
    }

    if (!touched)
        return false;

    int x, y;
    if (g_dispCfg.magic == DISPCFG_MAGIC) {
        // Calibrated mapping from the setup wizard
        int rawX = g_dispCfg.touchSwapXY ? ry : rx;
        int rawY = g_dispCfg.touchSwapXY ? rx : ry;
        x = (int)((long)(rawX - g_dispCfg.xRaw0) * 319 /
                  (g_dispCfg.xRaw319 - g_dispCfg.xRaw0));
        y = (int)((long)(rawY - g_dispCfg.yRaw0) * 239 /
                  (g_dispCfg.yRaw239 - g_dispCfg.yRaw0));
    } else if (g_kos.cal[1] && g_kos.cal[3]) {
        // K-OS: kalibracja z karty (albo domyslna z kos_board.h, gdy pliku nie ma)
        kosRawToLand(rx, ry, &x, &y);
    } else {
        if (rx < RAW_MIN || rx > RAW_MAX || ry < RAW_MIN || ry > RAW_MAX)
            return false;
        x = (int)((long)(ry - RAW_MIN) * 320 / (RAW_MAX - RAW_MIN));
        y = 239 - (int)((long)(rx - RAW_MIN) * 240 / (RAW_MAX - RAW_MIN));
    }

    if (x < 0) x = 0; if (x > 319) x = 319;
    if (y < 0) y = 0; if (y > 239) y = 239;
    *sx = x;
    *sy = y;
    return true;
}

static int zoneToKey(int x, int y)
{
    if (y < 48) {
        if (x < 96) return KEYD_START;
        if (x >= 224) return KEYD_SELECT;
    }
    if (x < 176) {
        // virtual d-pad centred in left half of play area
        int dx = x - 88;
        int dy = y - 150;
        if (abs(dx) > abs(dy))
            return dx < 0 ? KEYD_LEFT : KEYD_RIGHT;
        return dy < 0 ? KEYD_UP : KEYD_DOWN;
    }
    return y < 144 ? KEYD_A : KEYD_B;
}

static void touchPoll(void)
{
    int x, y;
    int key = -1;

    if (touchRead(&x, &y))
        key = zoneToKey(x, y);

    if (key != touchKeyHeld) {
        if (touchKeyHeld != -1)
            postKey(ev_keyup, touchKeyHeld);
        if (key != -1) {
            postKey(ev_keydown, key);
            printf("[touch] x=%d y=%d -> key %d\n", x, y, key);
        }
        touchKeyHeld = key;
    }
}

// --------------------------------------------------------------- serial ---

#define MAX_SERIAL_KEYS 8
static struct { int key; unsigned long expiry; } serialKeys[MAX_SERIAL_KEYS];

static int serialCharToKey(int c)
{
    switch (c) {
        case 'w': case 'W': return KEYD_UP;
        case 's': case 'S': return KEYD_DOWN;
        case 'a': case 'A': return KEYD_LEFT;
        case 'd': case 'D': return KEYD_RIGHT;
        case 'q': case 'Q': return KEYD_L;
        case 'e': case 'E': return KEYD_R;
        case 'f': case 'F': return KEYD_B;      // fire
        case ' ':           return KEYD_A;      // use / confirm
        case 'm': case 'M': case '\r': case '\n': return KEYD_START;
        case 't': case 'T': case '\t': return KEYD_SELECT;
    }
    return -1;
}

static void serialPoll(void)
{
    unsigned long now = millis();

    while (Serial.available()) {
        int key = serialCharToKey(Serial.read());
        if (key < 0)
            continue;

        int slot = -1;
        for (int i = 0; i < MAX_SERIAL_KEYS; i++) {
            if (serialKeys[i].key == key) { slot = i; break; }
            if (slot < 0 && serialKeys[i].key == -1) slot = i;
        }
        if (slot < 0)
            continue;

        if (serialKeys[slot].key == -1) {
            serialKeys[slot].key = key;
            postKey(ev_keydown, key);
        }
        serialKeys[slot].expiry = now + SERIAL_KEY_HOLD_MS;
    }

    for (int i = 0; i < MAX_SERIAL_KEYS; i++) {
        if (serialKeys[i].key != -1 && (long)(now - serialKeys[i].expiry) >= 0) {
            postKey(ev_keyup, serialKeys[i].key);
            serialKeys[i].key = -1;
        }
    }
}

// ----------------------------------------------------------- boot button ---

static bool bootHeld = false;

static void bootPoll(void)
{
    bool down = digitalRead(PIN_BOOT_BTN) == LOW;
    if (down != bootHeld) {
        postKey(down ? ev_keydown : ev_keyup, KEYD_START);
        bootHeld = down;
    }
}

// ----------------------------------------------------------------- api ---

void InputInit(void)
{
#if defined(CYD_BOARD_28)
    pinMode(PIN_TOUCH_CLK, OUTPUT);
    pinMode(PIN_TOUCH_DIN, OUTPUT);
    pinMode(PIN_TOUCH_CS, OUTPUT);
    pinMode(PIN_TOUCH_DOUT, INPUT);
    pinMode(PIN_TOUCH_IRQ, INPUT);
    digitalWrite(PIN_TOUCH_CS, HIGH);
    digitalWrite(PIN_TOUCH_CLK, LOW);
#else
    if (!s_touch) {
        spi_device_interface_config_t dev = {};
        dev.clock_speed_hz = 2500000;          // SPI_TOUCH_FREQUENCY z flag K-OS
        dev.mode = 0;
        dev.spics_io_num = 33;                 // TOUCH_CS=33 z build.sh K-OS
        dev.queue_size = 1;
        if (spi_bus_add_device(SPI2_HOST, &dev, &s_touch) != ESP_OK) {
            s_touch = nullptr;
            printf("[touch] nie moge dodac XPT2046 do magistrali ekranu\n");
        }
    }
#endif
    pinMode(PIN_BOOT_BTN, INPUT_PULLUP);

    for (int i = 0; i < MAX_SERIAL_KEYS; i++)
        serialKeys[i].key = -1;
}

void InputDeinit(void)
{
#if !defined(CYD_BOARD_28)
    if (s_touch) { spi_bus_remove_device(s_touch); s_touch = nullptr; }
#endif
}

void InputPoll(void)
{
    touchPoll();
    serialPoll();
    bootPoll();
}

// DOOM dla K-OS - (c) 2026 Piotr Korona. Licencja: GPL-2.0 lub pozniejsza (jak silnik, patrz LICENSE).
// K-OS Office - wspolny wzorzec bezpiecznego zapisu (opis i uzasadnienie w safewrite.h).
#include "safewrite.h"
#include <stdarg.h>
#include <string.h>
#include <sys/stat.h>

// ---- operacje plikowe przez wskazniki: na plytce prawdziwe, w testach atrapy ----
static FILE*  realOpen(const char* p, const char* m) { return fopen(p, m); }
static size_t realWrite(const void* p, size_t sz, size_t n, FILE* f) { return fwrite(p, sz, n, f); }
static int    realClose(FILE* f) { return fclose(f); }
static int    realRename(const char* a, const char* b) { return rename(a, b); }
static int    realRemove(const char* p) { return remove(p); }

const SwOps SW_OPS_REAL = { realOpen, realWrite, realClose, realRename, realRemove };
static const SwOps* SW = &SW_OPS_REAL;

void swSetOps(const SwOps* ops) { SW = ops ? ops : &SW_OPS_REAL; }

// ---- pisanie ----
bool swWrite(SwOut* o, const void* data, size_t n) {
  if (!o || !o->ok) return false;
  if (!n) return true;
  if (SW->xwrite(data, 1, n, o->f) != n) { o->ok = false; o->res = SW_ERR_WRITE; return false; }
  return true;
}

bool swPuts(SwOut* o, const char* s) { return swWrite(o, s, s ? strlen(s) : 0); }

bool swPrintf(SwOut* o, const char* fmt, ...) {
  if (!o || !o->ok) return false;
  char line[SW_LINE];
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(line, sizeof(line), fmt, ap);
  va_end(ap);
  // Obciety wpis to BLAD, a nie "zapisz, co sie zmiescilo" - polowa wiersza w pliku
  // danych jest gorsza niz brak zapisu, bo wyglada na poprawna.
  if (n < 0 || (size_t)n >= sizeof(line)) { o->ok = false; o->res = SW_ERR_FMT; return false; }
  return swWrite(o, line, (size_t)n);
}

// ---- sciezki ----
bool swTmpPath(const char* path, char* out, size_t n) {
  if (!path || !out) return false;
  int k = snprintf(out, n, "%s.part", path);
  return k > 0 && (size_t)k < n;
}

bool swBakPath(const char* path, char* out, size_t n) {
  if (!path || !out) return false;
  int k = snprintf(out, n, "%s.bak", path);
  return k > 0 && (size_t)k < n;
}

// stat, a nie fopen: nie zjada uchwytu z puli max_files karty (jest ich piec),
// odpowiada tak samo dla katalogu i dla pliku zajetego gdzie indziej, i nie zalezy
// od podmienionych operacji - istnienie pliku jest faktem, nie operacja do udawania.
bool swExists(const char* p) {
  struct stat st;
  return p && stat(p, &st) == 0;
}

// ---- podmiana ----
SwRes safeReplace(const char* tmp, const char* dst) {
  char bak[SW_PATH];
  if (!swBakPath(dst, bak, sizeof(bak))) return SW_ERR_PATH;
  // Gdy celu NIE MA, nie dotykamy ".bak" wcale: po przerwanym zapisie to jedyna kopia
  // poprzedniej tresci i skasowanie jej tutaj byloby dokladnie tym bledem, ktory ten
  // plik ma usuwac.
  if (!swExists(dst))
    return SW->xrename(tmp, dst) == 0 ? SW_OK : SW_ERR_REPLACE;
  // Cel jest, wiec trzeba go odstawic - ale NIE KOSZTEM pliku, ktory juz tam lezy.
  if (swExists(bak)) return SW_ERR_BUSY;
  if (SW->xrename(dst, bak) != 0) return SW_ERR_REPLACE;   // nie umiemy odstawic - nie ruszamy celu
  if (SW->xrename(tmp, dst) != 0) {
    // `tmp` ZOSTAJE - to jedyna kopia nowej tresci.
    if (SW->xrename(bak, dst) != 0) return SW_ERR_ORPHAN;  // stara tresc lezy jako "<cel>.bak"
    return SW_ERR_REPLACE;                                 // stara tresc wrocila CALA
  }
  SW->xremove(bak);
  return SW_OK;
}

// ---- caly zapis ----
SwRes safeWriteFile(const char* path, SwBody body, void* user) {
  char tmp[SW_PATH];
  if (!path || !body) return SW_ERR_PATH;
  // OBIE nazwy robocze sprawdzamy PRZED pisaniem, jednym buforem (SW_PATH to 400 B
  // i nie ma po co trzymac dwoch na stosie). Gdyby ".bak" byl zajety, a sprawdzalibysmy
  // go dopiero przy podmianie, zapisalibysmy caly plik roboczy tylko po to, zeby
  // odmowic - i porzucony ".part" zablokowalby juz KAZDA nastepna probe.
  if (!swBakPath(path, tmp, sizeof(tmp))) return SW_ERR_PATH;
  if (swExists(path) && swExists(tmp)) return SW_ERR_BUSY;
  if (!swTmpPath(path, tmp, sizeof(tmp))) return SW_ERR_PATH;
  // Pod nazwa pliku roboczego moze lezec cudzy plik ALBO nasza tresc z przerwanego
  // zapisu - w obu przypadkach `fopen(tmp,"w")` skasowalby ja bez slowa.
  if (swExists(tmp)) return SW_ERR_BUSY;
  FILE* f = SW->xopen(tmp, "wb");
  if (!f) return SW_ERR_OPEN;
  SwOut o = { f, true, SW_OK };
  bool bodyOk = body(&o, user);
  // fclose MUSI byc sprawdzony i MUSI byc wolany takze po bledzie - dane siedza
  // w buforze i blad zapisu na karte wychodzi dopiero tutaj, a nie z fwrite.
  int cr = SW->xclose(f);
  if (!o.ok || !bodyOk) {
    SW->xremove(tmp);
    return o.ok ? SW_ERR_BODY : o.res;
  }
  if (cr != 0) { SW->xremove(tmp); return SW_ERR_CLOSE; }
  return safeReplace(tmp, path);
}

struct SwBytes { const void* data; size_t n; };
static bool swBytesBody(SwOut* o, void* user) {
  SwBytes* b = (SwBytes*)user;
  return swWrite(o, b->data, b->n);
}

SwRes safeWriteBytes(const char* path, const void* data, size_t n) {
  SwBytes b = { data, n };
  return safeWriteFile(path, swBytesBody, &b);
}

const char* swErrText(SwRes r) {
  switch (r) {
    case SW_OK:          return "ok";
    case SW_ERR_PATH:    return "sciezka za dluga";
    case SW_ERR_BUSY:    return "plik roboczy .part albo .bak juz istnieje - nie ruszam go";
    case SW_ERR_OPEN:    return "nie moge zalozyc pliku roboczego";
    case SW_ERR_WRITE:   return "zapis nieudany (karta pelna?)";
    case SW_ERR_FMT:     return "wpis nie miesci sie w buforze";
    case SW_ERR_CLOSE:   return "fclose zglosil blad (karta pelna albo wyjeta)";
    case SW_ERR_REPLACE: return "podmiana nieudana, stara tresc na miejscu";
    case SW_ERR_ORPHAN:  return "podmiana nieudana, STARA TRESC ZOSTALA W PLIKU .bak";
    case SW_ERR_BODY:    return "przerwane w trakcie pisania";
  }
  return "?";
}

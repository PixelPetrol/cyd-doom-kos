// Zmienione dla K-OS 2026-09-25 (galaz kos, na bazie HenrysCat/cyd-doom 1c58bf4, GPL-2.0): tlo i uklad paska stanu z lumpu KOSSTBAR (grafika z WAD-u uzytkownika) zamiast wkompilowanego paska GBA Doom II, cyfry STTNUM zamiast STGANUM, sprawdzanie latek paska, bez tabeli amunicji.
/* Emacs style mode select   -*- C++ -*-
 *-----------------------------------------------------------------------------
 *
 *
 *  PrBoom: a Doom port merged with LxDoom and LSDLDoom
 *  based on BOOM, a modified and improved DOOM engine
 *  Copyright (C) 1999 by
 *  id Software, Chi Hoang, Lee Killough, Jim Flynn, Rand Phares, Ty Halderman
 *  Copyright (C) 1999-2000 by
 *  Jess Haas, Nicolas Kalkhof, Colin Phipps, Florian Schulze
 *  Copyright 2005, 2006 by
 *  Florian Schulze, Colin Phipps, Neil Stevens, Andrey Budko
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version 2
 *  of the License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 *  02111-1307, USA.
 *
 * DESCRIPTION:
 *      Status bar code.
 *      Does the face/direction indicator animatin.
 *      Does palette indicators as well (red pain/berserk, bright pickup)
 *
 *-----------------------------------------------------------------------------*/

#include "doomdef.h"
#include "doomstat.h"
#include "m_random.h"
#include "i_video.h"
#include "w_wad.h"
#include "st_stuff.h"
#include "st_lib.h"
#include "r_main.h"
#include "am_map.h"
#include "s_sound.h"
#include "sounds.h"
#include "dstrings.h"
#include "r_draw.h"

#include "global_data.h"
#include "lprintf.h"

#include <string.h>

// K-OS: tlo paska i polozenia widzetow z lumpu KOSSTBAR, ktory kos/wad2kos.py sklada z grafiki
// WAD-u uzytkownika (STBAR + STARMS + STTPRCNT, 320 -> 240 px). Upstream mial tu wkompilowane
// tlo z GBA Doom II (Torus Games, src/doom/gfx/stbar.h) - cudza grafika, ktorej nie wolno
// rozdawac z silnikiem na GPL. Uklad jest w danych, a nie w st_stuff.h, bo zalezy od grafiki:
// wad2kos.py wycina kolumny tla tam, gdzie nie widac szwu, a to wypada inaczej dla DOOM
// i dla Freedoomu. Format musi sie zgadzac z make_stbar() w kos/wad2kos.py.
#define KOS_STBAR_MAGIC  "KOSSTBR1"
#define KOS_STBAR_W      240
#define KOS_STBAR_HDR    64
#define KOS_STBAR_LEN    (KOS_STBAR_HDR + KOS_STBAR_W * ST_HEIGHT)

typedef struct
{
    char  magic[8];
    short w, h;
    // x wzgledem lewej krawedzi, y wzgledem gory paska (jak ST_* w st_stuff.h bez ST_Y);
    // liczby: x = prawa krawedz ostatniej cyfry, jak w STlib_drawNum
    short ammo[2], health[2], armor[2], face[2];
    short arms[6][2];
    short keys[3][2];
} kos_stbar_t;

typedef char kos_stbar_size_check[(sizeof(kos_stbar_t) == KOS_STBAR_HDR) ? 1 : -1];

// Kopia naglowka w RAM (64 B): pola czytamy przez memcpy, wiec wyrownanie lumpu w mapowanym
// flashu nie gra roli, a pozniejsze odczyty ukladu nie siegaja juz do flasha.
static kos_stbar_t kos_sb;

//
// STATUS BAR CODE
//

static void ST_Stop(void);

// Respond to keyboard input events,
//  intercept cheats.
boolean ST_Responder(const event_t *ev)
{
  // Filter automap on/off.
  if (ev->type == ev_keyup && (ev->data1 & 0xffff0000) == AM_MSGHEADER)
    {
      switch(ev->data1)
        {
        case AM_MSGENTERED:
          break;

        case AM_MSGEXITED:
          break;
        }
    }

  return false;
}

static int ST_calcPainOffset(void)
{
  static int lastcalc;
  static int oldhealth = -1;
  int health = _g->player.health > 100 ? 100 : _g->player.health;

  if (health != oldhealth)
    {
      lastcalc = ST_FACESTRIDE * (((100 - health) * ST_NUMPAINFACES) / 101);
      oldhealth = health;
    }
  return lastcalc;
}

//
// This is a not-very-pretty routine which handles
//  the face states and their timing.
// the precedence of expressions is:
//  dead > evil grin > turned head > straight ahead
//

static void ST_updateFaceWidget(void)
{
    int         i;
	angle_t     badguyangle;
	angle_t     diffang;
    static int  lastattackdown = -1;
    static int  priority = 0;
    boolean     doevilgrin;

    if (priority < 10)
    {
        // dead
        if (!_g->player.health)
        {
            priority = 9;
            _g->st_faceindex = ST_DEADFACE;
            _g->st_facecount = 1;
        }
    }

    if (priority < 9)
    {
        if (_g->player.bonuscount)
        {
            // picking up bonus
            doevilgrin = false;

            for (i=0;i<NUMWEAPONS;i++)
            {
                if (_g->oldweaponsowned[i] != _g->player.weaponowned[i])
                {
                    doevilgrin = true;
                    _g->oldweaponsowned[i] = _g->player.weaponowned[i];
                }
            }
            if (doevilgrin)
            {
                // evil grin if just picked up weapon
                priority = 8;
                _g->st_facecount = ST_EVILGRINCOUNT;
                _g->st_faceindex = ST_calcPainOffset() + ST_EVILGRINOFFSET;
            }
        }

    }
	
	//Restore the face looking at enemies direction in this SVN... Cause it's handy! ~Kippykip
	if (priority < 8)
    {
        if (_g->player.damagecount && _g->player.attacker && _g->player.attacker != _g->player.mo)
		{
			// being attacked
			priority = 7;

			// haleyjd 10/12/03: classic DOOM problem of missing OUCH face
			// was due to inversion of this test:
			// if(plyr->health - st_oldhealth > ST_MUCHPAIN)
            if(_g->st_oldhealth - _g->player.health > ST_MUCHPAIN)
			{
				_g->st_facecount = ST_TURNCOUNT;
				_g->st_faceindex = ST_calcPainOffset() + ST_OUCHOFFSET;
			}
			else
			{
                badguyangle = R_PointToAngle2(_g->player.mo->x,
                _g->player.mo->y,
                _g->player.attacker->x,
                _g->player.attacker->y);

                if (badguyangle > _g->player.mo->angle)
				{
					// whether right or left
                    diffang = badguyangle - _g->player.mo->angle;
					i = diffang > ANG180;
				}
				else
				{
					// whether left or right
                    diffang = _g->player.mo->angle - badguyangle;
					i = diffang <= ANG180;
				} // confusing, aint it?


				_g->st_facecount = ST_TURNCOUNT;
				_g->st_faceindex = ST_calcPainOffset();

				if (diffang < ANG45)
				{
					// head-on
					_g->st_faceindex += ST_RAMPAGEOFFSET;
				}
				else if (i)
				{
					// turn face right
					_g->st_faceindex += ST_TURNOFFSET;
				}
				else
				{
					// turn face left
					_g->st_faceindex += ST_TURNOFFSET+1;
				}
			}
		}
    }

    if (priority < 7)
    {
        if (_g->player.damagecount)
        {
            // haleyjd 10/12/03: classic DOOM problem of missing OUCH face
            // was due to inversion of this test:
            // if(plyr->health - st_oldhealth > ST_MUCHPAIN)
            if(_g->st_oldhealth - _g->player.health > ST_MUCHPAIN)
            {
                priority = 7;
                _g->st_facecount = ST_TURNCOUNT;
                _g->st_faceindex = ST_calcPainOffset() + ST_OUCHOFFSET;
            }
            else
            {
                priority = 6;
                _g->st_facecount = ST_TURNCOUNT;
                _g->st_faceindex = ST_calcPainOffset() + ST_RAMPAGEOFFSET;
            }

        }
    }

    if (priority < 6)
    {
        // rapid firing
        if (_g->player.attackdown)
        {
            if (lastattackdown==-1)
                lastattackdown = ST_RAMPAGEDELAY;
            else if (!--lastattackdown)
            {
                priority = 5;
                _g->st_faceindex = ST_calcPainOffset() + ST_RAMPAGEOFFSET;
                _g->st_facecount = 1;
                lastattackdown = 1;
            }
        }
        else
            lastattackdown = -1;

    }

    if (priority < 5)
    {
        // invulnerability
        if ((_g->player.cheats & CF_GODMODE)
                || _g->player.powers[pw_invulnerability])
        {
            priority = 4;

            _g->st_faceindex = ST_GODFACE;
            _g->st_facecount = 1;

        }

    }

    // look left or look right if the facecount has timed out
    if (!_g->st_facecount)
    {
        _g->st_faceindex = ST_calcPainOffset() + (_g->st_randomnumber % 3);
        _g->st_facecount = ST_STRAIGHTFACECOUNT;
        priority = 0;
    }

    _g->st_facecount--;

}

static void ST_updateWidgets(void)
{
    const static int  largeammo = 1994; // means "n/a"
    int         i;

    if(_g->fps_show)
        _g->w_ready.num = &_g->fps_framerate;
    else if (weaponinfo[_g->player.readyweapon].ammo == am_noammo)
        _g->w_ready.num = &largeammo;
    else
        _g->w_ready.num = &_g->player.ammo[weaponinfo[_g->player.readyweapon].ammo];


    // update keycard multiple widgets
    for (i=0;i<3;i++)
    {
        _g->keyboxes[i] = _g->player.cards[i] ? i : -1;

        //jff 2/24/98 select double key
        //killough 2/28/98: preserve traditional keys by config option

        if (_g->player.cards[i+3])
            _g->keyboxes[i] = i+3;
    }

    // refresh everything if this is him coming back to life
    ST_updateFaceWidget();
}

void ST_Ticker(void)
{
  _g->st_randomnumber = M_Random();
  ST_updateWidgets();
  _g->st_oldhealth = _g->player.health;
}


static void ST_doPaletteStuff(void)
{
    int         palette;
    int cnt = _g->player.damagecount;

    if (_g->player.powers[pw_strength])
    {
        // slowly fade the berzerk out
        int bzc = 12 - (_g->player.powers[pw_strength]>>6);
        if (bzc > cnt)
            cnt = bzc;
    }

    if (cnt)
    {
        palette = (cnt+7)>>3;
        if (palette >= NUMREDPALS)
            palette = NUMREDPALS-1;

        /* cph 2006/08/06 - if in the menu, reduce the red tint - navigating to
       * load a game can be tricky if the screen is all red */
        if (_g->menuactive) palette >>=1;

        palette += STARTREDPALS;
    }
    else
        if (_g->player.bonuscount)
        {
            palette = (_g->player.bonuscount+7)>>3;
            if (palette >= NUMBONUSPALS)
                palette = NUMBONUSPALS-1;
            palette += STARTBONUSPALS;
        }
        else
            if (_g->player.powers[pw_ironfeet] > 4*32 || _g->player.powers[pw_ironfeet] & 8)
                palette = RADIATIONPAL;
            else
                palette = 0;

    if (palette != _g->st_palette) {
        V_SetPalette(_g->st_palette = palette); // CPhipps - use new palette function
    }
}

static void ST_drawWidgets(boolean refresh)
{
    STlib_updateNum(&_g->w_ready, CR_RED, refresh);

    // K-OS: bez tabeli amunicji (BULL/SHEL/RCKT/CELL). Pasek PC ma 320 px, ekran 240 px, a grafika
    // idzie z WAD-u 1:1 - wad2kos.py odcina prawy segment paska (x >= 250), bo tylko on nie
    // niesie niczego, bez czego nie da sie grac: biezaca amunicje pokazuje duzy licznik AMMO.

    STlib_updatePercent(&_g->st_health, CR_RED, refresh);

    STlib_updatePercent(&_g->st_armor, CR_RED, refresh);

    STlib_updateMultIcon(&_g->w_faces, refresh);

    for (int i=0;i<3;i++)
        STlib_updateMultIcon(&_g->w_keyboxes[i], refresh);

    for (int i=0;i<6;i++)
        STlib_updateMultIcon(&_g->w_arms[i], refresh);
}

static void ST_doRefresh(void)
{
  // draw status bar background to off-screen buff
  ST_refreshBackground();

  // and refresh all widgets
  ST_drawWidgets(true);

}

static boolean ST_NeedUpdate()
{
	// ready weapon ammo
	if(_g->w_ready.oldnum != *_g->w_ready.num)
        return true;
	
    if(_g->st_health.n.oldnum != *_g->st_health.n.num)
        return true;

    if(_g->st_armor.n.oldnum != *_g->st_armor.n.num)
        return true;

    if(_g->w_faces.oldinum != *_g->w_faces.inum)
        return true;

    // K-OS: tabeli amunicji nie ma na pasku (ST_drawWidgets), wiec jej liczniki nie moga tu
    // wymuszac odswiezania - ich oldnum nigdy by sie nie zmienil i pasek rysowalby sie co klatke.

    // weapons owned
    for(int i=0; i<6; i++)
    {
        if(_g->w_arms[i].oldinum != *_g->w_arms[i].inum)
            return true;
    }

    for(int i = 0; i < 3; i++)
    {
        if(_g->w_keyboxes[i].oldinum != *_g->w_keyboxes[i].inum)
            return true;
    }

    return false;
}

void ST_Drawer(boolean statusbaron, boolean refresh)
{
    /* cph - let status bar on be controlled
   * completely by the call from D_Display
   * proff - really do it
   */

    ST_doPaletteStuff();  // Do red-/gold-shifts from damage/items

    if (statusbaron)
    {
        boolean needupdate = false;

        if(refresh)
        {
            needupdate = true;
            _g->st_needrefresh = 2;
        }
        else if(ST_NeedUpdate())
        {
            needupdate = true;
            _g->st_needrefresh = 2;
        }
        else if(_g->st_needrefresh)
        {
            needupdate = true;
        }

        if(needupdate)
        {
            ST_doRefresh();

            _g->st_needrefresh--;
        }
    }
}



//
// ST_loadGraphics
//
// CPhipps - Loads graphics needed for status bar if doload is true,
//  unloads them otherwise
//
// K-OS: latka widzetu musi lezec cala w pasku, a jej kolumny i posty - w granicach lumpu.
// DLACZEGO: V_DrawPatchNoScale nie przycina, a pasek to ostatnie 32 wiersze bufora klatki, wiec
// latka wystajaca w dol albo w bok pisalaby po stercie za buforem; kolumna albo post za koncem
// lumpu to odczyt obok danych w mapowanym flashu. Grafika pochodzi z WAD-u uzytkownika, wiec
// nie ufamy jej na slowo. Sprawdzamy raz przy starcie - samo rysowanie zostaje bez warunkow.
static void KosCheckPatch(const char* name, int lump, int x, int y)
{
    const byte* b = (const byte*)W_CacheLumpNum(lump);
    const unsigned int len = (unsigned int)W_LumpLength(lump);
    short hd[4];
    int w, h, x0, y0, c;

    if (len < 8)
        goto bad;
    memcpy(hd, b, sizeof(hd));   // memcpy: wyrownanie lumpu nie ma tu znaczenia
    w = hd[0];
    h = hd[1];
    x0 = x - hd[2];
    y0 = y - hd[3];
    if (w <= 0 || h <= 0 || x0 < 0 || x0 + w > KOS_STBAR_W || y0 < ST_Y || y0 + h > SCREENHEIGHT)
        goto bad;
    if (8u + 4u * (unsigned int)w > len)
        goto bad;
    for (c = 0; c < w; c++)
    {
        unsigned int o;
        memcpy(&o, b + 8 + 4 * c, sizeof(o));
        for (;;)   // kazdy krok przesuwa o >= 4 B, wiec petla konczy sie najpozniej na koncu lumpu
        {
            if (o >= len)
                goto bad;
            if (b[o] == 0xff)
                break;
            if (o + 4u > len || (unsigned int)b[o] + b[o + 1] > (unsigned int)h || o + 4u + b[o + 1] > len)
                goto bad;
            o += 4u + b[o + 1];
        }
    }
    return;
bad:
    I_Error("KOSSTBAR: latka %.8s nie pasuje do paska (x %d, y %d) - przygotuj doom.kwad jeszcze raz", name, x, y);
}

static const patch_t* KosStPatch(const char* name, int x, int y)
{
    const int lump = W_GetNumForName(name);
    KosCheckPatch(name, lump, x, y);
    return (const patch_t*)W_CacheLumpNum(lump);
}

static void KosLoadStbar(void)
{
    const int lump = W_CheckNumForName("KOSSTBAR");
    const byte* b;

    // Dlugosc co do bajtu: ST_refreshBackground kopiuje stbar_len bajtow bez pytania.
    if (lump < 0 || W_LumpLength(lump) != KOS_STBAR_LEN)
        I_Error("Brak paska stanu KOSSTBAR - przygotuj doom.kwad nowym kos/wad2kos.py");
    b = (const byte*)W_CacheLumpNum(lump);
    memcpy(&kos_sb, b, sizeof(kos_sb));
    if (memcmp(kos_sb.magic, KOS_STBAR_MAGIC, 8) || kos_sb.w != KOS_STBAR_W || kos_sb.h != ST_HEIGHT)
        I_Error("KOSSTBAR: nieznany format - przygotuj doom.kwad nowym kos/wad2kos.py");
    _g->stbarbg = (const patch_t*)(b + KOS_STBAR_HDR);
    _g->stbar_len = KOS_STBAR_W * ST_HEIGHT;
}

static void ST_loadGraphics(boolean doload)
{
    int  i, j, k, facenum;
    short w0 = 0;
    char namebuf[9];
    const short* numat[3];

    KosLoadStbar();

    // Load the numbers, tall and short
    for (i=0;i<10;i++)
    {
        // K-OS: STTNUM z WAD-u uzytkownika. Upstream bral STGANUM - cyfry GBA Doom II
        // z gbadoom.wad (Torus Games), 10 px zamiast 14 px, pod tamto tlo paska.
        sprintf(namebuf, "STTNUM%d", i);
        _g->tallnum[i] = (const patch_t *) W_CacheLumpName(namebuf);

        sprintf(namebuf, "STYSNUM%d", i);
        _g->shortnum[i] = (const patch_t *) W_CacheLumpName(namebuf);
    }

    // K-OS: kazda cyfra w kazdym miejscu, w ktorym STlib_drawNum moze ja postawic
    // (3 cyfry od prawej, krok = szerokosc "0", jak w st_lib.c).
    if (W_LumpLength(W_GetNumForName("STTNUM0")) >= 8)
        memcpy(&w0, _g->tallnum[0], sizeof(w0));
    if (w0 <= 0 || w0 > KOS_STBAR_W)
        I_Error("KOSSTBAR: zla szerokosc cyfry STTNUM0 (%d)", w0);
    numat[0] = kos_sb.ammo;
    numat[1] = kos_sb.health;
    numat[2] = kos_sb.armor;
    for (i = 0; i < 3; i++)
        for (k = 1; k <= 3; k++)
            for (j = 0; j < 10; j++)
            {
                sprintf(namebuf, "STTNUM%d", j);
                KosCheckPatch(namebuf, W_GetNumForName(namebuf), numat[i][0] - k * w0, ST_Y + numat[i][1]);
            }

    // Load percent key.
    //Note: why not load STMINUS here, too?
    // K-OS: procent jest wrysowany w KOSSTBAR (jak w pasku GBA upstreamu), wiec tylko ladujemy.
    _g->tallpercent = (const patch_t*) W_CacheLumpName("STTPRCNT");

    // key cards
    for (i=0;i<NUMCARDS;i++)
    {
        sprintf(namebuf, "STKEYS%d", i);
        _g->keys[i] = (const patch_t *) W_CacheLumpName(namebuf);
    }
    // K-OS: okienko i pokazuje karte i albo czaszke i+3 (ST_updateWidgets)
    for (i = 0; i < 3; i++)
        for (k = 0; k < 2; k++)
        {
            sprintf(namebuf, "STKEYS%d", i + 3 * k);
            KosCheckPatch(namebuf, W_GetNumForName(namebuf), kos_sb.keys[i][0], ST_Y + kos_sb.keys[i][1]);
        }

    // arms ownership widgets
    for (i=0;i<6;i++)
    {
        sprintf(namebuf, "STGNUM%d", i+2);

        // gray #
        _g->arms[i][0] = KosStPatch(namebuf, kos_sb.arms[i][0], ST_Y + kos_sb.arms[i][1]);

        // yellow #
        sprintf(namebuf, "STYSNUM%d", i+2);
        KosCheckPatch(namebuf, W_GetNumForName(namebuf), kos_sb.arms[i][0], ST_Y + kos_sb.arms[i][1]);
        _g->arms[i][1] = (const patch_t *) _g->shortnum[i+2];
    }

    // face states
    facenum = 0;

    for (i=0;i<ST_NUMPAINFACES;i++)
    {
        for (j=0;j<ST_NUMSTRAIGHTFACES;j++)
        {
            sprintf(namebuf, "STFST%d%d", i, j);
            _g->faces[facenum++] = KosStPatch(namebuf, kos_sb.face[0], ST_Y + kos_sb.face[1]);
        }
        sprintf(namebuf, "STFTR%d0", i);	// turn right
        _g->faces[facenum++] = KosStPatch(namebuf, kos_sb.face[0], ST_Y + kos_sb.face[1]);
        sprintf(namebuf, "STFTL%d0", i);	// turn left
        _g->faces[facenum++] = KosStPatch(namebuf, kos_sb.face[0], ST_Y + kos_sb.face[1]);
        sprintf(namebuf, "STFOUCH%d", i);	// ouch!
        _g->faces[facenum++] = KosStPatch(namebuf, kos_sb.face[0], ST_Y + kos_sb.face[1]);
        sprintf(namebuf, "STFEVL%d", i);	// evil grin ;)
        _g->faces[facenum++] = KosStPatch(namebuf, kos_sb.face[0], ST_Y + kos_sb.face[1]);
        sprintf(namebuf, "STFKILL%d", i);	// pissed off
        _g->faces[facenum++] = KosStPatch(namebuf, kos_sb.face[0], ST_Y + kos_sb.face[1]);
    }
    _g->faces[facenum++] = KosStPatch("STFGOD0", kos_sb.face[0], ST_Y + kos_sb.face[1]);
    _g->faces[facenum++] = KosStPatch("STFDEAD0", kos_sb.face[0], ST_Y + kos_sb.face[1]);
}

static void ST_loadData(void)
{
  ST_loadGraphics(true);
}

static void ST_initData(void)
{
    int i;

    _g->st_statusbaron = true;

    _g->st_faceindex = 0;
    _g->st_palette = -1;

    _g->st_oldhealth = -1;

    for (i=0;i<NUMWEAPONS;i++)
        _g->oldweaponsowned[i] = _g->player.weaponowned[i];

    for (i=0;i<3;i++)
        _g->keyboxes[i] = -1;

    STlib_init();
}

static void ST_createWidgets(void)
{
    int i;

    // K-OS: polozenia z KOSSTBAR (kos_sb, y wzgledem gory paska), a nie ze stalych ST_* -
    // tamte byly pod pasek GBA Doom II i jego waskie cyfry.

    // ready weapon ammo
    STlib_initNum(&_g->w_ready,
		kos_sb.ammo[0],
		ST_Y + kos_sb.ammo[1],
		_g->tallnum,
        &_g->player.ammo[weaponinfo[_g->player.readyweapon].ammo],
		&_g->st_statusbaron,
		ST_AMMOWIDTH );

    // health percentage
    STlib_initPercent(&_g->st_health,
			kos_sb.health[0],
			ST_Y + kos_sb.health[1],
			_g->tallnum,
            &_g->player.health,
			&_g->st_statusbaron,
			_g->tallpercent);

    // armor percentage - should be colored later
    STlib_initPercent(&_g->st_armor,
			kos_sb.armor[0],
			ST_Y + kos_sb.armor[1],
			_g->tallnum,
            &_g->player.armorpoints,
			&_g->st_statusbaron, _g->tallpercent);

    // weapons owned
    for(i=0;i<6;i++)
    {
        STlib_initMultIcon(&_g->w_arms[i],
			kos_sb.arms[i][0],
			ST_Y + kos_sb.arms[i][1],
            _g->arms[i], (int*) &_g->player.weaponowned[i+1],
			&_g->st_statusbaron);
    }

    // keyboxes 0-2
    for(i=0;i<3;i++)
    {
        STlib_initMultIcon(&_g->w_keyboxes[i],
            kos_sb.keys[i][0],
            ST_Y + kos_sb.keys[i][1],
            _g->keys,
            &_g->keyboxes[i],
            &_g->st_statusbaron);
    }

    // K-OS: bez tabeli amunicji (w_ammo, w_maxammo) - patrz ST_drawWidgets.

    // faces
    STlib_initMultIcon(&_g->w_faces,
			kos_sb.face[0],
			ST_Y + kos_sb.face[1],
			_g->faces,
			&_g->st_faceindex,
			&_g->st_statusbaron);
}

static boolean st_stopped = true;

void ST_Start(void)
{
  if (!st_stopped)
    ST_Stop();
  ST_initData();
  ST_createWidgets();
  st_stopped = false;
}

static void ST_Stop(void)
{
  if (st_stopped)
    return;
  V_SetPalette(0);
  st_stopped = true;
}

void ST_Init(void)
{
  ST_loadData();
}

#include "cps.h"
#include "bitswap.h"

// CPS (palette)

static bool CpsPaletteRestored = false;	// CpsPalVisualScan() restored CpsPal
static UINT8* CpsPalSrc = NULL;			// Copy of current input palette
static UINT16* CpsPalRaw = NULL;			// Effective hardware palette words
UINT32* CpsPal = NULL;					// Hicolor version of palette
INT32 nCpsPalCtrlReg;
INT32 bCpsUpdatePalEveryFrame = 0;		// Some of the hacks need this as they don't write to CpsReg 0x0a

INT32 CpsPalInit()
{
	INT32 nLen = 0;
	bool bRawPalette = Cps2Board.pDisplayState && Cps2Board.pDisplayState->bRawPalette;

	nLen = 0xc00 * sizeof(UINT16);
	// Keep effective hardware colors separately from the DMA source snapshot.
	if (bRawPalette) nLen *= 2;
	CpsPalSrc = (UINT8*)BurnMalloc(nLen);
	if (CpsPalSrc == NULL) {
		return 1;
	}
	memset(CpsPalSrc, 0, nLen);
	CpsPalRaw = bRawPalette ? (UINT16*)CpsPalSrc + 0xc00 : NULL;

	nLen = 0xc00 * sizeof(UINT32);
	CpsPal = (UINT32*)BurnMalloc(nLen);
	if (CpsPal == NULL) {
		return 1;
	}
	if (Cps2Board.pDisplayState) memset(CpsPal, 0, nLen);
	CpsPaletteRestored = false;

	return 0;
}

INT32 CpsPalExit()
{
	BurnFree(CpsPal);
	BurnFree(CpsPalSrc);
	CpsPalRaw = NULL;
	return 0;
}

static inline UINT32 CpsPalColor(UINT16 nRaw)
{
	INT32 Palette = BURN_ENDIAN_SWAP_INT16(nRaw);
	INT32 Bright = 0x0f + ((Palette >> 12) << 1);
	INT32 r = ((Palette >> 8) & 0x0f) * 0x11 * Bright / 0x2d;
	INT32 g = ((Palette >> 4) & 0x0f) * 0x11 * Bright / 0x2d;
	INT32 b = ((Palette >> 0) & 0x0f) * 0x11 * Bright / 0x2d;
	return BurnHighCol(r, g, b, 0);
}

// Update CpsPal with the new palette at pNewPal (length 0xc00 words)
INT32 CpsPalUpdate(UINT8* pNewPal)
{
	UINT16 *ps, *pn;

	ps = (UINT16*)CpsPalSrc;
	pn = (UINT16*)pNewPal;

	memcpy(ps, pn, 0xc00 * sizeof(UINT16));

	INT32 nCtrl = CpsReg[nCpsPalCtrlReg];
	UINT16 *PaletteRAM = (UINT16*)CpsPalSrc;

	for (INT32 nPage = 0; nPage < 6; nPage++) {
		if (BIT(nCtrl, nPage)) {
			for (INT32 Offset = 0; Offset < 0x200; ++Offset) {
				UINT16 nRaw = *PaletteRAM++;
				// Disabled DMA destinations retain their previous hardware colors.
				if (CpsPalRaw) CpsPalRaw[(0x200 * nPage) + Offset] = nRaw;
				CpsPal[(0x200 * nPage) + (Offset ^ 15)] = CpsPalColor(nRaw);
			}
		} else {
			if (PaletteRAM != (UINT16*)CpsPalSrc) {
				PaletteRAM += 0x200;
			}
		}
	}

	return 0;
}

// Palette RAM can already contain the next DMA's colors. Save the effective
// palette instead; hardware words can be converted to the current pixel format.
INT32 CpsPalVisualScan(INT32 nAction)
{
	if (!Cps2Board.pDisplayState || !(nAction & ACB_DRIVER_DATA)) return 0;

	struct BurnArea ba;
	memset(&ba, 0, sizeof(ba));
	if (!Cps2Board.pDisplayState->bRawPalette) {
		// Converted colors use the frontend's current pixel format.
		ba.Data = CpsPal;
		ba.nLen = 0xc00 * sizeof(UINT32);
	} else {
		ba.Data = CpsPalRaw;
		ba.nLen = 0xc00 * sizeof(UINT16);
	}
	ba.szName = Cps2Board.pDisplayState->szEffectivePalette;
	BurnAcb(&ba);

	if (nAction & ACB_WRITE) {
		if (Cps2Board.pDisplayState->bRawPalette) {
			UINT16* ps = CpsPalRaw;
			for (INT32 i = 0; i < 0xc00; i++) CpsPal[i ^ 15] = CpsPalColor(ps[i]);
		}
		CpsPaletteRestored = true;
	}

	return 0;
}

INT32 CpsPalConsumeRestoredCache()
{
	if (!Cps2Board.pDisplayState || !CpsPaletteRestored) return 0;

	CpsPaletteRestored = false;

	return 1;
}

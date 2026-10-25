#include "cps.h"
#include "xmvsf3v3.h"

// Ordinary CPS2 is the default; drivers configure only the hardware they change.
static const Cps2BoardConfig Cps2BoardDefault = {
	0, 0, CPS2_ROM_SOCKETS,
	~0U, 0, ~0U, 0x6000, 0, false, true,
	8000000, 0x8000,
	NULL,
	NULL, NULL, NULL, NULL, NULL, NULL, NULL
};
Cps2BoardConfig Cps2Board = Cps2BoardDefault;

void Cps2BoardReset()
{
	Cps2Board = Cps2BoardDefault;
}

static const Cps2DisplayState Cps2PlusDisplayState = {
	"Latched palette", "Latched registers", "Latched frg",
	"Sprite lag buffers", "Effective palette", true
};

static UINT32 Cps2PlusObjectOffset(UINT32 a)
{
	// A15 and the bank register choose physical RAM; A13 and A14 mirror its entries.
	return (((a >> 15) ^ nCpsObjectBank) & 1) * 0x8000 + (a & 0x1fff);
}

static UINT8 __fastcall Cps2PlusObjectReadByte(UINT32 a)
{
	return CpsRam708[Cps2PlusObjectOffset(a) ^ 1];
}

static UINT16 __fastcall Cps2PlusObjectReadWord(UINT32 a)
{
	return BURN_ENDIAN_SWAP_INT16(*(UINT16*)(CpsRam708 + Cps2PlusObjectOffset(a)));
}

static void Cps2PlusObjectWrite(UINT32 a)
{
	// A14 is a write selector, not an extra address line in object RAM.
	UINT32 nOffset = Cps2PlusObjectOffset(a);
	CpsObjGfxBank[(nOffset >> 15) * 0x400 + ((a & 0x1fff) >> 3)] = (a >> 14) & 1;
}

static void __fastcall Cps2PlusObjectWriteByte(UINT32 a, UINT8 d)
{
	CpsRam708[Cps2PlusObjectOffset(a) ^ 1] = d;
	Cps2PlusObjectWrite(a);
}

static void __fastcall Cps2PlusObjectWriteWord(UINT32 a, UINT16 d)
{
	*(UINT16*)(CpsRam708 + Cps2PlusObjectOffset(a)) = BURN_ENDIAN_SWAP_INT16(d);
	Cps2PlusObjectWrite(a);
}

static void Cps2PlusMapMemory()
{
	SekMapMemory(CpsRom + 0x400000, 0xa00000, 0xdfffff, MAP_ROM);
	// Both banks and their mirrors use the same handlers; the register selects physical RAM.
	SekMapHandler(4, 0x700000, 0x70ffff, MAP_RAM);
	SekSetReadByteHandler(4, Cps2PlusObjectReadByte);
	SekSetReadWordHandler(4, Cps2PlusObjectReadWord);
	SekSetWriteByteHandler(4, Cps2PlusObjectWriteByte);
	SekSetWriteWordHandler(4, Cps2PlusObjectWriteWord);
}

static UINT8 __fastcall Cps2PlusSoundRead(UINT16 a)
{
	if (a == 0xd007) return (QscRead() & 0x80) | 0x60 | QsndZGetBank();
	return QsndZRead(a);
}

static void __fastcall Cps2PlusSoundWrite(UINT16 a, UINT8 d)
{
	if (a == 0xd003) {
		INT32 nBank = d & 0x1f;
		QsndZSetBank(nBank < 30 ? nBank : 0);
		return;
	}
	QsndZWrite(a, d);
}

static INT32 Cps2PlusSoundInit()
{
	if (nCpsZRomLen == 0x80000) {
		ZetOpen(0);
		ZetSetReadHandler(Cps2PlusSoundRead);
		ZetSetWriteHandler(Cps2PlusSoundWrite);
		ZetClose();
	}
	return 0;
}

static INT32 Cps2PlusReset()
{
	memset(CpsObjGfxBank, 0, 0x800);
	CpsObjReset();
	ZetOpen(0);
	QsndZReset();
	QscReset();
	ZetClose();
	return 0;
}

static INT32 Cps2PlusExit()
{
	BurnFree(CpsObjGfxBank);
	return 0;
}

static INT32 Cps2PlusMemoryScan(INT32 nAction, INT32*)
{
	if (nAction & ACB_DRIVER_DATA) {
		struct BurnArea ba;
		memset(&ba, 0, sizeof(ba));
		ba.Data = CpsObjGfxBank;
		ba.nLen = 0x800;
		ba.szName = "CPS2+ object banks";
		BurnAcb(&ba);
		if (nAction & ACB_WRITE) {
			for (INT32 i = 0; i < 0x800; i++) {
				if (CpsObjGfxBank[i] > 1) return 1;
			}
		}
	}
	return 0;
}

static INT32 Cps2PlusScan(INT32 nAction, INT32*)
{
	return CpsVisualScan(nAction);
}

static void Cps2PlusFrame()
{
	// The restored palette is already current; subsequent DMA updates enabled pages.
	if (CpsPalConsumeRestoredCache()) CpsRecalcPal = 0;
}

// Validate the expanded board before the common CPS2 loader allocates or decodes ROMs.
static INT32 Cps2PlusCheckROMs(UINT32 nSampleMask)
{
	UINT32 nSize[16] = { 0 };
	UINT32 nGfxLane = 0, nGfxSize = 0;
	struct BurnRomInfo ri;

	for (INT32 i = 0; !BurnDrvGetRomInfo(&ri, i); i++) {
		if (!ri.nLen) continue;
		UINT32 nType = ri.nType & 0x0f;
		if ((nGfxLane & 3) && nType != CPS2_GFX) return 1;
		UINT32 nLimit;
		switch (nType) {
			case CPS2_PRG_68K:        nLimit = 0x800000; break;
			case CPS2_GFX:            nLimit = 0x4000000; break;
			case CPS2_PRG_Z80:        nLimit = 0x80000; break;
			case CPS2_QSND:           nLimit = 0x2000000; break;
			case CPS2_ENCRYPTION_KEY: nLimit = 0x14; break;
			default: return 1;
		}
		if (ri.nLen > nLimit - nSize[nType]) return 1;
		if ((nType == CPS2_PRG_68K || nType == CPS2_QSND) && (ri.nLen & 1)) return 1;
		if (nType == CPS2_ENCRYPTION_KEY && ri.nLen != 0x14) return 1;
		if (nType == CPS2_GFX) {
			if ((nGfxLane & 3) == 0) {
				// The common loader packs smaller graphics groups after larger ones.
				if (nGfxSize && ri.nLen > nGfxSize) return 1;
				nGfxSize = ri.nLen;
			}
			if (ri.nLen < 0x80000 || (ri.nLen & 0x7ffff) || ri.nLen != nGfxSize) return 1;
			nGfxLane++;
		}
		nSize[nType] += ri.nLen;
	}

	if (nSize[CPS2_PRG_68K] != 0x800000 || nSize[CPS2_GFX] < 0x2000000 ||
		(nSize[CPS2_PRG_Z80] != 0x40000 && nSize[CPS2_PRG_Z80] != 0x80000) ||
		nSize[CPS2_ENCRYPTION_KEY] != 0x14 || (nGfxLane & 3)) return 1;
	UINT32 nSamples = nSize[CPS2_QSND];
	if (nSamples != 0x800000 && nSamples != 0x1000000 && nSamples != 0x2000000) return 1;
	return nSamples <= nSampleMask;
}

INT32 Cps2PlusInit(INT32 nSampleBits)
{
	Cps = 2;
	Cps2BoardReset();
	nCpsQSamMask = 0;
	if (nSampleBits != 23 && nSampleBits != 24 && nSampleBits != 25) return 1;

	UINT32 nSampleMask = (1U << nSampleBits) - 1;
	if (Cps2PlusCheckROMs(nSampleMask)) return 1;

	CpsObjGfxBank = (UINT8*)BurnMalloc(0x800);
	if (CpsObjGfxBank == NULL) return 1;
	memset(CpsObjGfxBank, 0, 0x800);

	Cps2Board.nProgramMapSize = 0x400000;
	Cps2Board.nGfxSize = 0x4000000;
	Cps2Board.nScrollMask = 0x1ffffff;
	Cps2Board.nScrollBankAttribute = 0x200;
	Cps2Board.pDisplayState = &Cps2PlusDisplayState;
	Cps2Board.MapMemory = Cps2PlusMapMemory;
	Cps2Board.bObjectBankHandlers = true;
	Cps2Board.MemoryScan = Cps2PlusMemoryScan;
	CpsRunInitCallbackFunction = Cps2PlusSoundInit;
	CpsRunResetCallbackFunction = Cps2PlusReset;
	CpsRunExitCallbackFunction = Cps2PlusExit;
	CpsRunFrameStartCallbackFunction = Cps2PlusFrame;
	CpsMemScanCallbackFunction = Cps2PlusScan;
	nCpsQSamMask = nSampleMask;
	INT32 nRet = Cps2Init();
	// The frontend calls the driver's exit function after a failed init.
	// Before decryption, CpsCode still points inside the shared ROM allocation.
	if (nRet && !nCpsCodeLen) CpsCode = NULL;
	return nRet;
}

// SFZ3 Mix: linear ROM dumps, expanded sprite addresses and external music samples.
UINT8 AspectDIP;
static UINT8 AspectDIPLast;

// samples:
// empty + 0x3f L, channel 0, sample + 0x00
// empty + 0x3f R, channel 1, sample + 0x40

static void samples_l_start(int channel, int sample, int loop)
{
	BurnSampleChannelPlay(channel + 0x00, sample + 0x00, loop);
}

static int samples_l_playing()
{
	return BurnSampleGetChannelStatus(0x00) == SAMPLE_PLAYING;
}
static int samples_r_playing()
{
	return BurnSampleGetChannelStatus(0x01) == SAMPLE_PLAYING;
}

static void samples_r_start(int channel, int sample, int loop)
{
	BurnSampleChannelPlay(channel + 0x01, sample + 0x40, loop);
}

// ----------------------------------------------------------------------------
static void qsound_cps2turbo_write(UINT32 offset, UINT8 data) //Zero800 allows you to write and signal when a new song is played
{
	UINT8 *m_qsound_sharedram1 = CpsZRamC0;
	UINT8 *m_qsound_sharedram2 = CpsZRamF0;
	if (offset == 0x0001 && (data & 0xff) == 0x00 && m_qsound_sharedram1[0x00] == 0xff) //stop samples sound
	{
		samples_l_start(0, 0, 0);
		samples_r_start(0, 0, 0);
		bprintf(0, _T("Stop: %02x to %x\n"), offset, offset);
	}

	if (offset == 0xffe)
	{
		if (m_qsound_sharedram1[0x80] == 0x77) //stop music condition in audiocpu = C080 (z80 hacked)
		{
			samples_l_start(0, 0, 0); //init L samples
			samples_r_start(0, 0, 0); //init R samples
			m_qsound_sharedram2[0x27] = 0xc9; //Enable Qsound Volume
			m_qsound_sharedram1[0x80] = 0xff; //Reset music id flag
			m_qsound_sharedram1[0x81] = 0xff; //Reset music id flag
			bprintf(0, _T("Clear: %02x to %x\n"), data, offset);
		}
		else
			if (m_qsound_sharedram1[0x81] > 0x00 && m_qsound_sharedram1[0x81] < 0x40) //play music condition in audiocpu = C081 (z80 hacked)
			{
				int data_byte = m_qsound_sharedram1[0x81];
				m_qsound_sharedram1[0x80] = 0xff; //Reset music id flag
				m_qsound_sharedram1[0x81] = 0xff; //Reset music id flag
				bprintf(0, _T("Music: %02x to %x\n"), data_byte, offset);
				samples_l_start(0, data_byte, 1);
				if (samples_l_playing() == 1)
				{
					m_qsound_sharedram2[0x27] = 0x00; //Qsound music volume = 0
				}
				else
					samples_l_start(0, 0, 0); //init L samples

				samples_r_start(0, data_byte, 1);
				if (samples_r_playing() == 1)
				{
					m_qsound_sharedram2[0x27] = 0x00; //Qsound music volume = 0
				}
				else
					samples_r_start(0, 0, 0); //init R samples
			}
			else
				if (m_qsound_sharedram2[0x27] == 0x00 && samples_l_playing() != 1) //if loadstate and qsound volume = 0 && samples stopped
				{
					samples_l_start(0, 0, 0); //init L samples
					samples_r_start(0, 0, 0); //init R samples
					m_qsound_sharedram2[0x27] = 0xc9; //Enable Qsound Volume
					m_qsound_sharedram1[0x80] = 0xff; //Reset music id flag
					m_qsound_sharedram1[0x81] = 0xff; //Reset music id flag
					bprintf(0, _T("Volume0: %02x to %x\n"), data, offset);
				}
				else
					if (m_qsound_sharedram2[0x27] != 0x00 && samples_l_playing() == 1) //if loadstate and qsound volume != 0 && samples playing
					{
						samples_l_start(0, 0, 0); //init L samples
						samples_r_start(0, 0, 0); //init R samples
						m_qsound_sharedram2[0x27] = 0xc9; //Enable Qsound Volume
						m_qsound_sharedram1[0x80] = 0xff; //Reset music id flag
						m_qsound_sharedram1[0x81] = 0xff; //Reset music id flag
						bprintf(0, _T("Volume0: %02x to %x\n"), data, offset);
					}
					else
						if ((data & 0xff) == 0x55) //init samples in rom start
						{
							samples_l_start(0, 0, 0);
							samples_r_start(0, 0, 0);
						}
	}

	m_qsound_sharedram1[offset] = data;	//Play data Qsound
}

static void check_aspect()
{
	if ((AspectDIP & 0x03) != AspectDIPLast) {
		INT32 aspects[3][2] = { { 16, 9 }, { 4, 3 }, { 112, 81 } };
		AspectDIPLast = AspectDIP & 0x03;

		INT32 nAspectX, nAspectY;
		BurnDrvGetAspect(&nAspectX, &nAspectY);

		if (nAspectX != aspects[AspectDIPLast][0] || nAspectY != aspects[AspectDIPLast][1]) {
			bprintf(0, _T("*  CPS-2: Changing to %d:%d aspect\n"), aspects[AspectDIPLast][0], aspects[AspectDIPLast][1]);
			BurnDrvSetAspect(aspects[AspectDIPLast][0], aspects[AspectDIPLast][1]);
			Reinitialise();
		}
	}
}

static void __fastcall Cps2MixWriteByte(UINT32 a, UINT8 d)
{
	if ((a & 0xfffff0) == 0x665000 || (a & 0xfffff0) == 0xfffff0) {
		CpsFrg[a & 0x0f] = d;
		return;
	}
	if ((a & 0xfffff0) != 0x400000) CpsWriteByte(a, d);
}

void __fastcall CPSQSoundC0WriteByte(UINT32 a, UINT8 d);

static void __fastcall Cps2MixSoundWrite(UINT32 a, UINT8 d)
{
	if (a & 1) qsound_cps2turbo_write((a & 0x1fff) >> 1, d);
	CPSQSoundC0WriteByte(a, d);
}

static void Cps2MixMapMemory()
{
	SekSetWriteByteHandler(0, Cps2MixWriteByte);
	SekSetWriteByteHandler(1, Cps2MixSoundWrite);
}

static INT32 Cps2MixInit()
{
	BurnSampleInit(1);
	for (INT32 i = 0; i < 0x40; i++) {
		BurnSampleSetRoute(i + 0x00, BURN_SND_SAMPLE_ROUTE_1, 0.10, BURN_SND_ROUTE_LEFT);
		BurnSampleSetRoute(i + 0x00, BURN_SND_SAMPLE_ROUTE_2, 0.10, BURN_SND_ROUTE_NONE);
		BurnSampleSetRoute(i + 0x40, BURN_SND_SAMPLE_ROUTE_1, 0.10, BURN_SND_ROUTE_NONE);
		BurnSampleSetRoute(i + 0x40, BURN_SND_SAMPLE_ROUTE_2, 0.10, BURN_SND_ROUTE_RIGHT);
	}
	return 0;
}

static INT32 Cps2MixReset()
{
	AspectDIPLast = 0xff;
	check_aspect();
	return 0;
}

static INT32 Cps2MixExit()
{
	BurnSampleExit();
	return 0;
}

void Cps2MixConfig()
{
	Cps2BoardReset();
	Cps2Board.nProgramMapSize = 0x400000;
	Cps2Board.nRomLayout = CPS2_ROM_LINEAR;
	Cps2Board.nScroll3Mask = ~0x800000U;
	Cps2Board.nObjectAddressMask = 0xe000;
	Cps2Board.nObjectAliasBit = 0x1000;
	Cps2Board.bParentBlend = false;
	Cps2Board.nZ80Clock = 16000000;
	Cps2Board.nZ80BankOffset = 0x10000;
	Cps2Board.MapMemory = Cps2MixMapMemory;
	Cps2Board.SoundRender = BurnSampleRender;
	Cps2Board.SoundScan = BurnSampleScan;
	CpsRunInitCallbackFunction = Cps2MixInit;
	CpsRunResetCallbackFunction = Cps2MixReset;
	CpsRunExitCallbackFunction = Cps2MixExit;
	CpsRunFrameStartCallbackFunction = check_aspect;
	nCPS68KClockspeed = 32000000;
}

// XMvsSF 3v3: an input device, extra work RAM and optional wide-screen graphics.
static const Cps2DisplayState CpsXmvDisplayState = {
	"XMvsSF 3v3 latched palette", "XMvsSF 3v3 latched registers", "XMvsSF 3v3 latched frg",
	"XMvsSF 3v3 sprite lag buffers", "XMvsSF 3v3 effective palette", false
};

// X-Men Vs. Street Fighter 3v3 + DHC board additions (see xmvsf3v3.h), enabled by its drivers only
UINT8 CpsXmvSixButtons[XMV6_PLAYERS][13];
static INT32 CpsXmvWide;
static UINT16 CpsXmvSixCapabilities = 0;
static XmvSixInputState CpsXmvSixState;
static UINT8 *CpsXmvSixWorkRam = NULL;

static INT32 CpsXmvSixExit()
{
	CpsXmvWide = 0;
	BurnFree(CpsXmvSixWorkRam);
	return 0;
}

static INT32 CpsXmvSixReset()
{
	XmvSixReset(&CpsXmvSixState);
	memset(CpsXmvSixWorkRam, 0, XMV6_WORK_RAM_BYTES);

	return 0;
}

static INT32 CpsXmvSixScan(INT32 nAction, INT32* /*pnMin*/)
{
	if (nAction & ACB_MEMORY_RAM) {
		struct BurnArea ba;
		memset(&ba, 0, sizeof(ba));
		ba.Data		= CpsXmvSixWorkRam;
		ba.nLen		= XMV6_WORK_RAM_BYTES;
		ba.nAddress	= XMV6_WORK_RAM_BASE;
		ba.szName	= "XMvsSF 3v3 work RAM";
		BurnAcb(&ba);
	}

	if (nAction & ACB_DRIVER_DATA) {
		SCAN_VAR(CpsXmvSixState);

		if (nAction & ACB_WRITE) {
			if (CpsXmvSixState.abi != XMV6_VERSION || CpsXmvSixState.count > XMV6_DEPTH) {
				// not a state of this board
				CpsXmvSixReset();
				return 1;
			}
		}
	}

	return CpsVisualScan(nAction);
}

// Wide screen: while the ROM keeps 'XS1X' at 0x813ff8, scroll1 columns 64-127 come from 0x814000
static const UINT8* CpsXmvScroll1Extension()
{
	const UINT8* tag = CpsXmvSixWorkRam + 0x3ff8;
	if (tag[0] != 'X' || tag[1] != 'S' || tag[2] != '1' || tag[3] != 'X') return NULL;

	return CpsXmvSixWorkRam + 0x4000;
}

// Once a frame, before the input ports are compiled
static INT32 CpsXmvSixSample()
{
	UINT16 raw[XMV6_PLAYERS];
	for (INT32 p = 0; p < XMV6_PLAYERS; p++) {
		raw[p] = 0;
		for (INT32 bit = 0; bit < 13; bit++) {
			raw[p] |= (CpsXmvSixButtons[p][bit] & 1) << bit;
		}
	}
	XmvSixSample(&CpsXmvSixState, raw);

	// P1 and P2 also reach the stock input ports, for the game's own menus
	for (INT32 bit = 0; bit < 7; bit++) {
		CpsInp001[bit] = (CpsXmvSixState.held[0] >> bit) & 1;
		CpsInp000[bit] = (CpsXmvSixState.held[1] >> bit) & 1;
	}
	for (INT32 p = 0; p < 2; p++) {
		UINT16 value = CpsXmvSixState.held[p];
		CpsInp011[p * 4 + 0] = (value >>  8) & 1;	// weak kick
		CpsInp011[p * 4 + 1] = (value >>  9) & 1;	// medium kick
		CpsInp020[p + 0]     = (value >> 11) & 1;	// start
		CpsInp020[p + 4]     = (value >> 12) & 1;	// coin
	}
	CpsInp011[2] = (CpsXmvSixState.held[0] >> 10) & 1;	// P1 strong kick
	CpsInp020[6] = (CpsXmvSixState.held[1] >> 10) & 1;	// P2 strong kick
	return 2;
}

static UINT8 __fastcall CpsXmvReadByte(UINT32 a)
{
	return XmvSixReadByte(&CpsXmvSixState, CpsXmvSixCapabilities, a - XMV6_BASE);
}

static UINT16 __fastcall CpsXmvReadWord(UINT32 a)
{
	return XmvSixReadWord(&CpsXmvSixState, CpsXmvSixCapabilities, a - XMV6_BASE);
}

static UINT8 __fastcall CpsXmvRamReadByte(UINT32 a)
{
	return CpsXmvSixWorkRam[a - XMV6_WORK_RAM_BASE];
}

static UINT16 __fastcall CpsXmvRamReadWord(UINT32 a)
{
	return XmvSixRamReadWord(CpsXmvSixWorkRam, a - XMV6_WORK_RAM_BASE);
}

static void __fastcall CpsXmvRamWriteByte(UINT32 a, UINT8 d)
{
	CpsXmvSixWorkRam[a - XMV6_WORK_RAM_BASE] = d;
}

static void __fastcall CpsXmvRamWriteWord(UINT32 a, UINT16 d)
{
	XmvSixRamWriteWord(CpsXmvSixWorkRam, a - XMV6_WORK_RAM_BASE, d);
}

static void CpsXmvMapMemory()
{
	// Instruction reads use the same device decode as data reads.
	SekMapHandler(5, XMV6_BASE, XMV6_BASE + XMV6_WINDOW_BYTES - 1, MAP_ROM);
	SekSetReadByteHandler(5, CpsXmvReadByte);
	SekSetReadWordHandler(5, CpsXmvReadWord);
	SekMapHandler(6, XMV6_WORK_RAM_BASE, XMV6_WORK_RAM_BASE + XMV6_WORK_RAM_BYTES - 1, MAP_RAM);
	SekSetReadByteHandler(6, CpsXmvRamReadByte);
	SekSetReadWordHandler(6, CpsXmvRamReadWord);
	SekSetWriteByteHandler(6, CpsXmvRamWriteByte);
	SekSetWriteWordHandler(6, CpsXmvRamWriteWord);
}

static INT32 CpsXmvObjectPalette(INT32 n, UINT16 x, UINT16 y, INT32 a, UINT32* pPalette)
{
	UINT32 nColor;
	if (!XmvPortraitObject(n, x, y, a, CpsXmvWide != 0, &nColor)) return 0;
	UINT16 nColors[16];
	if (!XmvPortraitColors(CpsRom, nCpsRomLen, n, nColor, nColors)) return -1;
	for (INT32 pen = 0; pen < 16; pen++) {
		UINT32 c = nColors[pen];
		pPalette[pen] = BurnHighCol(((c >> 8) & 15) * 17, ((c >> 4) & 15) * 17, (c & 15) * 17, 0);
	}
	return 1;
}

INT32 CpsXmvSixInit(INT32 bWide)
{
	Cps2BoardReset();
	CpsXmvSixWorkRam = (UINT8*)BurnMalloc(XMV6_WORK_RAM_BYTES);
	if (CpsXmvSixWorkRam == NULL) return 1;

	CpsXmvWide = bWide ? 1 : 0;
	CpsXmvSixCapabilities = XMV6_CAP_WORK_RAM | XMV6_CAP_PORTRAITS | XMV6_CAP_PORTRAITS_6 | XMV6_CAP_PORTRAITS_12 | XMV6_CAP_PORTRAITS_18;
	if (bWide) CpsXmvSixCapabilities |= XMV6_CAP_WIDE | XMV6_CAP_WIDE_SCROLL1;
	memset(CpsXmvSixButtons, 0, sizeof(CpsXmvSixButtons));
	CpsXmvSixReset();

	Cps2Board.pDisplayState = &CpsXmvDisplayState;
	Cps2Board.MapMemory = CpsXmvMapMemory;
	Cps2Board.PrepareInputs = CpsXmvSixSample;
	Cps2Board.Scroll1 = bWide ? CpsXmvScroll1Extension : NULL;
	Cps2Board.ObjectPalette = CpsXmvObjectPalette;
	CpsRunResetCallbackFunction = CpsXmvSixReset;
	CpsRunExitCallbackFunction = CpsXmvSixExit;
	CpsMemScanCallbackFunction = CpsXmvSixScan;
	return Cps2Init();
}

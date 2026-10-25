#ifndef CPS2_BOARD_H
#define CPS2_BOARD_H

// ROMs stored as conventional CPS2 sockets or as linear program/graphics/sample dumps.
enum { CPS2_ROM_SOCKETS, CPS2_ROM_LINEAR };

struct Cps2DisplayState {
	char* szPalette;
	char* szRegisters;
	char* szFrg;
	char* szObjects;
	char* szEffectivePalette;
	bool bRawPalette;
};

struct Cps2BoardConfig {
	UINT32 nProgramMapSize;       // 0 uses the loaded program size
	UINT32 nGfxSize;              // 0 uses the populated graphics sockets
	INT32 nRomLayout;
	UINT32 nScrollMask, nScrollBankAttribute, nScroll3Mask;
	UINT32 nObjectAddressMask, nObjectAliasBit;
	bool bObjectBankHandlers;    // Handlers decode the bank register without remapping RAM
	bool bParentBlend;           // Parent blend tables require matching tile numbers
	INT32 nZ80Clock, nZ80BankOffset;

	const Cps2DisplayState* pDisplayState;

	void (*MapMemory)();
	INT32 (*MemoryScan)(INT32, INT32*);
	INT32 (*PrepareInputs)();    // Returns the number of players using the stock input ports
	const UINT8* (*Scroll1)();
	// Negative skips the object, zero uses its normal palette, positive supplies 16 colors.
	INT32 (*ObjectPalette)(INT32, UINT16, UINT16, INT32, UINT32*);
	void (*SoundRender)(INT16*, UINT32);
	void (*SoundScan)(INT32, INT32*);
};

extern Cps2BoardConfig Cps2Board;
void Cps2BoardReset();

INT32 Cps2PlusInit(INT32 nSampleBits);
void Cps2MixConfig();
extern UINT8 AspectDIP;
INT32 CpsXmvSixInit(INT32 bWide);
extern UINT8 CpsXmvSixButtons[6][13];

#endif

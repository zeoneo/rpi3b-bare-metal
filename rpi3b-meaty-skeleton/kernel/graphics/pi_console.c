#include<graphics/pi_console.h>
#include <kernel/rpi-mailbox-interface.h>
#include <plibc/stdio.h>

#define BitFontHt 16
#define BitFontWth 8
typedef int32_t		BOOL;							// BOOL is defined to an int32_t ... yeah windows is weird -1 is often returned
typedef char		TCHAR;							// TCHAR is a char
typedef uint32_t	COLORREF;						// COLORREF is a uint32_t
typedef uintptr_t	HDC;							// HDC is really a pointer

static const uint32_t __attribute((aligned(4))) BitFont[1024] = {
		0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u,     // 0 .. 3

		0x00007E81u, 0xA58181BDu, 0x9981817Eu, 0x00000000u,	    // 4 .. 7

		0x00007EFFu, 0xDBFFFFC3u, 0xE7FFFF7Eu, 0x00000000u,		// 8 .. 11

		0x00000000u, 0x6CFEFEFEu, 0xFE7C3810u, 0x00000000u,		// 12 .. 15

		0x00000000u, 0x10387CFEu, 0x7C381000u, 0x00000000u,		// 16 .. 19

		0x00000018u, 0x3C3CE7E7u, 0xE718183Cu, 0x00000000u,		// 20 .. 23

		0x00000018u, 0x3C7EFFFFu, 0x7E18183Cu, 0x00000000u,		// 24 .. 27

		0x00000000u, 0x0000183Cu, 0x3C180000u, 0x00000000u,		// 28 .. 31

		0xFFFFFFFFu, 0xFFFFE7C3u, 0xC3E7FFFFu, 0xFFFFFFFFu,		// 32 .. 35

		0x00000000u, 0x003C6642u, 0x42663C00u, 0x00000000u,		// 36 .. 39

		0xFFFFFFFFu, 0xFFC399BDu, 0xBD99C3FFu, 0xFFFFFFFFu,		// 40 .. 43

		0x00001E0Eu, 0x1A3278CCu, 0xCCCCCC78u, 0x00000000u,		// 44 .. 47

		0x00003C66u, 0x6666663Cu, 0x187E1818u, 0x00000000u,		// 48 .. 51

		0x00003F33u, 0x3F303030u, 0x3070F0E0u, 0x00000000u,		// 52 .. 55

		0x00007F63u, 0x7F636363u, 0x6367E7E6u, 0xC0000000u,		// 56 .. 59

		0x00000018u, 0x18DB3CE7u, 0x3CDB1818u, 0x00000000u,		// 60 .. 63

		0x0080C0E0u, 0xF0F8FEF8u, 0xF0E0C080u, 0x00000000u,		// 64 .. 67

		0x0002060Eu, 0x1E3EFE3Eu, 0x1E0E0602u, 0x00000000u,		// 68 .. 71

		0x0000183Cu, 0x7E181818u, 0x7E3C1800u, 0x00000000u,		// 72 .. 75

		0x00006666u, 0x66666666u, 0x66006666u, 0x00000000u,		// 76 .. 79

		0x00007FDBu, 0xDBDB7B1Bu, 0x1B1B1B1Bu, 0x00000000u,		// 80 .. 83

		0x007CC660u, 0x386CC6C6u, 0x6C380CC6u, 0x7C000000u,		// 84 .. 87

		0x00000000u, 0x00000000u, 0xFEFEFEFEu, 0x00000000u,		// 88 .. 91

		0x0000183Cu, 0x7E181818u, 0x7E3C187Eu, 0x00000000u,		// 92 .. 95

		0x0000183Cu, 0x7E181818u, 0x18181818u, 0x00000000u,		// 96 .. 99

		0x00001818u, 0x18181818u, 0x187E3C18u, 0x00000000u,		// 100 .. 103

		0x00000000u, 0x00180CFEu, 0x0C180000u, 0x00000000u,		// 104 .. 107

		0x00000000u, 0x003060FEu, 0x60300000u, 0x00000000u,		// 108 .. 111

		0x00000000u, 0x0000C0C0u, 0xC0FE0000u, 0x00000000u,		// 112 .. 115

		0x00000000u, 0x00286CFEu, 0x6C280000u, 0x00000000u,		// 116 .. 119

		0x00000000u, 0x1038387Cu, 0x7CFEFE00u, 0x00000000u,		// 120 .. 123

		0x00000000u, 0xFEFE7C7Cu, 0x38381000u, 0x00000000u,		// 124 .. 127

		0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u,		// 128 .. 131

		0x0000183Cu, 0x3C3C1818u, 0x18001818u, 0x00000000u,		// 132 .. 135

		0x00666666u, 0x24000000u, 0x00000000u, 0x00000000u,		// 136 .. 139

		0x0000006Cu, 0x6CFE6C6Cu, 0x6CFE6C6Cu, 0x00000000u,		// 140 .. 143

		0x18187CC6u, 0xC2C07C06u, 0x0686C67Cu, 0x18180000u,		// 144 .. 147

		0x00000000u, 0xC2C60C18u, 0x3060C686u, 0x00000000u,		// 148 .. 151

		0x0000386Cu, 0x6C3876DCu, 0xCCCCCC76u, 0x00000000u,		// 152 .. 155

		0x00303030u, 0x60000000u, 0x00000000u, 0x00000000u,		// 156 .. 159

		0x00000C18u, 0x30303030u, 0x3030180Cu, 0x00000000u,		// 160 .. 163

		0x00003018u, 0x0C0C0C0Cu, 0x0C0C1830u, 0x00000000u,		// 164 .. 167

		0x00000000u, 0x00663CFFu, 0x3C660000u, 0x00000000u,		// 168 .. 171

		0x00000000u, 0x0018187Eu, 0x18180000u, 0x00000000u,		// 172 .. 175

		0x00000000u, 0x00000000u, 0x00181818u, 0x30000000u,		// 176 .. 179

		0x00000000u, 0x000000FEu, 0x00000000u, 0x00000000u,		// 180 .. 183

		0x00000000u, 0x00000000u, 0x00001818u, 0x00000000u,		// 184 .. 187

		0x00000000u, 0x02060C18u, 0x3060C080u, 0x00000000u,		// 188 .. 191

		0x0000386Cu, 0xC6C6D6D6u, 0xC6C66C38u, 0x00000000u,		// 192 .. 195

		0x00001838u, 0x78181818u, 0x1818187Eu, 0x00000000u,		// 196 .. 199

		0x00007CC6u, 0x060C1830u, 0x60C0C6FEu, 0x00000000u,		// 200 .. 203

		0x00007CC6u, 0x06063C06u, 0x0606C67Cu, 0x00000000u,		// 204 .. 207

		0x00000C1Cu, 0x3C6CCCFEu, 0x0C0C0C1Eu, 0x00000000u,		// 208 .. 211

		0x0000FEC0u, 0xC0C0FC06u, 0x0606C67Cu, 0x00000000u,		// 212 .. 215

		0x00003860u, 0xC0C0FCC6u, 0xC6C6C67Cu, 0x00000000u,		// 216 .. 219

		0x0000FEC6u, 0x06060C18u, 0x30303030u, 0x00000000u,		// 220 .. 223

		0x00007CC6u, 0xC6C67CC6u, 0xC6C6C67Cu, 0x00000000u,		// 224 .. 227

		0x00007CC6u, 0xC6C67E06u, 0x06060C78u, 0x00000000u,		// 228 .. 231

		0x00000000u, 0x18180000u, 0x00181800u, 0x00000000u,		// 232 .. 235

		0x00000000u, 0x18180000u, 0x00181830u, 0x00000000u,		// 236 .. 239

		0x00000006u, 0x0C183060u, 0x30180C06u, 0x00000000u,		// 240 .. 243

		0x00000000u, 0x007E0000u, 0x7E000000u, 0x00000000u,		// 244 .. 247

		0x00000060u, 0x30180C06u, 0x0C183060u, 0x00000000u,		// 248 .. 251

		0x00007CC6u, 0xC60C1818u, 0x18001818u, 0x00000000u,		// 252 .. 255

		0x0000007Cu, 0xC6C6DEDEu, 0xDEDCC07Cu, 0x00000000u,		// 256 .. 259

		0x00001038u, 0x6CC6C6FEu, 0xC6C6C6C6u, 0x00000000u,		// 260 .. 263

		0x0000FC66u, 0x66667C66u, 0x666666FCu, 0x00000000u,		// 264 .. 267

		0x00003C66u, 0xC2C0C0C0u, 0xC0C2663Cu, 0x00000000u,		// 268 .. 271

		0x0000F86Cu, 0x66666666u, 0x66666CF8u, 0x00000000u,		// 272 .. 275

		0x0000FE66u, 0x62687868u, 0x606266FEu, 0x00000000u,		// 276 .. 279

		0x0000FE66u, 0x62687868u, 0x606060F0u, 0x00000000u,		// 280 .. 283

		0x00003C66u, 0xC2C0C0DEu, 0xC6C6663Au, 0x00000000u,		// 284 .. 287

		0x0000C6C6u, 0xC6C6FEC6u, 0xC6C6C6C6u, 0x00000000u,		// 288 .. 291

		0x00003C18u, 0x18181818u, 0x1818183Cu, 0x00000000u,		// 292 .. 295

		0x00001E0Cu, 0x0C0C0C0Cu, 0xCCCCCC78u, 0x00000000u,		// 296 .. 299

		0x0000E666u, 0x666C7878u, 0x6C6666E6u, 0x00000000u,		// 300 .. 303

		0x0000F060u, 0x60606060u, 0x606266FEu, 0x00000000u,

		0x0000C6EEu, 0xFEFED6C6u, 0xC6C6C6C6u, 0x00000000u,

		0x0000C6E6u, 0xF6FEDECEu, 0xC6C6C6C6u, 0x00000000u,

		0x00007CC6u, 0xC6C6C6C6u, 0xC6C6C67Cu, 0x00000000u,

		0x0000FC66u, 0x66667C60u, 0x606060F0u, 0x00000000u,

		0x00007CC6u, 0xC6C6C6C6u, 0xC6D6DE7Cu, 0x0C0E0000u,

		0x0000FC66u, 0x66667C6Cu, 0x666666E6u, 0x00000000u,

		0x00007CC6u, 0xC660380Cu, 0x06C6C67Cu, 0x00000000u,

		0x00007E7Eu, 0x5A181818u, 0x1818183Cu, 0x00000000u,

		0x0000C6C6u, 0xC6C6C6C6u, 0xC6C6C67Cu, 0x00000000u,

		0x0000C6C6u, 0xC6C6C6C6u, 0xC66C3810u, 0x00000000u,

		0x0000C6C6u, 0xC6C6D6D6u, 0xD6FEEE6Cu, 0x00000000u,

		0x0000C6C6u, 0x6C7C3838u, 0x7C6CC6C6u, 0x00000000u,

		0x00006666u, 0x66663C18u, 0x1818183Cu, 0x00000000u,

		0x0000FEC6u, 0x860C1830u, 0x60C2C6FEu, 0x00000000u,

		0x00003C30u, 0x30303030u, 0x3030303Cu, 0x00000000u,

		0x00000080u, 0xC0E07038u, 0x1C0E0602u, 0x00000000u,

		0x00003C0Cu, 0x0C0C0C0Cu, 0x0C0C0C3Cu, 0x00000000u,

		0x10386CC6u, 0x00000000u, 0x00000000u, 0x00000000u,

		0x00000000u, 0x00000000u, 0x00000000u, 0x00FF0000u,

		0x30301800u, 0x00000000u, 0x00000000u, 0x00000000u,

		0x00000000u, 0x00780C7Cu, 0xCCCCCC76u, 0x00000000u,

		0x0000E060u, 0x60786C66u, 0x6666667Cu, 0x00000000u,

		0x00000000u, 0x007CC6C0u, 0xC0C0C67Cu, 0x00000000u,

		0x00001C0Cu, 0x0C3C6CCCu, 0xCCCCCC76u, 0x00000000u,

		0x00000000u, 0x007CC6FEu, 0xC0C0C67Cu, 0x00000000u,

		0x0000386Cu, 0x6460F060u, 0x606060F0u, 0x00000000u,

		0x00000000u, 0x0076CCCCu, 0xCCCCCC7Cu, 0x0CCC7800u,

		0x0000E060u, 0x606C7666u, 0x666666E6u, 0x00000000u,

		0x00001818u, 0x00381818u, 0x1818183Cu, 0x00000000u,

		0x00000606u, 0x000E0606u, 0x06060606u, 0x66663C00u,

		0x0000E060u, 0x60666C78u, 0x786C66E6u, 0x00000000u,

		0x00003818u, 0x18181818u, 0x1818183Cu, 0x00000000u,

		0x00000000u, 0x00ECFED6u, 0xD6D6D6C6u, 0x00000000u,

		0x00000000u, 0x00DC6666u, 0x66666666u, 0x00000000u,

		0x00000000u, 0x007CC6C6u, 0xC6C6C67Cu, 0x00000000u,

		0x00000000u, 0x00DC6666u, 0x6666667Cu, 0x6060F000u,

		0x00000000u, 0x0076CCCCu, 0xCCCCCC7Cu, 0x0C0C1E00u,

		0x00000000u, 0x00DC7666u, 0x606060F0u, 0x00000000u,

		0x00000000u, 0x007CC660u, 0x380CC67Cu, 0x00000000u,

		0x00001030u, 0x30FC3030u, 0x3030361Cu, 0x00000000u,

		0x00000000u, 0x00CCCCCCu, 0xCCCCCC76u, 0x00000000u,

		0x00000000u, 0x00666666u, 0x66663C18u, 0x00000000u,

		0x00000000u, 0x00C6C6D6u, 0xD6D6FE6Cu, 0x00000000u,

		0x00000000u, 0x00C66C38u, 0x38386CC6u, 0x00000000u,

		0x00000000u, 0x00C6C6C6u, 0xC6C6C67Eu, 0x060CF800u,

		0x00000000u, 0x00FECC18u, 0x3060C6FEu, 0x00000000u,

		0x00000E18u, 0x18187018u, 0x1818180Eu, 0x00000000u,

		0x00001818u, 0x18180018u, 0x18181818u, 0x00000000u,

		0x00007018u, 0x18180E18u, 0x18181870u, 0x00000000u,

		0x000076DCu, 0x00000000u, 0x00000000u, 0x00000000u,

		0x00000000u, 0x10386CC6u, 0xC6C6FE00u, 0x00000000u,

		0x00003C66u, 0xC2C0C0C0u, 0xC2663C0Cu, 0x067C0000u,

		0x0000CC00u, 0x00CCCCCCu, 0xCCCCCC76u, 0x00000000u,

		0x000C1830u, 0x007CC6FEu, 0xC0C0C67Cu, 0x00000000u,

		0x0010386Cu, 0x00780C7Cu, 0xCCCCCC76u, 0x00000000u,

		0x0000CC00u, 0x00780C7Cu, 0xCCCCCC76u, 0x00000000u,

		0x00603018u, 0x00780C7Cu, 0xCCCCCC76u, 0x00000000u,

		0x00386C38u, 0x00780C7Cu, 0xCCCCCC76u, 0x00000000u,

		0x00000000u, 0x3C666060u, 0x663C0C06u, 0x3C000000u,

		0x0010386Cu, 0x007CC6FEu, 0xC0C0C67Cu, 0x00000000u,

		0x0000C600u, 0x007CC6FEu, 0xC0C0C67Cu, 0x00000000u,

		0x00603018u, 0x007CC6FEu, 0xC0C0C67Cu, 0x00000000u,

		0x00006600u, 0x00381818u, 0x1818183Cu, 0x00000000u,

		0x00183C66u, 0x00381818u, 0x1818183Cu, 0x00000000u,

		0x00603018u, 0x00381818u, 0x1818183Cu, 0x00000000u,

		0x00C60010u, 0x386CC6C6u, 0xFEC6C6C6u, 0x00000000u,

		0x386C3800u, 0x386CC6C6u, 0xFEC6C6C6u, 0x00000000u,

		0x18306000u, 0xFE66607Cu, 0x606066FEu, 0x00000000u,

		0x00000000u, 0x00CC7636u, 0x7ED8D86Eu, 0x00000000u,

		0x00003E6Cu, 0xCCCCFECCu, 0xCCCCCCCEu, 0x00000000u,

		0x0010386Cu, 0x007CC6C6u, 0xC6C6C67Cu, 0x00000000u,

		0x0000C600u, 0x007CC6C6u, 0xC6C6C67Cu, 0x00000000u,

		0x00603018u, 0x007CC6C6u, 0xC6C6C67Cu, 0x00000000u,

		0x003078CCu, 0x00CCCCCCu, 0xCCCCCC76u, 0x00000000u,

		0x00603018u, 0x00CCCCCCu, 0xCCCCCC76u, 0x00000000u,

		0x0000C600u, 0x00C6C6C6u, 0xC6C6C67Eu, 0x060C7800u,

		0x00C6007Cu, 0xC6C6C6C6u, 0xC6C6C67Cu, 0x00000000u,

		0x00C600C6u, 0xC6C6C6C6u, 0xC6C6C67Cu, 0x00000000u,

		0x0018183Cu, 0x66606060u, 0x663C1818u, 0x00000000u,

		0x00386C64u, 0x60F06060u, 0x6060E6FCu, 0x00000000u,

		0x00006666u, 0x3C187E18u, 0x7E181818u, 0x00000000u,

		0x00F8CCCCu, 0xF8C4CCDEu, 0xCCCCCCC6u, 0x00000000u,

		0x000E1B18u, 0x18187E18u, 0x18181818u, 0xD8700000u,

		0x00183060u, 0x00780C7Cu, 0xCCCCCC76u, 0x00000000u,

		0x000C1830u, 0x00381818u, 0x1818183Cu, 0x00000000u,

		0x00183060u, 0x007CC6C6u, 0xC6C6C67Cu, 0x00000000u,

		0x00183060u, 0x00CCCCCCu, 0xCCCCCC76u, 0x00000000u,

		0x000076DCu, 0x00DC6666u, 0x66666666u, 0x00000000u,

		0x76DC00C6u, 0xE6F6FEDEu, 0xCEC6C6C6u, 0x00000000u,

		0x003C6C6Cu, 0x3E007E00u, 0x00000000u, 0x00000000u,

		0x00386C6Cu, 0x38007C00u, 0x00000000u, 0x00000000u,

		0x00003030u, 0x00303060u, 0xC0C6C67Cu, 0x00000000u,

		0x00000000u, 0x0000FEC0u, 0xC0C0C000u, 0x00000000u,

		0x00000000u, 0x0000FE06u, 0x06060600u, 0x00000000u,

		0x00C0C0C2u, 0xC6CC1830u, 0x60DC860Cu, 0x183E0000u,

		0x00C0C0C2u, 0xC6CC1830u, 0x66CE9E3Eu, 0x06060000u,

		0x00001818u, 0x00181818u, 0x3C3C3C18u, 0x00000000u,

		0x00000000u, 0x00366CD8u, 0x6C360000u, 0x00000000u,

		0x00000000u, 0x00D86C36u, 0x6CD80000u, 0x00000000u,

		0x11441144u, 0x11441144u, 0x11441144u, 0x11441144u,

		0x55AA55AAu, 0x55AA55AAu, 0x55AA55AAu, 0x55AA55AAu,

		0xDD77DD77u, 0xDD77DD77u, 0xDD77DD77u, 0xDD77DD77u,

		0x18181818u, 0x18181818u, 0x18181818u, 0x18181818u,

		0x18181818u, 0x181818F8u, 0x18181818u, 0x18181818u,

		0x18181818u, 0x18F818F8u, 0x18181818u, 0x18181818u,

		0x36363636u, 0x363636F6u, 0x36363636u, 0x36363636u,

		0x00000000u, 0x000000FEu, 0x36363636u, 0x36363636u,

		0x00000000u, 0x00F818F8u, 0x18181818u, 0x18181818u,

		0x36363636u, 0x36F606F6u, 0x36363636u, 0x36363636u,

		0x36363636u, 0x36363636u, 0x36363636u, 0x36363636u,

		0x00000000u, 0x00FE06F6u, 0x36363636u, 0x36363636u,

		0x36363636u, 0x36F606FEu, 0x00000000u, 0x00000000u,

		0x36363636u, 0x363636FEu, 0x00000000u, 0x00000000u,

		0x18181818u, 0x18F818F8u, 0x00000000u, 0x00000000u,

		0x00000000u, 0x000000F8u, 0x18181818u, 0x18181818u,

		0x18181818u, 0x1818181Fu, 0x00000000u, 0x00000000u,

		0x18181818u, 0x181818FFu, 0x00000000u, 0x00000000u,

		0x00000000u, 0x000000FFu, 0x18181818u, 0x18181818u,

		0x18181818u, 0x1818181Fu, 0x18181818u, 0x18181818u,

		0x00000000u, 0x000000FFu, 0x00000000u, 0x00000000u,

		0x18181818u, 0x181818FFu, 0x18181818u, 0x18181818u,

		0x18181818u, 0x181F181Fu, 0x18181818u, 0x18181818u,

		0x36363636u, 0x36363637u, 0x36363636u, 0x36363636u,

		0x36363636u, 0x3637303Fu, 0x00000000u, 0x00000000u,

		0x00000000u, 0x003F3037u, 0x36363636u, 0x36363636u,

		0x36363636u, 0x36F700FFu, 0x00000000u, 0x00000000u,

		0x00000000u, 0x00FF00F7u, 0x36363636u, 0x36363636u,

		0x36363636u, 0x36373037u, 0x36363636u, 0x36363636u,

		0x00000000u, 0x00FF00FFu, 0x00000000u, 0x00000000u,

		0x36363636u, 0x36F700F7u, 0x36363636u, 0x36363636u,

		0x18181818u, 0x18FF00FFu, 0x00000000u, 0x00000000u,

		0x36363636u, 0x363636FFu, 0x00000000u, 0x00000000u,

		0x00000000u, 0x00FF00FFu, 0x18181818u, 0x18181818u,

		0x00000000u, 0x000000FFu, 0x36363636u, 0x36363636u,

		0x36363636u, 0x3636363Fu, 0x00000000u, 0x00000000u,

		0x18181818u, 0x181F181Fu, 0x00000000u, 0x00000000u,

		0x00000000u, 0x001F181Fu, 0x18181818u, 0x18181818u,

		0x00000000u, 0x0000003Fu, 0x36363636u, 0x36363636u,

		0x36363636u, 0x363636FFu, 0x36363636u, 0x36363636u,

		0x18181818u, 0x18FF18FFu, 0x18181818u, 0x18181818u,

		0x18181818u, 0x181818F8u, 0x00000000u, 0x00000000u,

		0x00000000u, 0x0000001Fu, 0x18181818u, 0x18181818u,

		0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu,

		0x00000000u, 0x000000FFu, 0xFFFFFFFFu, 0xFFFFFFFFu,

		0xF0F0F0F0u, 0xF0F0F0F0u, 0xF0F0F0F0u, 0xF0F0F0F0u,

		0x0F0F0F0Fu, 0x0F0F0F0Fu, 0x0F0F0F0Fu, 0x0F0F0F0Fu,

		0xFFFFFFFFu, 0xFFFFFF00u, 0x00000000u, 0x00000000u,

		0x00000000u, 0x0076DCD8u, 0xD8D8DC76u, 0x00000000u,

		0x000078CCu, 0xCCCCD8CCu, 0xC6C6C6CCu, 0x00000000u,

		0x0000FEC6u, 0xC6C0C0C0u, 0xC0C0C0C0u, 0x00000000u,

		0x00000000u, 0xFE6C6C6Cu, 0x6C6C6C6Cu, 0x00000000u,

		0x000000FEu, 0xC6603018u, 0x3060C6FEu, 0x00000000u,

		0x00000000u, 0x007ED8D8u, 0xD8D8D870u, 0x00000000u,

		0x00000000u, 0x66666666u, 0x667C6060u, 0xC0000000u,

		0x00000000u, 0x76DC1818u, 0x18181818u, 0x00000000u,

		0x0000007Eu, 0x183C6666u, 0x663C187Eu, 0x00000000u,

		0x00000038u, 0x6CC6C6FEu, 0xC6C66C38u, 0x00000000u,

		0x0000386Cu, 0xC6C6C66Cu, 0x6C6C6CEEu, 0x00000000u,

		0x00001E30u, 0x180C3E66u, 0x6666663Cu, 0x00000000u,

		0x00000000u, 0x007EDBDBu, 0xDB7E0000u, 0x00000000u,

		0x00000003u, 0x067EDBDBu, 0xF37E60C0u, 0x00000000u,

		0x00001C30u, 0x60607C60u, 0x6060301Cu, 0x00000000u,

		0x0000007Cu, 0xC6C6C6C6u, 0xC6C6C6C6u, 0x00000000u,

		0x00000000u, 0xFE0000FEu, 0x0000FE00u, 0x00000000u,

		0x00000000u, 0x18187E18u, 0x180000FFu, 0x00000000u,

		0x00000030u, 0x180C060Cu, 0x1830007Eu, 0x00000000u,

		0x0000000Cu, 0x18306030u, 0x180C007Eu, 0x00000000u,

		0x00000E1Bu, 0x1B181818u, 0x18181818u, 0x18181818u,

		0x18181818u, 0x18181818u, 0xD8D8D870u, 0x00000000u,

		0x00000000u, 0x1818007Eu, 0x00181800u, 0x00000000u,

		0x00000000u, 0x0076DC00u, 0x76DC0000u, 0x00000000u,

		0x00386C6Cu, 0x38000000u, 0x00000000u, 0x00000000u,

		0x00000000u, 0x00000018u, 0x18000000u, 0x00000000u,

		0x00000000u, 0x00000000u, 0x18000000u, 0x00000000u,

		0x000F0C0Cu, 0x0C0C0CECu, 0x6C6C3C1Cu, 0x00000000u,

		0x00D86C6Cu, 0x6C6C6C00u, 0x00000000u, 0x00000000u,

		0x0070D830u, 0x60C8F800u, 0x00000000u, 0x00000000u,

		0x00000000u, 0x7C7C7C7Cu, 0x7C7C7C00u, 0x00000000u,

		0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u,
};

typedef struct __attribute__((__packed__, aligned(1))) tagRGB {
	uint8_t rgbBlue;								// Blue
	uint8_t rgbGreen;								// Green
	uint8_t rgbRed;									// Red
} RGB;

typedef struct __attribute__((__packed__, aligned(4))) tagRGBQUAD {
	union {
		struct {
			uint8_t rgbBlue;						// Blue
			uint8_t rgbGreen;						// Green
			uint8_t rgbRed;							// Red
			uint8_t rgbReserved;					// Reserved
		};
		COLORREF ref;								// Colour reference
	};
} RGBQUAD;

typedef struct __attribute__((__packed__, aligned(4))) tagRGBA {
	union {
		struct {
			union {
				struct __attribute__((__packed__, aligned(1))) {
					uint8_t rgbBlue;				// Blue
					uint8_t rgbGreen;				// Green
					uint8_t rgbRed;					// Red
				};
				RGB rgb;							// RGB union
			};
			uint8_t rgbAlpha;						// Alpha
		};
		COLORREF ref;								// Colour reference
	};
} RGBA;


typedef struct __attribute__((__packed__, aligned(1))) RGB565 {
	unsigned B : 5;
	unsigned G : 6;
	unsigned R : 5;
} RGB565;

typedef struct tagPOINT {
	int_fast32_t x;									// x co-ordinate
	int_fast32_t y;									// y co-ordinate
} POINT, *LPPOINT;									// Typedef define POINT and LPPOINT


typedef struct __attribute__((__packed__, aligned(4))) tagINTDC {
	uintptr_t fb;													// Frame buffer address
	uint32_t wth;													// Screen width (of frame buffer)
	uint32_t ht;													// Screen height (of frame buffer)
	uint32_t depth;													// Colour depth (of frame buffer)
																	/* Position control */
	POINT curPos;													// Current position
	POINT cursor;													// Current cursor position

																	/* Text colour control */
	RGBA TxtColor;													// Text colour to write
	RGBA BkColor;													// Background colour to write
	RGBA BrushColor;												// Brush colour to write

	void(*ClearArea) (struct tagINTDC* dc, uint_fast32_t x1, uint_fast32_t y1, uint_fast32_t x2, uint_fast32_t y2);
	// void(*VertLine) (struct tagINTDC* dc, uint_fast32_t cy, int_fast8_t dir);
	// void(*HorzLine) (struct tagINTDC* dc, uint_fast32_t cx, int_fast8_t dir);
	// void(*DiagLine) (struct tagINTDC* dc, uint_fast32_t dx, uint_fast32_t dy, int_fast8_t xdir, int_fast8_t ydir);
	void(*WriteChar) (struct tagINTDC* dc, uint8_t Ch);
	// void(*PutImage) (struct tagINTDC* dc, uint_fast32_t dx, uint_fast32_t dy, IMAGE_PTR imgSrc, bool BottomUp);
} INTDC;

INTDC __attribute__((aligned(4))) console = { 0 };

static void ClearArea16(INTDC* dc, uint_fast32_t x1, uint_fast32_t y1, uint_fast32_t x2, uint_fast32_t y2) {
	RGB565* __attribute__((__packed__, aligned(1))) video_wr_ptr = (RGB565*)(uintptr_t)(dc->fb + (y1 * dc->wth * 2) + (x1 * 2));
	RGB565 Bc;
	Bc.R = dc->BrushColor.rgbRed >> 3;
	Bc.G = dc->BrushColor.rgbGreen >> 2;
	Bc.B = dc->BrushColor.rgbBlue >> 3;
	for (uint_fast32_t y = 0; y < (y2 - y1); y++) {					// For each y line
		for (uint_fast32_t x = 0; x < (x2 - x1); x++) {				// For each x between x1 and x2
			video_wr_ptr[x] = Bc;									// Write the colour
		}
		video_wr_ptr += dc->wth;									// Offset to next line
	}
}

static void WriteChar16(INTDC* dc, uint8_t Ch) {
	RGB565* __attribute__((aligned(1))) video_wr_ptr = (RGB565*)(uintptr_t)(dc->fb + (dc->curPos.y * dc->wth * 2) + (dc->curPos.x * 2));
	RGB565 Fc, Bc;
	Fc.R = dc->TxtColor.rgbRed >> 3;
	Fc.G = dc->TxtColor.rgbGreen >> 2;
	Fc.B = dc->TxtColor.rgbBlue >> 3;								// Colour for text
	Bc.R = dc->BkColor.rgbRed >> 3;
	Bc.G = dc->BkColor.rgbGreen >> 2;
	Bc.B = dc->BkColor.rgbBlue >> 3;								// Colour for background
	for (uint_fast32_t y = 0; y < 4; y++) {
		uint32_t b = BitFont[(Ch * 4) + y];							// Fetch character bits
		for (uint_fast32_t i = 0; i < 32; i++) {					// For each bit
			RGB565 col = Bc;										// Preset background colour
			int xoffs = i % 8;										// X offset
			if ((b & 0x80000000) != 0) col = Fc;					// If bit set take text colour
			video_wr_ptr[xoffs] = col;								// Write pixel
			b <<= 1;												// Roll font bits left
			if (xoffs == 7) video_wr_ptr += dc->wth;				// If was bit 7 next line down
		}
	}
	dc->curPos.x += BitFontWth;										// Increment x position
}

void console_putchar(char ch) {
	switch (ch) {
	case '\r': {											// Carriage return character
		console.cursor.x = 0;								// Cursor back to line start
	}
			   break;
	case '\t': {											// Tab character character
		console.cursor.x += 5;								// Cursor increment to by 5
		console.cursor.x -= (console.cursor.x % 4);			// align it to 4
	}
			   break;
	case '\n': {											// New line character
		console.cursor.x = 0;								// Cursor back to line start
		console.cursor.y++;									// Increment cursor down a line
	}
			   break;
	default: {												// All other characters
		console.curPos.x = console.cursor.x * BitFontWth;
		console.curPos.y = console.cursor.y * BitFontHt;
		console.WriteChar(&console, ch);					// Write the character to graphics screen
		console.cursor.x++;									// Cursor.x forward one character
	}
			 break;
	}
}

void console_puts(char *str) {
    while(*str != '\0') {
        console_putchar(*str);
        str++;
    }
}



void get_console_width_height_depth(uint32_t *width, uint32_t *height, uint32_t *depth, uint32_t *pitch) {
    RPI_PropertyInit();
    RPI_PropertyAddTag(TAG_SET_VIRTUAL_OFFSET, 0, 0);
    RPI_PropertyAddTag(TAG_GET_PHYSICAL_SIZE);
    RPI_PropertyAddTag(TAG_GET_DEPTH);
    RPI_PropertyAddTag(TAG_GET_PITCH);
    RPI_PropertyProcess();

    rpi_mailbox_property_t *mp = RPI_PropertyGet(TAG_GET_PHYSICAL_SIZE);
    *width = (uint32_t)(mp->data.buffer_32[0]);
    *height = (uint32_t)(mp->data.buffer_32[1]);
 

    mp = RPI_PropertyGet(TAG_GET_DEPTH);
    *depth = mp->data.value_32;

    mp = RPI_PropertyGet(TAG_GET_PITCH);
    *pitch = mp->data.value_32;
    printf("\n width: %d height %d depth %d pitch:%d  \n", *width, *height, *depth, *pitch);
}

uint32_t get_console_frame_buffer(uint32_t width, uint32_t height, uint32_t depth) {
    if(width == 0 || height == 0 || depth == 0) {
        return 0;
    }

    RPI_PropertyInit();
    RPI_PropertyAddTag(TAG_SET_VIRTUAL_OFFSET, 0, 0);
    RPI_PropertyAddTag(TAG_SET_PHYSICAL_SIZE, width, height);
    RPI_PropertyAddTag(TAG_SET_VIRTUAL_SIZE, width, height);
    RPI_PropertyAddTag(TAG_SET_DEPTH, depth);

    RPI_PropertyAddTag(TAG_ALLOCATE_BUFFER, 16);

    RPI_PropertyProcess();
    rpi_mailbox_property_t *mp = RPI_PropertyGet(TAG_ALLOCATE_BUFFER);
    uint32_t frame_buffer_addr = (uint32_t)(mp->data.buffer_32[0]);
    uint32_t frame_buffer_size = (uint32_t)(mp->data.buffer_32[1]);

	console.TxtColor.ref = 0xFFFFFFFF;
	console.BkColor.ref = 0x00000000;
	console.BrushColor.ref = 0xFF00FF00;
	console.wth = width;
	console.ht = height;
	console.depth = depth;
    console.fb = frame_buffer_addr & 0x3FFFFFFF; //(~0xC0000000);

    console.ClearArea = ClearArea16;
    console.WriteChar = WriteChar16;

    printf("\n frame_buffer_addr: %x frame_buffer_size %d  \n", frame_buffer_addr, frame_buffer_size);
    return console.fb;
}
#pragma comment(linker,"\"/manifestdependency:type='win32' \
name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

#include <windows.h>
#include <MMSystem.h>
#include <fstream>
#include <cstdlib>
#include <string>
#include <sstream>
#include <iostream>
#include <vector>
#include <Commctrl.h>

#include "CUESDK/CUESDK.h"
#include "CUESDK/CUESDKGlobal.h"

#include "kiss_fft.h"

#include "colorbutton.h"
#include "combobox.h"
#include "updowncontrol.h"

#define M_PI_F       3.14159265358979323846f

// Output
#define BARS 16
//#define BAR_VOLUME_SCALE 0.5f
#define BAR_VOLUME_SCALE 0.1f
#define BAR_VOLUME_THRESHOLD 0.2f

// Sampling
#define SAMPLES_PER_BAR 16
//#define INPUT_DEVICE 1

#define INP_BUFFER_SIZE (BARS * SAMPLES_PER_BAR)
#define SAMPLE_RATE (30 * INP_BUFFER_SIZE)

// Window
#define WBARSIZE int(256.0f / BARS)
#define WWIDTH (BARS * WBARSIZE + 16)
#define WHEIGHT 128 + 16 + 172

// Config
static unsigned char g_ColorBackground [] = { 0, 0, 0 };
static unsigned char g_ColorLow [] = { 0, 64, 255 };
static unsigned char g_ColorHigh [] = { 255, 0, 0 };
//static unsigned char g_ColorLow[] = { 255, 255, 0 };
//static unsigned char g_ColorHigh[] = { 255, 0, 0 };
static float g_Volume = 1.0f;


HWND hwnd;
static HWAVEIN      hWaveIn;
static bool bTerminating;
static BOOL         bRecording;
static HWAVEOUT     hWaveOut;
static PBYTE        pBuffer1, pBuffer2;
static PWAVEHDR     pWaveHdr1, pWaveHdr2;
static WAVEFORMATEX waveform;

float bars[BARS];
float magnitude[INP_BUFFER_SIZE];
float phase[INP_BUFFER_SIZE];
float power[INP_BUFFER_SIZE];
kiss_fft_cfg cfg;
COLORREF ccref[16];

LRESULT CALLBACK WindowProcedure(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

#define IDC_BUTTON_BACKGROUND 100
#define IDC_BUTTON_LOW 101
#define IDC_BUTTON_HIGH 102
#define IDC_COMBOBOX_WAVEIN 103
#define IDC_UPDOWN_VOLUME 104

CorsairLedPositions *fullleds;

struct LedShortcut
{
	float x, y;
	CorsairLedId ledId;
};
LedShortcut *leds;
int g_NumLeds;
float kx0, ky0, kx1, ky1, kw, kh;

std::vector<LedShortcut> barLeds[BARS];
CorsairLedColor *ledColors;
int g_ColorDelta[3];

HWND waveInComboBox;

const char* toString(CorsairError error)
{
	switch (error) {
	case CE_Success:
		return "CE_Success";
	case CE_ServerNotFound:
		return "CE_ServerNotFound";
	case CE_NoControl:
		return "CE_NoControl";
	case CE_ProtocolHandshakeMissing:
		return "CE_ProtocolHandshakeMissing";
	case CE_IncompatibleProtocol:
		return "CE_IncompatibleProtocol";
	case CE_InvalidArguments:
		return "CE_InvalidArguments";
	default:
		return "unknown error";
	}
}

bool checkCorsairError()
{
	if (const auto error = CorsairGetLastError()) {
		std::stringstream str;
		str << "Corsair call failed: " << toString(error) << std::endl;
		OutputDebugStringA(str.str().c_str());
		return true;
	}
	return false;
}

void updateLEDDelta()
{
	for (int i = 0; i < 3; ++i)
	{
		g_ColorDelta[i] = g_ColorHigh[i] - g_ColorLow[i];
	}
}

bool initCorsair()
{
	CorsairPerformProtocolHandshake();
	if (checkCorsairError())
	{
		return 0;
	}

	fullleds = CorsairGetLedPositions();
	if (checkCorsairError())
	{
		return 0;
	}

	// get short leds
	g_NumLeds = fullleds->numberOfLed;
	leds = new LedShortcut[g_NumLeds];

	// get keyboard size
	kx0 = 9999;
	ky0 = 9999;
	for (int i = 0; i < g_NumLeds; ++i)
	{
		LedShortcut &led = leds[i];
		const CorsairLedPosition &cled = fullleds->pLedPosition[i];

		led.ledId = cled.ledId;
		led.x = float(cled.left + cled.width * 0.5f);
		led.y = float(cled.top + cled.height * 0.5f);

		kx0 = kx0 < led.x ? kx0 : led.x;
		kx1 = kx1 > led.x ? kx1 : led.x;
		ky0 = ky0 < led.y ? ky0 : led.y;
		ky1 = ky1 > led.y ? ky1 : led.y;
	}
	kw = kx1 - kx0;
	kh = ky1 - ky0;

	// assign bar leds
	float kbarWidth = kw / BARS;
	for (int i = 0; i < g_NumLeds; ++i)
	{
		LedShortcut &led = leds[i];
		int b = int((led.x - kx0) / (kw + 0.0001f) * BARS) % BARS;
		if (b >= BARS)
		{
			b = BARS - 1;
		}
		barLeds[b].push_back(led);
	}

	// LED color helper
	updateLEDDelta();
	return true;
}

void updateLedFFT()
{
	if (!ledColors)
	{
		ledColors = new CorsairLedColor[g_NumLeds];
	}

	int x = 0;
	for (int i = 0; i < BARS; ++i)
	{
		for (auto l : barLeds[i])
		{
			float frac = 1.0f - (l.y - ky0) / kh;
			float s = (bars[i] - BAR_VOLUME_THRESHOLD);
			s = s > 1.0f ? 1.0f : s;
			ledColors[x].ledId = l.ledId;

			if (frac <= s)
			{
				ledColors[x].r = g_ColorLow[0] + int(g_ColorDelta[0] * s); //int(64 + 191 * s);
				ledColors[x].g = g_ColorLow[1] + int(g_ColorDelta[1] * s); //int(128 - 128 * s);
				ledColors[x].b = g_ColorLow[2] + int(g_ColorDelta[2] * s); //int(255 - 255 * s);
			}
			else
			{
				ledColors[x].r = g_ColorBackground[0];
				ledColors[x].g = g_ColorBackground[1];
				ledColors[x].b = g_ColorBackground[2];
			}
			x++;
		}
	}

	CorsairSetLedsColors(g_NumLeds, ledColors);
	checkCorsairError();
}

void startRecord(int device)
{
	if (pWaveHdr1 == NULL)
		pWaveHdr1 = reinterpret_cast <PWAVEHDR> (malloc(sizeof(WAVEHDR)));
	if (pWaveHdr2 == NULL)
		pWaveHdr2 = reinterpret_cast <PWAVEHDR> (malloc(sizeof(WAVEHDR)));

	if (pBuffer1 == NULL)
		pBuffer1 = reinterpret_cast <PBYTE> (malloc(INP_BUFFER_SIZE));
	if (pBuffer2 == NULL)
		pBuffer2 = reinterpret_cast <PBYTE> (malloc(INP_BUFFER_SIZE));

	//if (!pBuffer1 || !pBuffer2)
	//{
	//	if (pBuffer1) free(pBuffer1);
	//	if (pBuffer2) free(pBuffer2);
	//	return;
	//}

	waveInReset(hWaveIn);

	// Open waveform audio for input
	waveform.wFormatTag = WAVE_FORMAT_PCM;
	waveform.nChannels = 1;
	waveform.nSamplesPerSec = SAMPLE_RATE;
	waveform.nAvgBytesPerSec = SAMPLE_RATE;
	waveform.nBlockAlign = 1;
	waveform.wBitsPerSample = 8;
	waveform.cbSize = 0;

	int res = waveInOpen(&hWaveIn, device, &waveform,
		(DWORD) hwnd, 0, CALLBACK_WINDOW);
	if (res)
	{
		std::stringstream error;
		error << "Opening device failed: ";
		error << res;
		error << "\n";
		OutputDebugStringA(error.str().c_str());
	}
	// Set up headers and prepare them

	pWaveHdr1->lpData = reinterpret_cast <CHAR*>(pBuffer1);
	pWaveHdr1->dwBufferLength = INP_BUFFER_SIZE;
	pWaveHdr1->dwBytesRecorded = 0;
	pWaveHdr1->dwUser = 0;
	pWaveHdr1->dwFlags = 0;
	pWaveHdr1->dwLoops = 1;
	pWaveHdr1->lpNext = NULL;
	pWaveHdr1->reserved = 0;

	waveInPrepareHeader(hWaveIn, pWaveHdr1, sizeof(WAVEHDR));

	pWaveHdr2->lpData = reinterpret_cast <CHAR*>(pBuffer2);
	pWaveHdr2->dwBufferLength = INP_BUFFER_SIZE;
	pWaveHdr2->dwBytesRecorded = 0;
	pWaveHdr2->dwUser = 0;
	pWaveHdr2->dwFlags = 0;
	pWaveHdr2->dwLoops = 1;
	pWaveHdr2->lpNext = NULL;
	pWaveHdr2->reserved = 0;

	waveInPrepareHeader(hWaveIn, pWaveHdr2, sizeof(WAVEHDR));
	bRecording = TRUE;
	bTerminating = false;

	//waveInAddBuffer (hWaveIn, (PWAVEHDR) pWaveHdr1, sizeof (WAVEHDR)) ;
}

wchar_t *stristr(const wchar_t *String, const wchar_t *Pattern)
{
	wchar_t *pptr, *sptr, *start;

	for (start = (wchar_t *)String; *start != NULL; ++start)
	{
		while (((*start!=NULL) && (toupper(*start) 
			!= toupper(*Pattern))))
		{
			++start;
		}

		if (NULL == *start)
			return NULL;

		pptr = (wchar_t *)Pattern;
		sptr = (wchar_t *)start;

		while (toupper(*sptr) == toupper(*pptr))
		{
			sptr++;
			pptr++;

			if (NULL == *pptr)
				return (start);
		}
	}

	return NULL;
}

int WINAPI WinMain(HINSTANCE hThisInstance,
	HINSTANCE hPrevInstance,
	LPSTR lpszArgument,
	int nCmdShow)
{
	MSG messages;            /* Here messages to the application are saved */
	WNDCLASSEX wincl;        /* Data structure for the windowclass */

	/* The Window structure */
	wincl.hInstance = hThisInstance;
	wincl.lpszClassName = L"HiddenClassRec";
	wincl.lpfnWndProc = WindowProcedure;      /* This function is called by windows */
	wincl.style = CS_DBLCLKS;                 /* Catch double-clicks */
	wincl.cbSize = sizeof(WNDCLASSEX);

	/* Use default icon and mouse-pointer */
	wincl.hIcon = LoadIcon(NULL, IDI_APPLICATION);
	wincl.hIconSm = LoadIcon(NULL, IDI_APPLICATION);
	wincl.hCursor = LoadCursor(NULL, IDC_ARROW);
	wincl.lpszMenuName = L"APP_MENU";                 /*menu */
	wincl.cbClsExtra = 0;                      /* No extra bytes after the window class */
	wincl.cbWndExtra = 0;                      /* structure or the window instance */
	/* Use Windows's default colour as the background of the window */
	wincl.hbrBackground = (HBRUSH) COLOR_BACKGROUND;

	cfg = kiss_fft_alloc(INP_BUFFER_SIZE, 0, 0, 0);

	/* Register the window class, and if it fails quit the program */
	if (!RegisterClassEx(&wincl))
		return 0;

	hwnd = CreateWindowEx(
		0,                   /* Extended possibilites for variation */
		L"HiddenClassRec",         /* Classname */
		L"Corsair FFT",       /* Title Text */
		WS_VISIBLE | WS_SYSMENU, //WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX, /* default window */
		CW_USEDEFAULT,       /* Windows decides the position */
		CW_USEDEFAULT,       /* where the window ends up on the screen */
		WWIDTH,                 /* The programs width */
		WHEIGHT,                 /* and height in pixels */
		HWND_DESKTOP,        /* The window is a child-window to desktop */
		NULL,                /*use class menu */
		hThisInstance,       /* Program Instance handler */
		NULL                 /* No Window Creation data */
		);

	waveInComboBox = CreateComboBox(hwnd, IDC_COMBOBOX_WAVEIN, L"Input", 15, 140);
	CreateUpDownControl(hwnd, IDC_UPDOWN_VOLUME, L"Volume", 30, 15, 164);
	CreateColorButton(hwnd, IDC_BUTTON_HIGH, L"High color", 15, 188);
	CreateColorButton(hwnd, IDC_BUTTON_LOW, L"Low color", 15, 212);
	CreateColorButton(hwnd, IDC_BUTTON_BACKGROUND, L"Background", 15, 236);

	int bestDevice = -1;
	UINT devs = waveInGetNumDevs();
	for (UINT dev = 0; dev < devs; dev++)
	{
		WAVEINCAPS caps = {};
		MMRESULT mmr = waveInGetDevCaps(dev, &caps, sizeof(caps));
		AddItemToComboBox(waveInComboBox, caps.szPname);
		if ((stristr(caps.szPname, L"wave") || stristr(caps.szPname, L"mix")) && bestDevice < 0)
		{
			bestDevice = dev;
		}
	}

	if (bestDevice >= 0)
	{
		SelectComboBoxItem(waveInComboBox, bestDevice);
	}

	ShowWindow(hwnd, 1);
	UpdateWindow(hwnd);

	if (!initCorsair())
	{
		return 0;
	}

	if (bestDevice >= 0)
	{
		startRecord(bestDevice);
	}

	while (GetMessage(&messages, NULL, 0, 0))
	{
		/* Translate virtual-key messages into character messages */
		TranslateMessage(&messages);
		/* Send message to WindowProcedure */
		DispatchMessage(&messages);
	}

	return 0;
}

void stopRecord()
{
	bTerminating = true;

	waveInStop(hWaveIn);
	waveInReset(hWaveIn);
	MMRESULT res = waveInClose(hWaveIn);

	std::stringstream ss;
	ss << "Close result: ";
	ss << res;
	ss << "\n";
	OutputDebugStringA(ss.str().c_str());
}

void CleanUp()
{
	//waveInStop(hWaveIn);
	//waveInClose(hWaveIn);

	free(pBuffer1);
	free(pBuffer2);

	delete [] leds;
	g_NumLeds = 0;

	delete [] ledColors;
	kiss_fft_free(cfg);
}

LRESULT CALLBACK WindowProcedure(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	HDC hDC;
	int ad = 0;
	switch (message)
	{
	case MM_WIM_OPEN:
		// Add the buffers
		waveInAddBuffer(hWaveIn, pWaveHdr1, sizeof(WAVEHDR));
		waveInAddBuffer(hWaveIn, pWaveHdr2, sizeof(WAVEHDR));
		waveInStart(hWaveIn);
		return TRUE;

	case MM_WIM_DATA:
	{
		LPSTR data = ((PWAVEHDR) lParam)->lpData;
		int length = ((PWAVEHDR) lParam)->dwBytesRecorded;

		if (length == INP_BUFFER_SIZE)
		{
			kiss_fft_cpx in[INP_BUFFER_SIZE];
			kiss_fft_cpx out[INP_BUFFER_SIZE];
			for (int i = 0; i < length; ++i)
			{
				in[i].r = ((unsigned char) data[i]) / 256.0f;
				in[i].i = 1.0f / in[i].r;
			}

			float avg_power = 0.0f;
			kiss_fft(cfg, in, out);

			// Clear bars
			for (int i = 0; i < BARS; ++i)
			{
				bars[i] = 0.0f;
			}

			for (int i = 1; i <= length / 2; ++i)
			{
				float p = out[i].r * out[i].r + out[i].i * out[i].i;

				// Hamming window
				//p *= 0.54f - 0.46f * cos(2 * M_PI_F * i / (length / 2 - 1));

				float m = 2.0f * sqrt(p);

				// Add to bar
				int b = (i - 1) % BARS;
				//bars[b] += m;
				bars[b] = bars[b] > m ? bars[b] : m;
			}

			// Normalize bars
			for (int i = 0; i < BARS; ++i)
			{
				bars[i] *= BAR_VOLUME_SCALE * g_Volume; // / SAMPLES_PER_BAR;
			}
		}

		// Update LEDs
		updateLedFFT();

		// Refresh UI
		InvalidateRect(hwnd, 0, FALSE);

		// Send out a new buffer
		if (!bTerminating)
		{
			waveInAddBuffer(hWaveIn, (PWAVEHDR) lParam, sizeof(WAVEHDR));
		}
		else
		{
			waveInStop(hWaveIn);
			waveInClose(hWaveIn);
		}
		return TRUE;
	}

	case MM_WIM_CLOSE:
		if (bRecording == TRUE)
		{
			waveInUnprepareHeader(hWaveIn, pWaveHdr1, sizeof(WAVEHDR));
			waveInUnprepareHeader(hWaveIn, pWaveHdr2, sizeof(WAVEHDR));
			bRecording = FALSE;
		}

		//CleanUp();
		//PostQuitMessage(0);
		return TRUE;

	case WM_DESTROY:
		stopRecord();

		if (bRecording == TRUE)
		{
			waveInUnprepareHeader(hWaveIn, pWaveHdr1, sizeof(WAVEHDR));
			waveInUnprepareHeader(hWaveIn, pWaveHdr2, sizeof(WAVEHDR));
			bRecording = FALSE;
		}

		CleanUp();
		PostQuitMessage(0);
		break;

	case WM_PAINT:
	{
		PAINTSTRUCT     ps;
		hDC = BeginPaint(hwnd, &ps);

		if (hDC)
		{
			RECT rc;
			rc.top = 0;
			rc.left = 0;
			rc.bottom = 128;
			rc.right = WWIDTH;
			HBRUSH brush = CreateSolidBrush(RGB(g_ColorBackground[0], g_ColorBackground[1], g_ColorBackground[2]));
			FillRect(hDC, &rc, brush);
			DeleteObject(brush);

			for (int i = 0; i < BARS; ++i)
			{
				float f = bars[i];
				if (f > 1.0f)
				{
					f = 1.0f;
				}

				brush = CreateSolidBrush(RGB(
					g_ColorLow[0] + g_ColorDelta[0] * f,
					g_ColorLow[1] + g_ColorDelta[1] * f,
					g_ColorLow[2] + g_ColorDelta[2] * f
					));

				int p = i * WBARSIZE;
				rc.left = p;
				rc.right = p + WBARSIZE;
				rc.top = long(128 - bars[i] * 128.0f);
				FillRect(hDC, &rc, brush);

				DeleteObject(brush);
			}
		}
		DeleteDC(hDC);
		EndPaint(hwnd, &ps);
		//ValidateRect(hwnd, 0);
	}
	break;

	case WM_NOTIFY:
	{
		LPNMHDR some_item = (LPNMHDR)lParam;

		if (some_item->idFrom == IDC_BUTTON_BACKGROUND && some_item->code == NM_CUSTOMDRAW)
		{
			return DrawColorButton(some_item, g_ColorBackground);
		}
		else if (some_item->idFrom == IDC_BUTTON_LOW && some_item->code == NM_CUSTOMDRAW)
		{
			return DrawColorButton(some_item, g_ColorLow);
		}
		else if (some_item->idFrom == IDC_BUTTON_HIGH && some_item->code == NM_CUSTOMDRAW)
		{
			return DrawColorButton(some_item, g_ColorHigh);
		}
	}
	break;

	case WM_COMMAND:
	{
		switch (HIWORD(wParam))
		{
			case BN_CLICKED:
			{
				switch (LOWORD(wParam))
				{
					case IDC_BUTTON_BACKGROUND:
					case IDC_BUTTON_LOW:
					case IDC_BUTTON_HIGH:
					{
						unsigned char *colors = g_ColorBackground;
						switch (LOWORD(wParam))
						{
						case IDC_BUTTON_LOW:
							colors = g_ColorLow;
							break;

						case IDC_BUTTON_HIGH:
							colors = g_ColorHigh;
							break;
						}
						CHOOSECOLOR color;
						color.lStructSize = sizeof(CHOOSECOLOR);
						color.hwndOwner = hwnd;
						color.hInstance = NULL;
						color.rgbResult = RGB(colors[0], colors[1], colors[2]);
						color.lpCustColors = ccref;
						color.Flags = CC_RGBINIT | CC_FULLOPEN;
						color.lCustData = 0;
						color.lpfnHook = NULL;
						color.lpTemplateName = NULL;
						if (ChooseColor(&color))
						{
							colors[0] = GetRValue(color.rgbResult);
							colors[1] = GetGValue(color.rgbResult);
							colors[2] = GetBValue(color.rgbResult);
							updateLEDDelta();
							InvalidateRect(hwnd, 0, false);
						}
					}
					break;
				}
			}
			break;

			case CBN_SELCHANGE:
			{
				int item = SendMessage((HWND) lParam, (UINT) CB_GETCURSEL, 
					(WPARAM) 0, (LPARAM) 0);

				stopRecord();
				startRecord(item);
			}
			break;

			case EN_CHANGE:
			{
				wchar_t text[16];
				((WORD*) text)[0] = 16;
				SendMessage((HWND) lParam, (UINT) EM_GETLINE,
					(WPARAM) 0, (LPARAM) text);
				int value = wcstol(text, NULL, 10);
				g_Volume = value / 30.0f;
			}
			break;
		}
	}
	break;

	case WM_CTLCOLORSTATIC:
	{
		HDC hdcStatic = (HDC) wParam;
		SetTextColor(hdcStatic, RGB(0,0,0));
		SetBkMode (hdcStatic, TRANSPARENT);
		return (LRESULT)GetStockObject(NULL_BRUSH);
	}

	default:
		return DefWindowProc(hwnd, message, wParam, lParam);
	}
	return 0;
}


#include <windows.h>
#include <MMSystem.h>
#include <fstream>
#include <cstdlib>
#include <string>
#include <sstream>
#include <iostream>
#include <vector>

#include "CUESDK/CUESDK.h"
#include "CUESDK/CUESDKGlobal.h"

#include "kiss_fft.h"

#define BARS 16
#define SAMPLES_PER_BAR 16
#define INP_BUFFER_SIZE (BARS * SAMPLES_PER_BAR)
#define SAMPLE_RATE (20 * INP_BUFFER_SIZE)

HWND hwnd;
static HWAVEIN      hWaveIn;
static BOOL         bRecording, bPlaying, bEnding;
static DWORD        dwDataLength, dwRepetitions = 1;
static HWAVEOUT     hWaveOut;
static PBYTE        pBuffer1, pBuffer2, pSaveBuffer, pNewBuffer;
static PWAVEHDR     pWaveHdr1, pWaveHdr2;
static TCHAR        szOpenError [] = TEXT("Error opening waveform audio!");
static TCHAR        szMemError [] = TEXT("Error allocating memory!");
static WAVEFORMATEX waveform;


float bars[BARS];
float magnitude[INP_BUFFER_SIZE];
float phase[INP_BUFFER_SIZE];
float power[INP_BUFFER_SIZE];
float masterVolume;

CorsairLedPositions *fullleds;

struct LedShortcut
{
	float x, y;
	CorsairLedId ledId;
};
LedShortcut *leds;
int g_NumLeds;
float kx0, ky0, kx1, ky1, kw, kh;

LRESULT CALLBACK WindowProcedure(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

//void CALLBACK waveInProc(
//   HWAVEIN   hwi,
//   UINT      uMsg,
//   DWORD_PTR dwInstance,
//   DWORD_PTR dwParam1,
//   DWORD_PTR dwParam2)
//{
//	dwParam2 = dwParam1;
//	return;
//}

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
		//getchar();
		return true;
	}
	return false;
}

std::vector<LedShortcut> barLeds[BARS];
CorsairLedColor *ledColors;

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
		int b = int((led.x - kx0) / (kw + 0.0001f) * 16.0f) % BARS;
		if (b >= BARS)
		{
			b = BARS - 1;
		}
		barLeds[b].push_back(led);
	}

	return true;
}

void updateLedFFT()
{
	if (!ledColors)
	{
		ledColors = new CorsairLedColor[g_NumLeds];
	}

	const float flScale = 64.0f;
	int x = 0;
	for (int i = 0; i < 16; ++i)
	{
		for (auto l : barLeds[i])
		{
			float frac = 1.0f - (l.y - ky0) / kh;
			float s = (bars[i] - 3.0f) / 48.0f;
			s = s > 1.0f ? 1.0f : s;
			ledColors[x].ledId = l.ledId;

			if (frac <= s)
			{
				ledColors[x].r = int(64 + 191 * s);
				ledColors[x].g = int(128 - 128 * s);
				ledColors[x].b = int(255 - 255 * s);
			}
			else
			{
				ledColors[x].r = 0;
				ledColors[x].g = 0;
				ledColors[x].b = 0;
			}
			x++;
		}
	}

	CorsairSetLedsColors(g_NumLeds, ledColors);
	checkCorsairError();
}

void startRecord()
{
	pWaveHdr1 = reinterpret_cast <PWAVEHDR> (malloc(sizeof(WAVEHDR)));
	pWaveHdr2 = reinterpret_cast <PWAVEHDR> (malloc(sizeof(WAVEHDR)));
	// Allocate memory for save buffer
	pSaveBuffer = reinterpret_cast <PBYTE> (malloc(1));

	waveInReset(hWaveIn);
	pBuffer1 = reinterpret_cast <PBYTE> (malloc(INP_BUFFER_SIZE));
	pBuffer2 = reinterpret_cast <PBYTE> (malloc(INP_BUFFER_SIZE));

	if (!pBuffer1 || !pBuffer2)
	{
		if (pBuffer1) free(pBuffer1);
		if (pBuffer2) free(pBuffer2);
		return;
	}
	// Open waveform audio for input

	waveform.wFormatTag = WAVE_FORMAT_PCM;
	waveform.nChannels = 1;
	waveform.nSamplesPerSec = SAMPLE_RATE;
	waveform.nAvgBytesPerSec = SAMPLE_RATE;
	waveform.nBlockAlign = 1;
	waveform.wBitsPerSample = 8;
	waveform.cbSize = 0;

	int res = waveInOpen(&hWaveIn, 1, &waveform,
		(DWORD) hwnd, 0, CALLBACK_WINDOW);
	if (res)
	{
		//free(pBuffer1);
		//free(pBuffer2);

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

	//waveInAddBuffer (hWaveIn, (PWAVEHDR) pWaveHdr1, sizeof (WAVEHDR)) ;
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
		300,                 /* The programs width */
		300,                 /* and height in pixels */
		HWND_DESKTOP,        /* The window is a child-window to desktop */
		NULL,                /*use class menu */
		hThisInstance,       /* Program Instance handler */
		NULL                 /* No Window Creation data */
		);

	ShowWindow(hwnd, 1);

	if (!initCorsair())
	{
		return 0;
	}

	startRecord();

	while (GetMessage(&messages, NULL, 0, 0))
	{
		/* Translate virtual-key messages into character messages */
		TranslateMessage(&messages);
		/* Send message to WindowProcedure */
		DispatchMessage(&messages);
	}

	return 0;
}

bool bTerminating;
void Shutdown()
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
	//MMSYSERR_INVALHANDLE

	//if (bRecording == TRUE)
	//{
	//	waveInUnprepareHeader(hWaveIn, pWaveHdr1, sizeof(WAVEHDR));
	//	waveInUnprepareHeader(hWaveIn, pWaveHdr2, sizeof(WAVEHDR));

	//	free(pBuffer1);
	//	free(pBuffer2);
	//}

}

void CleanUp()
{
	waveInStop(hWaveIn);
	waveInClose(hWaveIn);

	delete [] leds;
	g_NumLeds = 0;

	delete [] ledColors;
}

LRESULT CALLBACK WindowProcedure(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	HDC hDC;
	int ad = 0;
	switch (message)
	{
	case MM_WIM_OPEN:
		//pSaveBuffer = reinterpret_cast <PBYTE>(realloc(pSaveBuffer, 1));

		// Add the buffers
		waveInAddBuffer(hWaveIn, pWaveHdr1, sizeof(WAVEHDR));
		waveInAddBuffer(hWaveIn, pWaveHdr2, sizeof(WAVEHDR));

		bEnding = FALSE;
		dwDataLength = 0;
		waveInStart(hWaveIn);
		return TRUE;

	case MM_WIM_DATA:
	{
		//pNewBuffer = reinterpret_cast <PBYTE> (realloc (pSaveBuffer, dwDataLength +
		//                                       ((PWAVEHDR) lParam)->dwBytesRecorded)) ;

		//if (pNewBuffer == NULL)
		//{
		//    waveInClose (hWaveIn) ;
		//    return TRUE;
		//}

		//pSaveBuffer = pNewBuffer ;
		//CopyMemory (pSaveBuffer + dwDataLength, ((PWAVEHDR) lParam)->lpData,
		//            ((PWAVEHDR) lParam)->dwBytesRecorded) ;

		//dwDataLength += ((PWAVEHDR) lParam)->dwBytesRecorded ;

		if (bEnding)
		{
			waveInClose(hWaveIn);
			return TRUE;
		}

		LPSTR data = ((PWAVEHDR) lParam)->lpData;
		int length = ((PWAVEHDR) lParam)->dwBytesRecorded;

		if (length == INP_BUFFER_SIZE)
		{
			kiss_fft_cpx in[INP_BUFFER_SIZE];
			kiss_fft_cpx out[INP_BUFFER_SIZE];
			for (int i = 0; i < length; ++i)
			{
				//left[i] = ((unsigned char) data[i]) / 256.0f;
				in[i].r = ((unsigned char) data[i]) / 256.0f;
				in[i].i = 1.0f / in[i].r;
				//if (left[i] < 0.4f || left[i] > 0.6f){
				//	std::stringstream ss;
				//	ss << left[i];
				//	ss << "\n";
				//	OutputDebugStringA(ss.str().c_str());
				//}
			}

			float avg_power = 0.0f;
			//fft		myfft;
			//myfft.powerSpectrum(0, (int) INP_BUFFER_SIZE / 2, left, INP_BUFFER_SIZE, &magnitude[0], &phase[0], &power[0], &avg_power);

			kiss_fft_cfg cfg = kiss_fft_alloc(INP_BUFFER_SIZE, 0, 0, 0);

			kiss_fft(cfg, in, out);

			kiss_fft_free(cfg);

			// Clear bars
			for (int i = 0; i < BARS; ++i)
			{
				bars[i] = 0.0f;
			}

			for (int i = 1; i <= length / 2; ++i)
			{
				float p = out[i].r * out[i].r + out[i].i * out[i].i;
				float m = 2.0f * sqrt(p);

				// Add to bar
				int b = (i - 1) % 16;

				bars[b] += m;
				//if (m > 0.1f)
				//{
				//	std::stringstream ss;
				//	ss << m;
				//	ss << "\n";
				//	OutputDebugStringA(ss.str().c_str());
				//}
			}

			//int j = 10;
			//for (int j = 1; j < INP_BUFFER_SIZE / 2 - 1; j++) {
			//freq[index][j] = magnitude[j];
			//float m = magnitude[j];

			//if (m > 0.1f)
			//{
			//	std::stringstream ss;
			//	ss << m;
			//	ss << "\n";
			//	OutputDebugStringA(ss.str().c_str());
			//}
			//}
		}

		//for (int i = 0; i < length; ++i)
		//{
		//	char v = data[i];
		//	float val = v / 128.0f;

		//	std::stringstream str;
		//	str << val;
		//	str << "\n";
		//	OutputDebugStringA(str.str().c_str());
		//}

		//float fl = avg / float(length);
		//std::stringstream str;
		//str << fl;
		//str << "\n";
		//OutputDebugStringA(str.str().c_str());

		updateLedFFT();
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
		//OutputDebugStringA("Data\n");
		return TRUE;
	}

	case MM_WIM_CLOSE:
		if (bRecording == TRUE)
		{
			waveInUnprepareHeader(hWaveIn, pWaveHdr1, sizeof(WAVEHDR));
			waveInUnprepareHeader(hWaveIn, pWaveHdr2, sizeof(WAVEHDR));

			free(pBuffer1);
			free(pBuffer2);
		}

		bRecording = FALSE;

		//if (bTerminating)
		//SendMessage(hwnd, WM_SYSCOMMAND, SC_CLOSE, 0L);

		CleanUp();
		PostQuitMessage(0);
		return TRUE;

	case WM_DESTROY:
		Shutdown();

		if (bRecording == TRUE)
		{
			waveInUnprepareHeader(hWaveIn, pWaveHdr1, sizeof(WAVEHDR));
			waveInUnprepareHeader(hWaveIn, pWaveHdr2, sizeof(WAVEHDR));

			free(pBuffer1);
			free(pBuffer2);
		}

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
			rc.right = INP_BUFFER_SIZE;
			FillRect(hDC, &rc, (HBRUSH) (COLOR_MENUTEXT));

			for (int i = 0; i < BARS; ++i)
			{
				int p = i * SAMPLES_PER_BAR;
				rc.left = p;
				rc.right = p + SAMPLES_PER_BAR;
				rc.top = long(128 - bars[i]);
				FillRect(hDC, &rc, (HBRUSH) (COLOR_ACTIVEBORDER));

				//std::stringstream ss;
				//ss << i;
				//ss << " - ";
				//ss << left[i];
				//ss << "\n";
				//OutputDebugStringA(ss.str().c_str());
			}
		}
		//DeleteObject(hPen);
		DeleteDC(hDC);
		EndPaint(hwnd, &ps);
	}
	break;

	default:                      /* for messages that we don't deal with */
		return DefWindowProc(hwnd, message, wParam, lParam);
	}
	return 0;
}
#include "something.h"
#include <stdio.h>
#include <stdlib.h>
#include <Windows.h>
#include "portaudio.h"
#include "pa_ringbuffer.h"
#include "pa_util.h"
#include <process.h>
#include <d2d1.h>
#pragma comment(lib,"d2d1.lib")
#include <Uxtheme.h>
#include <CommCtrl.h>


//Assembled by Jacob Lilly
//Makes use of the PortAudio library

#define PA_SAMPLE_TYPE paInt16
typedef short SAMPLE;
#define PAUSED 1
#define PLAYINGINITIAL 2
#define STOPPED 3
#define RESUMED 4

#define ITSMPEG 1
#define ITSWAVE 2



#pragma comment(linker,"\"/manifestdependency:type='win32' \
name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"") 

//#pragma comment(linker, "\"/manifestdependency:type='win32' \name='Microsoft.Windows.Common-Controls' version='6.0.19041.1110' \processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
//C:\Windows\WinSxS\x86_microsoft.windows.common-controls_6595b64144ccf1df_5.82.19041.4355_none_c0dc01d438beab35
//this is kinda computer specific, unsure if this will work for everyone...

//https://www.portaudio.com/docs/v19-doxydocs/api_overview.html

bool stop = false;
bool pause = false;
bool resume = false;
int state = STOPPED;
int PersistentState = STOPPED;

//drawing..
ID2D1Factory* pD2DFactory = NULL;
ID2D1HwndRenderTarget* pRenderTarget = NULL;
ID2D1SolidColorBrush* pBrush = NULL;

//images
HBITMAP hBmp = (HBITMAP)LoadImageW(NULL, L"Background.bmp", IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE);

typedef struct
{
	unsigned frameIndex;
	int threadSyncFlag;
	SAMPLE* ringBufferData;
	PaUtilRingBuffer ringBuffer;
	FILE* file;
	void* threadHandle;
}paData;
/*
typedef struct
{
	paData* data1;
	paData* data2;
}doublePaData; //agony
*/
typedef struct MyData {
	bool val;
} MYDATA, *PMYDATA;

typedef struct
{
	wave* header;
	FILE* file;
}waveFile;

static ring_buffer_size_t rbs_min(ring_buffer_size_t a, ring_buffer_size_t b)
{
	return (a < b) ? a : b;
}

int callbacker(const void* input, void* output, unsigned long framesPerBuffer, const PaStreamCallbackTimeInfo* timeinfo, PaStreamCallbackFlags statusFlags, void* userD)
{
	if (pause) { 
		
		SAMPLE* wptr = (SAMPLE*)output;
		memset(wptr, 0, framesPerBuffer * sizeof(SAMPLE) * 2); //fill output with zeroes, thank you random person on some website for recomending this
		return paContinue; }
	else
	{
		paData* data = (paData*)userD; //pointer to ring buffer, from which to pull
		ring_buffer_size_t elementsToPlay = PaUtil_GetRingBufferReadAvailable(&data->ringBuffer);
		ring_buffer_size_t elementsToRead = rbs_min(elementsToPlay, (ring_buffer_size_t)(framesPerBuffer * 2));
		SAMPLE* wptr = (SAMPLE*)output; //what the frick is this

		data->frameIndex += PaUtil_ReadRingBuffer(&data->ringBuffer, wptr, elementsToRead); //this function calls memcpy and puts the ring buffer data in the location at wptr, which is something???


		return data->threadSyncFlag ? paComplete : paContinue;

	}

	(void)input; // Prevent unused variable warnings. 
	(void)timeinfo;
	(void)statusFlags;
	//(void)userD;

}


static unsigned NextPowerOf2(unsigned val) //from portaudio
{
	val--;
	val = (val >> 1) | val;
	val = (val >> 2) | val;
	val = (val >> 4) | val;
	val = (val >> 8) | val;
	val = (val >> 16) | val;
	return ++val;
}

static int threadFunctionReadFromRawFile(void* ptr)
{
	paData* pData = (paData*)ptr;

	while (1)
	{

		ring_buffer_size_t elementsInBuffer = PaUtil_GetRingBufferWriteAvailable(&pData->ringBuffer); //watch the file object... (this line means: There are THIS many elements in the buffer that we can write to right now)

			if (elementsInBuffer >= pData->ringBuffer.bufferSize / 4) //if that previous number is greater than or equal to the bufferSize / 4, then...
			{
				void* ptr[2] = { 0 }; //array of locations within the ring buffer where things can be written
				ring_buffer_size_t sizes[2] = { 0 }; //the size of those locations

				/* By using PaUtil_GetRingBufferWriteRegions, we can write directly into the ring buffer */
				PaUtil_GetRingBufferWriteRegions(&pData->ringBuffer, elementsInBuffer, ptr + 0, sizes + 0, ptr + 1, sizes + 1);

				if (!feof(pData->file))
				{
					ring_buffer_size_t itemsReadFromFile = 0;
					int i;
					for (i = 0; i < 2 && ptr[i] != NULL; ++i)
					{

							itemsReadFromFile += (ring_buffer_size_t)fread(ptr[i], pData->ringBuffer.elementSizeBytes, sizes[i], pData->file); //this is the line that reads the file into the ring buffer

					} //if we want to pause, my theory is that we fill ptr[i] in the above line with 0s, should we use memcpy?
					PaUtil_AdvanceRingBufferWriteIndex(&pData->ringBuffer, itemsReadFromFile);

					/* Mark thread started here, that way we "prime" the ring buffer before playback */
					pData->threadSyncFlag = 0;
				}
				else
				{
					/* No more data to read */
					pData->threadSyncFlag = 1;
					break;
				}
			}

		/* Sleep a little while... */
		Pa_Sleep(20);
	}

	return 0;
}

typedef int (*ThreadFunctionType)(void*);

static PaError startThread(paData* pData, ThreadFunctionType fn) //from portaudio examples
{
#ifdef _WIN32
	typedef unsigned(__stdcall* WinThreadFunctionType)(void*);
	pData->threadHandle = (void*)_beginthreadex(NULL, 0, (WinThreadFunctionType)fn, pData, CREATE_SUSPENDED, NULL);
	if (pData->threadHandle == NULL) return paUnanticipatedHostError;

	/* Set file thread to a little higher prio than normal */
	SetThreadPriority(pData->threadHandle, THREAD_PRIORITY_ABOVE_NORMAL);

	/* Start it up */
	pData->threadSyncFlag = 1;
	ResumeThread(pData->threadHandle);

#endif
	/* Wait for thread to startup */
	while (pData->threadSyncFlag) {
		Pa_Sleep(10);
	}

	return paNoError;
}

static int stopThread(paData* pData) //from portaudio examples
{
	pData->threadSyncFlag = 1;
	/* Wait for thread to stop */
	while (pData->threadSyncFlag) {
		Pa_Sleep(10);
	}
#ifdef _WIN32
	CloseHandle(pData->threadHandle);
	pData->threadHandle = 0;
#endif

	return paNoError;
}

unsigned long WINAPI mus(LPVOID soundFile)
{
	waveFile* sFile = (waveFile*)soundFile;
	OutputDebugStringA((LPCSTR)"mus function called...\n");
	PaStreamParameters outputParameters;
	PaStream* stream;
	PaError err;
	paData data = { 0 };

	wave* headerInfo = (wave*)sFile->header; //corrupt here.
	//paData pauseData = { 0 }; //agony
	unsigned            numSamples;
	unsigned            numBytes;

	err = Pa_Initialize();

	//begin from portaudio examples

	numSamples = NextPowerOf2((unsigned)(44100 * 0.5 * 2));
	numBytes = numSamples * sizeof(SAMPLE);
	data.ringBufferData = (SAMPLE*)PaUtil_AllocateMemory(numBytes);
	//pauseData.ringBufferData = (SAMPLE*)PaUtil_AllocateMemory(numBytes); //agony

	if (data.ringBufferData == NULL)
	{
		OutputDebugStringA((LPCSTR)"Couldn't allocate ring buffer\n");
		return 0;
	}

	if (PaUtil_InitializeRingBuffer(&data.ringBuffer, sizeof(SAMPLE), numSamples, data.ringBufferData) < 0)
	{
		OutputDebugStringA((LPCSTR)"Failed to initialize ring buffer, not a power of 2 or something...\n");
		return 0;
	}


	//end from portaudio examples

	data.frameIndex = 0;

	outputParameters.device = Pa_GetDefaultOutputDevice(); //write exception for no found device.

	outputParameters.channelCount = 2;
	outputParameters.sampleFormat = 8;
	outputParameters.suggestedLatency = Pa_GetDeviceInfo(outputParameters.device)->defaultLowInputLatency;
	outputParameters.hostApiSpecificStreamInfo = NULL;


	
		err = Pa_OpenStream(&stream, NULL, &outputParameters, headerInfo->sampleRate, 512, 1, callbacker, &data); //going to need to change some of those numbers to variables passed in...
		if (stream)
		{
			OutputDebugStringA((LPCSTR)"Stream Initiated...\n");
			data.file = (FILE*)sFile->file;
			int* filecheck = (int*)data.file;
			if (data.file == NULL)
			{
				printf("%s", "you suck\n");
			}
			else {}

			err = startThread(&data, threadFunctionReadFromRawFile);

			err = Pa_StartStream(stream);

			while ((err = Pa_IsStreamActive(stream)) == 1) { //didn't check if this will work
			//while (true) {
			
				printf("index = %d\n", data.frameIndex); fflush(stdout);
				Pa_Sleep(100);
				if (stop == true)
				{
					err = stopThread(&data);
					Pa_StopStream(&stream);

				}
			}
			state = STOPPED;
			err = Pa_CloseStream(stream);
			fclose(data.file);
		}

		err = Pa_CloseStream(stream);
		delete sFile; // I suppose this is what gets rid of the dynamically created struct. I hope this works.
		//note that there is another dynamically created object that you should delete right here
		Pa_Terminate();

		return 0; //erase later
}
int typeOfFile(const char* filename)
{
	char* f = (char*)filename;
	if ((f = (char*)strrchr(filename, '.')) != NULL) {
		if (strcmp(f, ".mp3") == 0) //im guessing here what the return of strcmp is
		{
			return ITSMPEG;
		}
		else if (strcmp(f, ".wav") == 0)
		{
			return ITSWAVE;
		}
		else return 0;
	}
}

waveFile* parseFile(const char* fileName) //this one is all me. it should really be called parseWaveFile, since this couldn't work for any other type of file given the return type.
{
	FILE* file = fopen(fileName, "rb"); //remember that this function is relying on an accurate filepath

	int* filecheck = (int*)file;
	if (file == NULL)
	{
		printf("%s", "you suck\n");
	}
	else {}
	//before fread we need to malloc the proper amount of memory
	//now how much should that be...
	wave* holder = new wave; //(wave*)malloc(sizeof(wave));
	//char* header[0x2c];
	fread(holder, 1, 0x2c, file); //lets try and take the header into memory...
	wave* waveHeader = (wave*)holder;
	printf("%d\n", waveHeader->filesize); //how big is the file, also did it work? yes it did.
	printf("%d\n", waveHeader->formatType);
	wchar_t text_buffer[20] = { 0 }; //temporary buffer
	swprintf(text_buffer, _countof(text_buffer), L"%d", waveHeader->filesize); // convert
	OutputDebugString(text_buffer); // print
	
	waveFile* complete = new waveFile;
	complete->file = file;
	complete->header = waveHeader;
	return complete; //scope end corrupts.
}

LPCWSTR stringToWide(const char* name) //temporary solution for MP3 filenames for that one function that plays them
{
	int sizeofName = MultiByteToWideChar(CP_UTF8, 0, name, -1, NULL, 0);
	wchar_t* wideName = new wchar_t[sizeofName];
	MultiByteToWideChar(CP_UTF8, 0, name, -1, wideName, sizeofName);
	LPCWSTR widenameOfFileTemp = wideName;
	return widenameOfFileTemp;
}


LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow)
{
	INITCOMMONCONTROLSEX icex;
	icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
	icex.dwICC = ICC_LISTVIEW_CLASSES;
	InitCommonControlsEx(&icex);


	const wchar_t CLASS_NAME[] = L"Music Window Class";
	
	WNDCLASS wc = {};

	wc.lpfnWndProc = WindowProc;
	wc.hInstance = hInstance;
	wc.lpszClassName = CLASS_NAME;
	


	RegisterClass(&wc);

	HWND hwnd = CreateWindowExW(1, CLASS_NAME, L"Music Player", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, NULL, NULL, hInstance, NULL);
	if (hwnd == NULL) { return 0; }

	ShowWindow(hwnd, nCmdShow);
	UpdateWindow(hwnd);

	

	HWND hwndButton = CreateWindowW(L"BUTTON", L"Play", WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_RADIOBUTTON, 10, 10, 100, 30, hwnd, (HMENU)4, (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), NULL);
	//SetWindowTheme(hwndButton, L"Explorer", L"");
	HWND hwndButton3 = CreateWindowW(L"BUTTON", L"Resume", WS_DISABLED | WS_TABSTOP | WS_CHILD | BS_PUSHBUTTON, 10, 10, 100, 30, hwnd, (HMENU)6, (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), NULL);

	HWND hwndButton4 = CreateWindowW(L"BUTTON", L"Pause", WS_DISABLED | WS_TABSTOP | WS_CHILD | BS_PUSHBUTTON, 10, 10, 100, 30, hwnd, (HMENU)7, (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), NULL);

	HWND hwndButton2 = CreateWindowW(L"BUTTON", L"Stop/End", WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, 200, 10, 100, 30, hwnd, (HMENU)5, (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), NULL);

	//HWND titleBar = CreateWindowW()
	//SetThemeAppProperties(STAP_ALLOW_CONTROLS);
	//SendMessage(hwnd, WM_THEMECHANGED, NULL, NULL);

	NONCLIENTMETRICS ncm;
	ncm.cbSize = sizeof(NONCLIENTMETRICS);
	::SystemParametersInfo(SPI_GETNONCLIENTMETRICS, sizeof(NONCLIENTMETRICS), &ncm, 0);
	HFONT hFont = ::CreateFontIndirect(&ncm.lfMessageFont);
	::SendMessage(hwndButton, WM_SETFONT, (WPARAM)hFont, MAKELPARAM(TRUE, 0));
	::SendMessage(hwndButton2, WM_SETFONT, (WPARAM)hFont, MAKELPARAM(TRUE, 0));
	::SendMessage(hwndButton3, WM_SETFONT, (WPARAM)hFont, MAKELPARAM(TRUE, 0));
	::SendMessage(hwndButton4, WM_SETFONT, (WPARAM)hFont, MAKELPARAM(TRUE, 0));

	HRESULT hr = D2D1CreateFactory(
		D2D1_FACTORY_TYPE_SINGLE_THREADED,
		&pD2DFactory
	);



	MSG msg = { };
	while (GetMessage(&msg, NULL, 0, 0) > 0)
	{
		switch (state)
		{
		case PAUSED:
			ShowWindow(hwndButton4, SW_HIDE);
			EnableWindow(hwndButton3, true);
			ShowWindow(hwndButton3, SW_NORMAL);

			state = 0;
			break;
		case PLAYINGINITIAL:
			ShowWindow(hwndButton, SW_HIDE);
			EnableWindow(hwndButton4, true);
			ShowWindow(hwndButton4, SW_NORMAL);
			
			state = 0;
			break;
		case STOPPED:
			ShowWindow(hwndButton4, SW_HIDE);
			ShowWindow(hwndButton3, SW_HIDE);
			ShowWindow(hwndButton, SW_RESTORE);
			state = 0; // include this lest you repeatedly ShowWindow everytime the message loop loops... have it go to default if no other command shall run...
			break;
		case RESUMED:
			ShowWindow(hwndButton3, SW_HIDE);
			EnableWindow(hwndButton4, true);
			ShowWindow(hwndButton4, SW_NORMAL);

			state = 0;
			break;
		default:
			printf("%s", "normal");
			break;
		}


		//important part below.
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	return 0;


}


LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	const char* nameOfFileTemp = "05 Starless.mp3"; //I wonder if japanese characters will work if i dont type them out
	


	switch (uMsg)
	{
	case WM_CREATE:
		return 0;
	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;

	case WM_PAINT:
	{
		PAINTSTRUCT ps;
		HDC hdc = BeginPaint(hwnd, &ps);
		//FillRect(hdc, &ps.rcPaint, (HBRUSH)(1));
		BITMAP bm;
		GetObject(hBmp, sizeof(bm), &bm);

		HDC hdcMem = CreateCompatibleDC(hdc);
		SelectObject(hdcMem, hBmp);
		BitBlt(hdc, 0, 0, bm.bmWidth, bm.bmHeight, hdcMem, 0, 0, SRCCOPY);
		DeleteDC(hdcMem);
		EndPaint(hwnd, &ps);

		RECT rc;
		GetClientRect(hwnd, &rc);


		/*
		ID2D1HwndRenderTarget* pRT = NULL;
		HRESULT hr = pD2DFactory->CreateHwndRenderTarget(D2D1::RenderTargetProperties(), D2D1::HwndRenderTargetProperties(hwnd, D2D1::SizeU(rc.right - rc.left, rc.bottom - rc.top)), &pRT);
		ID2D1SolidColorBrush* stupidBrush = NULL;
		if (SUCCEEDED(hr))
		{
			pRT->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Red), &stupidBrush);
		}
		pRT->BeginDraw();
		pRT->DrawRectangle(D2D1::RectF(rc.left + 500.0f, rc.top + 150.0f, rc.right - 500.0f, rc.bottom - 300.0f), stupidBrush);
		hr = pRT->EndDraw();
		*/

	}
	case WM_LBUTTONDOWN:
	{
		OutputDebugStringA((LPCSTR)"why aren't you painting idiot\n"); //get rid of this
		
	}
	case WM_COMMAND:
		if (LOWORD(wParam) == 4) //play, initial
		{
			
			if (typeOfFile(nameOfFileTemp) == ITSWAVE)
			{
				waveFile* argument1 = parseFile(nameOfFileTemp);
				//OutputDebugStringA((LPCSTR)"Clicky\n");
				//PMYDATA pDataArray[3];
				unsigned long dwThreadIdArray[3];
				//HANDLE hThreadArray[3];
				//pDataArray[1] = (PMYDATA)argument1; //did it lose its pointerness?
				resume = false;
				stop = false;
				state = PLAYINGINITIAL;
				PersistentState = PLAYINGINITIAL;
				CreateThread(NULL, 0, mus, (LPVOID)argument1, 0, &dwThreadIdArray[1]);
			}
			else if (typeOfFile(nameOfFileTemp) == ITSMPEG)
			{
				LPCWSTR wideName = stringToWide(nameOfFileTemp);
				wchar_t openStr[100] = L"open \"";
				LPCWSTR firstConcat = wcscat(openStr, wideName);
				wchar_t endStr[30] = L"\" type mpegvideo alias mp3";
				wchar_t* reOpenStr = (wchar_t*)firstConcat;
				LPCWSTR secondConcat = wcscat(reOpenStr, endStr);
				//LPCWSTR commandWFile = L"open \"05 Starless.mp3\" type mpegvideo";
				
				mciSendString(secondConcat, NULL, 0, 0);
				/*
				wchar_t openStr2[100] = L"play \"";
				LPCWSTR firstConcat2 = wcscat(openStr2, wideName);
				wchar_t endStr2[20] = L"\" repeat";
				wchar_t* reOpenStr2 = (wchar_t*)firstConcat2;
				LPCWSTR secondConcat2 = wcscat(reOpenStr2, endStr2); */
				LPCWSTR test = L"play mp3 repeat";
				//commandWFile = L"play \"05 Starless.mp3\" repeat";
				
				mciSendString(test, NULL, 0, 0);
				state = PLAYINGINITIAL;
				delete wideName;
			}

		}
		else if (LOWORD(wParam) == 5) //stop all playback
		{
			if (typeOfFile(nameOfFileTemp) == ITSWAVE)
			{
				stop = true;
				state = STOPPED;
				PersistentState = STOPPED;
				resume = false;
				pause = false;
			}
			else if (typeOfFile(nameOfFileTemp) == ITSMPEG)
			{
				state = STOPPED;
				mciSendString(L"close mp3", NULL, 0, 0);
			}


		}
		else if (LOWORD(wParam) == 6) //resume
		{
			if (typeOfFile(nameOfFileTemp) == ITSWAVE)
			{
				stop = false;
				pause = false;
				resume = true;
				state = RESUMED;
				PersistentState = RESUMED;
			}
			else if (typeOfFile(nameOfFileTemp) == ITSMPEG)
			{
				state = RESUMED;
				mciSendString(L"resume mp3", NULL, 0, 0);
			}
			
		}
		else if (LOWORD(wParam) == 7) //pause
		{
			if (typeOfFile(nameOfFileTemp) == ITSWAVE)
			{
				resume = false;
				pause = true;
				state = PAUSED;
				PersistentState = PAUSED;
				stop = false;
			}
			else if (typeOfFile(nameOfFileTemp) == ITSMPEG)
			{
				state = PAUSED;
				mciSendString(L"pause mp3", NULL, 0, 0);
			}

		}
		
	return 0;
	}
	return DefWindowProc(hwnd, uMsg, wParam, lParam);
}


#include <windows.h>
#include <Commctrl.h>
#include <stdio.h>

#include "updowncontrol.h"

HWND CreateUpDownControl(HWND parent, int ID, const wchar_t *text, int value, int x, int y)
{
	INITCOMMONCONTROLSEX icex;
	icex.dwICC = ICC_STANDARD_CLASSES;

	CreateWindowEx( 0,
		L"STATIC",  // Predefined class; Unicode assumed 
		text,      // Button text 
		WS_TABSTOP | WS_VISIBLE | WS_EX_TRANSPARENT| WS_CHILD,  // Styles 
		x,         // x position 
		y,         // y position 
		58,        // Button width
		24,        // Button height
		parent,     // Parent window
		0,       // No menu.
		(HINSTANCE)GetWindowLong(parent, GWL_HINSTANCE),
		NULL);      // Pointer not needed.

    HWND hBuddy = CreateWindowEx(WS_EX_LEFT | WS_EX_CLIENTEDGE | WS_EX_CONTEXTHELP,    //Extended window styles.
                              WC_EDIT,
                              NULL,
                              WS_CHILDWINDOW | WS_VISIBLE | WS_BORDER    // Window styles.
                              | ES_NUMBER | ES_LEFT,                     // Edit control styles.
                              x + 168, y,
                              50, 24,
                              parent,
                              NULL,
                              (HINSTANCE)GetWindowLong(parent, GWL_HINSTANCE),
                              NULL);

	icex.dwICC = ICC_UPDOWN_CLASS;    // Set the Initialization Flag value.
    InitCommonControlsEx(&icex);      // Initialize the Common Controls Library.

    HWND hControl = CreateWindowEx(WS_EX_LEFT | WS_EX_LTRREADING,
                              UPDOWN_CLASS,
                              NULL,
                              WS_CHILDWINDOW | WS_VISIBLE
                              | UDS_AUTOBUDDY | UDS_SETBUDDYINT | UDS_ALIGNRIGHT | UDS_ARROWKEYS | UDS_HOTTRACK,
                              0, 0,
                              0, 0,         // Set to zero to automatically size to fit the buddy window.
                              parent,
                              NULL,
                              (HINSTANCE)GetWindowLong(parent, GWL_HINSTANCE),
                              NULL);

    SendMessage(hControl, UDM_SETRANGE, 0, MAKELPARAM(99, 0));

	wchar_t valueText[16];
	swprintf_s(valueText, L"%d", value);
	SendMessage(hBuddy, WM_SETTEXT, (WPARAM) 0, (LPARAM)valueText);
	return hControl;

	//return CreateWindow(WC_,
	//	L"",
	//	CBS_HASSTRINGS | WS_CHILD | WS_OVERLAPPED | WS_VISIBLE | CBS_DROPDOWNLIST,
	//	x + 58,
	//	y,
	//	160,
	//	24,
	//	parent,
	//	(HMENU)ID,
	//	(HINSTANCE)GetWindowLong(parent, GWL_HINSTANCE),
	//	NULL);
}
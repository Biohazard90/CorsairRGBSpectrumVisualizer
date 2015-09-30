
#include <windows.h>
#include <Commctrl.h>

#include "combobox.h"

HWND CreateComboBox(HWND parent, int ID, const wchar_t *text, int x, int y)
{
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

	return CreateWindow(WC_COMBOBOX,
		L"",
		CBS_HASSTRINGS | WS_CHILD | WS_OVERLAPPED | WS_VISIBLE | CBS_DROPDOWNLIST,
		x + 58,
		y,
		160,
		24,
		parent,
		(HMENU)ID,
		(HINSTANCE)GetWindowLong(parent, GWL_HINSTANCE),
		NULL);
}

void AddItemToComboBox(HWND comboBox, const wchar_t *item)
{
	SendMessage(comboBox, (UINT) CB_ADDSTRING, (WPARAM) 0, (LPARAM)item);
}

void SelectComboBoxItem(HWND comboBox, int item)
{
	SendMessage(comboBox, CB_SETCURSEL, (WPARAM)item, (LPARAM)0);
}

#include <windows.h>
#include <Commctrl.h>

#include "colorbutton.h"

HWND CreateColorButton(HWND parent, int ID, const wchar_t *text, int x, int y)
{
	HWND hwndButton = CreateWindowEx( NULL,
		L"BUTTON",  // Predefined class; Unicode assumed 
		L"",      // Button text 
		WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON,  // Styles 
		x + 170,         // x position 
		y,         // y position 
		48,        // Button width
		24,        // Button height
		parent,     // Parent window
		(HMENU)ID,       // No menu.
		(HINSTANCE)GetWindowLong(parent, GWL_HINSTANCE),
		NULL);      // Pointer not needed.

	CreateWindowEx( 0,
		L"STATIC",  // Predefined class; Unicode assumed 
		text,      // Button text 
		WS_TABSTOP | WS_VISIBLE | WS_EX_TRANSPARENT| WS_CHILD,  // Styles 
		x,         // x position 
		y,         // y position 
		150,        // Button width
		24,        // Button height
		parent,     // Parent window
		0,       // No menu.
		(HINSTANCE)GetWindowLong(parent, GWL_HINSTANCE),
		NULL);      // Pointer not needed.

	return hwndButton;
}

int DrawColorButton(LPNMHDR some_item, unsigned char *colors)
{
	LPNMCUSTOMDRAW item = (LPNMCUSTOMDRAW)some_item;
	//HPEN pen = CreatePen(PS_INSIDEFRAME, 0, RGB(0, 0, 0));
	HBRUSH brush = CreateSolidBrush(RGB(colors[0], colors[1], colors[2]));

	//HGDIOBJ old_pen = SelectObject(item->hdc, pen);
	HGDIOBJ old_brush = SelectObject(item->hdc, brush);

	FillRect(item->hdc, &item->rc, brush);
	//RoundRect(item->hdc, item->rc.left, item->rc.top, item->rc.right, item->rc.bottom, 5, 5);

	//SelectObject(item->hdc, old_pen);
	SelectObject(item->hdc, old_brush);
	//DeleteObject(pen);
	DeleteObject(brush);

	return CDRF_DODEFAULT;
}
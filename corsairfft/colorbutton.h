#pragma once

#include <windows.h>

HWND CreateColorButton(HWND parent, int ID, const wchar_t *text, int x, int y);
int DrawColorButton(LPNMHDR some_item, unsigned char *colors);
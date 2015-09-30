#pragma once

#include <windows.h>

HWND CreateComboBox(HWND parent, int ID, const wchar_t *text, int x, int y);
void AddItemToComboBox(HWND comboBox, const wchar_t *item);
void SelectComboBoxItem(HWND comboBox, int item);
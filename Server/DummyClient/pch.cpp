#include "pch.h"

std::atomic_int active_clients{ 0 };
std::atomic_int global_delay{ 0 };
HWND g_hWnd = NULL;
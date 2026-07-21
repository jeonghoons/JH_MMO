#include "pch.h"
#include "Utils.h"

wstring Utils::UTF8ToWString(const string& utf8Str)
{
    if (utf8Str.empty()) return wstring();

    int size_needed = MultiByteToWideChar(CP_UTF8, 0, &utf8Str[0], (int)utf8Str.size(), NULL, 0);
    wstring wstrTo(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, &utf8Str[0], (int)utf8Str.size(), &wstrTo[0], size_needed);

    return wstrTo;
}

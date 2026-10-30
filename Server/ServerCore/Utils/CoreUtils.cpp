#include "CorePch.h"
#include "Utils/CoreUtils.h"

wstring Utils::UTF8ToWString(const string& utf8Str)
{
    if (utf8Str.empty()) return wstring();

    int size_needed = MultiByteToWideChar(CP_UTF8, 0, &utf8Str[0], (int)utf8Str.size(), NULL, 0);
    wstring wstrTo(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, &utf8Str[0], (int)utf8Str.size(), &wstrTo[0], size_needed);

    return wstrTo;
}

string Utils::WStringToUTF8(const wstring& wideStr)
{
    if (wideStr.empty()) return string();

    int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wideStr[0], (int)wideStr.size(), NULL, 0, NULL, NULL);
    string strTo(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, &wideStr[0], (int)wideStr.size(), &strTo[0], size_needed, NULL, NULL);

    return strTo;
}

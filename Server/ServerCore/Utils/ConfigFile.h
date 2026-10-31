#pragma once
#include <string>
#include <vector>

// 서버 설정용 단순 INI 파서.
// 형식: UTF-8(BOM 허용), [섹션], 키=값, ';' 또는 '#'로 시작하는 줄은 주석.
// 값 뒤의 주석은 지원하지 않는다(DB 연결 문자열에 ';'가 들어가므로 값은 '=' 뒤 줄 끝까지 그대로 쓴다).
// 형식이 틀리면 조용히 넘어가지 않고 줄 번호가 포함된 오류 메시지를 돌려준다. 어떤 키가 유효한지는 사용하는 쪽이 판단한다.

struct ConfigEntry
{
	std::string section;
	std::string key;
	std::string value;
	int line = 0;
};

class ConfigFile
{
public:
	// 파일을 읽어 파싱한다. 실패하면 false, err에 파일명과 줄 번호가 담긴 사유를 적는다.
	// 섹션 밖의 키, '='가 없는 줄, 같은 섹션의 중복 키는 오류다.
	bool Load(const std::string& path, std::string& err);

	const std::string& Path() const { return _path; }
	const std::vector<ConfigEntry>& Entries() const { return _entries; }

private:
	std::string _path;
	std::vector<ConfigEntry> _entries;
};

#pragma once
#include <string>
#include <vector>
#include <unordered_map>

// Common/DataTable/*.csv 를 읽는 단순 CSV 파서.
// 규약(Docs/DESIGN_DATA.md): UTF-8(BOM 허용), 쉼표 구분, 첫 행은 헤더, 첫 컬럼 Name = 정수 ID.
// 형식이 틀리면 조용히 넘어가지 않고 줄 번호가 포함된 오류 메시지를 돌려준다.

class CsvRow
{
public:
	int Line() const { return _line; }

	// 컬럼이 없거나 값이 형식에 맞지 않으면 false를 반환하고 err에 사유를 적는다.
	bool GetInt(const char* column, int& out, std::string& err) const;
	bool GetFloat(const char* column, float& out, std::string& err) const;
	bool GetString(const char* column, std::string& out, std::string& err) const;

private:
	friend class CsvFile;
	const std::unordered_map<std::string, int>* _columns = nullptr;
	std::vector<std::string> _cells;
	int _line = 0;
	const std::string* _fileName = nullptr;

	bool Find(const char* column, const std::string*& cell, std::string& err) const;
};

class CsvFile
{
public:
	// 파일을 읽어 파싱한다. 실패하면 false, err에 파일명과 줄 번호가 담긴 사유를 적는다.
	bool Load(const std::string& path, std::string& err);

	const std::string& Path() const { return _path; }
	const std::vector<CsvRow>& Rows() const { return _rows; }

private:
	std::string _path;
	std::unordered_map<std::string, int> _columns;
	std::vector<CsvRow> _rows;
};

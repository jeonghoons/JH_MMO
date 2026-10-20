#include "pch.h"
#include "CsvFile.h"
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <cerrno>
#include <climits>

namespace
{
	// 한 줄을 쉼표로 나눈다. 큰따옴표로 감싼 칸 안의 쉼표와 "" 이스케이프를 지원한다.
	bool SplitLine(const std::string& line, std::vector<std::string>& cells, std::string& why)
	{
		cells.clear();
		std::string cur;
		bool inQuotes = false;

		for (size_t i = 0; i < line.size(); ++i)
		{
			const char c = line[i];
			if (inQuotes)
			{
				if (c == '"')
				{
					if (i + 1 < line.size() && line[i + 1] == '"') { cur.push_back('"'); ++i; }
					else inQuotes = false;
				}
				else cur.push_back(c);
			}
			else if (c == '"') inQuotes = true;
			else if (c == ',') { cells.push_back(cur); cur.clear(); }
			else cur.push_back(c);
		}

		if (inQuotes) { why = "closing quote is missing"; return false; }
		cells.push_back(cur);
		return true;
	}

	std::string Trim(const std::string& s)
	{
		size_t b = 0, e = s.size();
		while (b < e && (s[b] == ' ' || s[b] == '\t')) ++b;
		while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t')) --e;
		return s.substr(b, e - b);
	}
}

bool CsvRow::Find(const char* column, const std::string*& cell, std::string& err) const
{
	auto it = _columns->find(column);
	if (it == _columns->end())
	{
		err = *_fileName + ": column '" + column + "' does not exist";
		return false;
	}
	cell = &_cells[it->second];
	return true;
}

bool CsvRow::GetString(const char* column, std::string& out, std::string& err) const
{
	const std::string* cell = nullptr;
	if (!Find(column, cell, err)) return false;
	out = *cell;
	return true;
}

bool CsvRow::GetInt(const char* column, int& out, std::string& err) const
{
	const std::string* cell = nullptr;
	if (!Find(column, cell, err)) return false;

	const std::string text = Trim(*cell);
	char* end = nullptr;
	errno = 0;
	const long v = std::strtol(text.c_str(), &end, 10);
	if (text.empty() || *end != '\0' || errno == ERANGE || v < INT_MIN || v > INT_MAX)
	{
		err = *_fileName + " line " + std::to_string(_line) + ": column '" + column + "' is not an integer: '" + text + "'";
		return false;
	}
	out = static_cast<int>(v);
	return true;
}

bool CsvRow::GetFloat(const char* column, float& out, std::string& err) const
{
	const std::string* cell = nullptr;
	if (!Find(column, cell, err)) return false;

	const std::string text = Trim(*cell);
	char* end = nullptr;
	errno = 0;
	const float v = std::strtof(text.c_str(), &end);
	if (text.empty() || *end != '\0' || errno == ERANGE)
	{
		err = *_fileName + " line " + std::to_string(_line) + ": column '" + column + "' is not a number: '" + text + "'";
		return false;
	}
	out = v;
	return true;
}

bool CsvFile::Load(const std::string& path, std::string& err)
{
	_path = path;
	_columns.clear();
	_rows.clear();

	std::ifstream file(path, std::ios::binary);
	if (!file.is_open())
	{
		err = path + ": file cannot be opened";
		return false;
	}

	std::string line;
	int lineNo = 0;
	bool headerRead = false;
	size_t headerCount = 0;

	while (std::getline(file, line))
	{
		++lineNo;
		if (!line.empty() && line.back() == '\r') line.pop_back();
		if (lineNo == 1 && line.size() >= 3 && static_cast<unsigned char>(line[0]) == 0xEF &&
			static_cast<unsigned char>(line[1]) == 0xBB && static_cast<unsigned char>(line[2]) == 0xBF)
			line.erase(0, 3);   // UTF-8 BOM

		if (Trim(line).empty()) continue;

		std::vector<std::string> cells;
		std::string why;
		if (!SplitLine(line, cells, why))
		{
			err = path + " line " + std::to_string(lineNo) + ": " + why;
			return false;
		}

		if (!headerRead)
		{
			for (size_t i = 0; i < cells.size(); ++i)
			{
				const std::string name = Trim(cells[i]);
				if (name.empty())
				{
					err = path + " line " + std::to_string(lineNo) + ": header has an empty column name";
					return false;
				}
				if (!_columns.emplace(name, static_cast<int>(i)).second)
				{
					err = path + " line " + std::to_string(lineNo) + ": duplicate column '" + name + "'";
					return false;
				}
			}
			headerCount = cells.size();
			headerRead = true;
			continue;
		}

		if (cells.size() != headerCount)
		{
			err = path + " line " + std::to_string(lineNo) + ": expected " + std::to_string(headerCount) +
				" columns but found " + std::to_string(cells.size());
			return false;
		}

		CsvRow row;
		row._columns = &_columns;
		row._cells = std::move(cells);
		row._line = lineNo;
		row._fileName = &_path;
		_rows.push_back(std::move(row));
	}

	if (!headerRead)
	{
		err = path + ": header row is missing";
		return false;
	}
	return true;
}

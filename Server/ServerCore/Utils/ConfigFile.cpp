#include "CorePch.h"
#include "Utils/ConfigFile.h"
#include <fstream>

namespace
{
	std::string Trim(const std::string& s)
	{
		size_t b = 0, e = s.size();
		while (b < e && (s[b] == ' ' || s[b] == '\t')) ++b;
		while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t')) --e;
		return s.substr(b, e - b);
	}
}

bool ConfigFile::Load(const std::string& path, std::string& err)
{
	_path = path;
	_entries.clear();

	std::ifstream file(path, std::ios::binary);
	if (!file.is_open())
	{
		err = path + ": cannot open file";
		return false;
	}

	std::string section;
	std::string line;
	int lineNo = 0;

	while (std::getline(file, line))
	{
		++lineNo;
		if (!line.empty() && line.back() == '\r') line.pop_back();
		if (lineNo == 1 && line.size() >= 3 &&
			(unsigned char)line[0] == 0xEF && (unsigned char)line[1] == 0xBB && (unsigned char)line[2] == 0xBF)
		{
			line.erase(0, 3);
		}

		const std::string text = Trim(line);
		if (text.empty() || text[0] == ';' || text[0] == '#') continue;

		const std::string where = path + " line " + std::to_string(lineNo) + ": ";

		if (text[0] == '[')
		{
			if (text.back() != ']' || text.size() < 3)
			{
				err = where + "invalid section header '" + text + "'";
				return false;
			}
			section = Trim(text.substr(1, text.size() - 2));
			continue;
		}

		const size_t eq = text.find('=');
		if (eq == std::string::npos)
		{
			err = where + "expected 'Key=Value' but found '" + text + "'";
			return false;
		}

		ConfigEntry entry;
		entry.section = section;
		entry.key = Trim(text.substr(0, eq));
		entry.value = Trim(text.substr(eq + 1));
		entry.line = lineNo;

		if (entry.key.empty())
		{
			err = where + "key is empty";
			return false;
		}
		if (section.empty())
		{
			err = where + "key '" + entry.key + "' is outside of any [Section]";
			return false;
		}
		for (const ConfigEntry& e : _entries)
		{
			if (e.section == entry.section && e.key == entry.key)
			{
				err = where + "duplicate key '" + section + "." + entry.key + "' (first at line " + std::to_string(e.line) + ")";
				return false;
			}
		}
		_entries.push_back(std::move(entry));
	}
	return true;
}

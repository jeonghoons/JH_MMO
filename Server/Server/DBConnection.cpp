#include "pch.h"
#include "DBConnection.h"

bool DBConnection::Connect(SQLHENV henv, const WCHAR* connectionPath)
{
	if (::SQLAllocHandle(SQL_HANDLE_DBC, henv, &_connection) != SQL_SUCCESS)
		return false;

	WCHAR connectionString[MAX_PATH]{};
	::wcscpy_s(connectionString, connectionPath);

	WCHAR resultString[MAX_PATH]{};
	SQLSMALLINT len{};

	SQLRETURN ret = ::SQLDriverConnectW(_connection, NULL,
		connectionString, _countof(connectionString),
		resultString, _countof(resultString),
		&len,
		SQL_DRIVER_NOPROMPT
	);

	if (::SQLAllocHandle(SQL_HANDLE_STMT, _connection, &_statement) != SQL_SUCCESS)
		return false;

	return (ret == SQL_SUCCESS || SQL_SUCCESS_WITH_INFO);
	
}

bool DBConnection::Execute(const WCHAR* query)
{
	SQLRETURN ret = ::SQLExecDirectW(_statement, (SQLWCHAR*)query, SQL_NTSL);
	if (ret == SQL_SUCCESS || ret == SQL_SUCCESS_WITH_INFO)
		return true;


	HandleError(ret);
	return false;
}

bool DBConnection::Fetch()
{
	SQLRETURN ret = ::SQLFetch(_statement);

	switch (ret)
	{
	case SQL_SUCCESS:
	case SQL_SUCCESS_WITH_INFO:
		return true;

	case SQL_NO_DATA:
		return false;

	case SQL_ERROR:
	default:
		HandleError(ret);
		return false;
	}
}

int DBConnection::GetRowCount()
{
	SQLLEN count = 0;
	SQLRETURN ret = ::SQLRowCount(_statement, &count);

	if (ret == SQL_SUCCESS || ret == SQL_SUCCESS_WITH_INFO)
		return static_cast<int>(count);

	return -1;
}

void DBConnection::Unbind()
{
	::SQLFreeStmt(_statement, SQL_UNBIND);
	::SQLFreeStmt(_statement, SQL_RESET_PARAMS);
	::SQLFreeStmt(_statement, SQL_CLOSE);
}

bool DBConnection::BindParam(int paramIndex, int* value, SQLLEN* index)
{
	return BindParam(paramIndex, SQL_C_LONG, SQL_INTEGER, sizeof(int), value, index);
}

bool DBConnection::BindParam(int paramIndex, int8_t* value, SQLLEN* index)
{
	return BindParam(paramIndex, SQL_C_CHAR, SQL_TINYINT, sizeof(int8_t), value, index);
}

bool DBConnection::BindParam(int paramIndex, int16_t* value, SQLLEN* index)
{
	return BindParam(paramIndex, SQL_C_SHORT, SQL_SMALLINT, sizeof(int16_t), value, index);
}

bool DBConnection::BindParam(int paramIndex, int64_t* value, SQLLEN* index)
{
	return BindParam(paramIndex, SQL_C_SBIGINT, SQL_BIGINT, sizeof(int64_t), value, index);
}

bool DBConnection::BindParam(int paramIndex, float* value, SQLLEN* index)
{
	return BindParam(paramIndex, SQL_C_FLOAT, SQL_REAL, sizeof(float), value, index);
}

bool DBConnection::BindParam(int paramIndex, double* value, SQLLEN* index)
{
	return BindParam(paramIndex, SQL_C_DOUBLE, SQL_DOUBLE, sizeof(double), value, index);
}

bool DBConnection::BindParam(int paramIndex, const WCHAR* str, SQLLEN* index)
{
	SQLULEN size = static_cast<SQLULEN>((::wcslen(str) + 1) * sizeof(WCHAR));
	*index = SQL_NTSL;

	if (size > 4000)
		return BindParam(paramIndex, SQL_C_WCHAR, SQL_WLONGVARCHAR, size, (SQLPOINTER)str, index);
	else
		return BindParam(paramIndex, SQL_C_WCHAR, SQL_WVARCHAR, size, (SQLPOINTER)str, index);
}

bool DBConnection::BindParam(int paramIndex, TIMESTAMP_STRUCT* value, SQLLEN* index)
{
	return BindParam(paramIndex, SQL_C_TYPE_TIMESTAMP, SQL_TYPE_TIMESTAMP, sizeof(TIMESTAMP_STRUCT), value, index);
}

bool DBConnection::BindCol(int columnIndex, int* value, SQLLEN* index)
{
	return BindCol(columnIndex, SQL_C_LONG, sizeof(int), value, index);
}

bool DBConnection::BindCol(int columnIndex, int8_t* value, SQLLEN* index)
{
	return BindCol(columnIndex, SQL_C_CHAR, sizeof(int8_t), value, index);
}

bool DBConnection::BindCol(int columnIndex, int16_t* value, SQLLEN* index)
{
	return BindCol(columnIndex, SQL_C_SHORT, sizeof(int16_t), value, index);
}

bool DBConnection::BindCol(int columnIndex, int64_t* value, SQLLEN* index)
{
	return BindCol(columnIndex, SQL_C_SBIGINT, sizeof(int64_t), value, index);
}

bool DBConnection::BindCol(int columnIndex, float* value, SQLLEN* index)
{
	return BindCol(columnIndex, SQL_C_FLOAT, sizeof(float), value, index);
}

bool DBConnection::BindCol(int columnIndex, double* value, SQLLEN* index)
{
	return BindCol(columnIndex, SQL_DOUBLE, sizeof(double), value, index);
}

bool DBConnection::BindCol(int columnIndex, WCHAR* str, int size, SQLLEN* index)
{
	return BindCol(columnIndex, SQL_C_WCHAR, size, str, index);
}

bool DBConnection::BindCol(int columnIndex, TIMESTAMP_STRUCT* value, SQLLEN* index)
{
	return BindCol(columnIndex, SQL_C_TYPE_TIMESTAMP, sizeof(TIMESTAMP_STRUCT), value, index);
}

bool DBConnection::BindParam(SQLUSMALLINT paramIndex, SQLSMALLINT cType, SQLSMALLINT sqlType, SQLULEN len, SQLPOINTER ptr, SQLLEN* index)
{
	SQLRETURN ret = ::SQLBindParameter(_statement, paramIndex, SQL_PARAM_INPUT, cType, sqlType, len, 0, ptr, 0, index);
	if (ret != SQL_SUCCESS && ret != SQL_SUCCESS_WITH_INFO)
	{
		HandleError(ret);
		return false;
	}

	return true;
}

bool DBConnection::BindCol(SQLUSMALLINT columnIndex, SQLSMALLINT cType, SQLULEN len, SQLPOINTER value, SQLLEN* index)
{
	SQLRETURN ret = ::SQLBindCol(_statement, columnIndex, cType, value, len, index);
	if (ret != SQL_SUCCESS && ret != SQL_SUCCESS_WITH_INFO)
	{
		HandleError(ret);
		return false;
	}

	return true;
}

void DBConnection::HandleError(SQLRETURN ret)
{
	SQLSMALLINT index = 1;
	SQLWCHAR sqlState[MAX_PATH]{};
	SQLINTEGER nativeErr = 0;
	SQLWCHAR errMsg[MAX_PATH]{};
	SQLSMALLINT msgLen = 0;
	SQLRETURN errorRet = 0;

	while (true)
	{
		errorRet = ::SQLGetDiagRecW(
			SQL_HANDLE_STMT,
			_statement,
			index,
			sqlState,
			&nativeErr,
			errMsg,
			_countof(errMsg),
			&msgLen
		);

		if (errorRet == SQL_NO_DATA)
			break;

		if (errorRet != SQL_SUCCESS && errorRet != SQL_SUCCESS_WITH_INFO)
			break;

		
		std::wcout.imbue(std::locale("kor"));
		std::wcout << errMsg << std::endl;

		index++;
	}
}

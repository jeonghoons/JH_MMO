#pragma once


class DBConnection
{
public:
	bool			Connect(SQLHENV henv, const WCHAR* connectionPath);

	bool			Execute(const WCHAR* query);
	bool			Fetch();
	int			GetRowCount();
	void			Unbind();


public:
	bool			BindParam(int paramIndex, int* value, SQLLEN* index);
	bool			BindParam(int paramIndex, int8_t* value, SQLLEN* index);
	bool			BindParam(int paramIndex, int16_t* value, SQLLEN* index);
	bool			BindParam(int paramIndex, int64_t* value, SQLLEN* index);
	bool			BindParam(int paramIndex, float* value, SQLLEN* index);
	bool			BindParam(int paramIndex, double* value, SQLLEN* index);
	bool			BindParam(int paramIndex, const WCHAR* str, SQLLEN* index);
	bool			BindParam(int paramIndex, TIMESTAMP_STRUCT* value, SQLLEN* index);


	bool			BindCol(int columnIndex, int* value, SQLLEN* index);
	bool			BindCol(int columnIndex, int8_t* value, SQLLEN* index);
	bool			BindCol(int columnIndex, int16_t* value, SQLLEN* index);
	bool			BindCol(int columnIndex, int64_t* value, SQLLEN* index);
	bool			BindCol(int columnIndex, float* value, SQLLEN* index);
	bool			BindCol(int columnIndex, double* value, SQLLEN* index);
	bool			BindCol(int columnIndex, WCHAR* str, int size, SQLLEN* index);
	bool			BindCol(int columnIndex, TIMESTAMP_STRUCT* value, SQLLEN* index);

private:
	bool			BindParam(SQLUSMALLINT paramIndex, SQLSMALLINT cType, SQLSMALLINT sqlType, SQLULEN len, SQLPOINTER ptr, SQLLEN* index);
	bool			BindCol(SQLUSMALLINT columnIndex, SQLSMALLINT cType, SQLULEN len, SQLPOINTER value, SQLLEN* index);
	
	void			HandleError(SQLRETURN ret);

private:
	SQLHDBC		_connection{ SQL_NULL_HANDLE };
	SQLHSTMT		_statement{ SQL_NULL_HANDLE };
};


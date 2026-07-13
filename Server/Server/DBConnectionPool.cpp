#include "pch.h"
#include "DBConnectionPool.h"

DBConnectionPool::DBConnectionPool()
{
}

DBConnectionPool::~DBConnectionPool()
{
	if (_enviroment != SQL_NULL_HANDLE) {
		::SQLFreeHandle(SQL_HANDLE_ENV, _enviroment);
		_enviroment = SQL_NULL_HANDLE;
	}

	while (_connectionQueue.empty() == false) {
		DBConnection* conn = _connectionQueue.front();
		delete conn;
		_connectionQueue.pop();
	}
	
}

bool DBConnectionPool::Connect(int connectionCount, const WCHAR* connectionPath)
{

	if (::SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &_enviroment) != SQL_SUCCESS)
		return false;

	if (::SQLSetEnvAttr(_enviroment, SQL_ATTR_ODBC_VERSION, reinterpret_cast<SQLPOINTER>(SQL_OV_ODBC3), 0) != SQL_SUCCESS)
		return false;



	for (int i = 0; i < connectionCount; ++i) {
		DBConnection* conn = new DBConnection();
		if (false == conn->Connect(_enviroment, connectionPath))
			return false;
		_connectionQueue.push(conn);
	}


	return true;
}

DBConnection* DBConnectionPool::Pop()
{
	lock_guard<mutex> lock(_mutex);

	if (_connectionQueue.empty())
		return nullptr;

	DBConnection* conn = _connectionQueue.front();
	_connectionQueue.pop();
	return conn;
}

void DBConnectionPool::Push(DBConnection* connection)
{
	lock_guard<mutex> lock(_mutex);
	_connectionQueue.push(connection);
}

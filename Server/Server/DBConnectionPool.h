#pragma once
#include "DBConnection.h"

class DBConnectionPool
{
public:
	DBConnectionPool();
	~DBConnectionPool();

	bool Connect(int connectionCount, const WCHAR* connectionPath);
	
	DBConnection* Pop();
	void			Push(DBConnection* connection);

private:
	SQLHENV		_enviroment = SQL_NULL_HANDLE;
	queue<DBConnection*>	_connectionQueue;
	mutex				_mutex;
};
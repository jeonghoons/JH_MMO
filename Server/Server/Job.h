#pragma once
#include "pch.h"
#include <functional>

class Job
{
public:
	// 인자를 안받고 void를 반환하는 Job
	Job(function<void()>&& func) : _func(move(func)) {}


	// 클래스의 멤버 함수를 호출하는 Job
	template<typename T, typename... Arguments>
	Job(shared_ptr<T> owner, void(T::* memFunc)(Arguments...), Arguments&&... args)
	{
		_func = [owner, memFunc, args...]()
			{
				(owner.get()->*memFunc)(args...);
			};
	}

	void Execute()
	{
		_func();
	}

private:
	function<void()>		_func;
};


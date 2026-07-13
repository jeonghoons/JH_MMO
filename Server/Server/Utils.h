#pragma once
#include <random>

class Utils
{
public:
	template<typename T>
	static T GetRandom(T min, T max)
	{
		thread_local std::random_device rd;
		thread_local std::mt19937 generator(rd());
		if constexpr (std::is_integral_v<T>)
		{
			std::uniform_int_distribution<T> distribution(min, max);
			return distribution(generator);
		}
		else
		{
			std::uniform_real_distribution<T> distribution(min, max);
			return distribution(generator);
		}
	}
};

struct Cooldown
{
	std::chrono::steady_clock::time_point lastTick;
	long long durationMs = 0;

	bool IsReady() const
	{
		auto now = std::chrono::steady_clock::now();
		auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastTick).count();
		return elapsed >= durationMs;
	}

	void Reset(long long newDurationMs)
	{
		lastTick = std::chrono::steady_clock::now();
		durationMs = newDurationMs;
	}
};

class MathUtils
{
public:
	static constexpr float PI = 3.14159265f;

	// 부채꼴 충돌 판정 
	static bool CheckSector(const PositionInfo& origin, const PositionInfo& target, float radius, float angleDegree)
	{
		float dx = target.x - origin.x;
		float dy = target.y - origin.y;

		float distSq = (dx * dx) + (dy * dy);
		if (distSq > (radius * radius)) return false;
		if (distSq == 0.0f) return true;

		float dist = std::sqrt(distSq);
		float dirX = dx / dist;
		float dirY = dy / dist;

		float yawRad = origin.yaw * (PI / 180.0f);
		float forwardX = std::cos(yawRad);
		float forwardY = std::sin(yawRad);

		float dot = (forwardX * dirX) + (forwardY * dirY);
		float cosHalf = std::cos((angleDegree * 0.5f) * (PI / 180.0f));

		return dot >= cosHalf;
	}

	// 직선 충돌 판정 
	static bool CheckLine(const PositionInfo& origin, const PositionInfo& target, float width, float range)
	{
		float dx = target.x - origin.x;
		float dy = target.y - origin.y;
		float distSq = (dx * dx) + (dy * dy);

		if (distSq > (range * range)) return false;
		if (distSq == 0.0f) return true;

		float yawRad = origin.yaw * (PI / 180.0f);
		float fwdX = std::cos(yawRad);
		float fwdY = std::sin(yawRad);

		float projDist = (dx * fwdX) + (dy * fwdY);

		if (projDist < 0.0f || projDist > range) return false;

		float perpDistSq = distSq - (projDist * projDist);
		float halfWidth = width / 2.0f;

		return perpDistSq <= (halfWidth * halfWidth);
	}
};


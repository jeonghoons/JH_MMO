#pragma once
// 게임 판정용 수학(부채꼴·직선 충돌). Protocol::PositionInfo를 쓰므로 게임 쪽에 둔다.

class MathUtils
{
public:
	static constexpr float PI = 3.14159265f;

	// 부채꼴 충돌 판정 
	static bool CheckSector(const Protocol::PositionInfo& origin, const Protocol::PositionInfo& target, float radius, float angleDegree)
	{
		float dx = target.x() - origin.x();
		float dy = target.y() - origin.y();

		float distSq = (dx * dx) + (dy * dy);
		if (distSq > (radius * radius)) return false;
		if (distSq == 0.0f) return true;

		float dist = std::sqrt(distSq);
		float dirX = dx / dist;
		float dirY = dy / dist;

		float yawRad = origin.yaw() * (PI / 180.0f);
		float forwardX = std::cos(yawRad);
		float forwardY = std::sin(yawRad);

		float dot = (forwardX * dirX) + (forwardY * dirY);
		float cosHalf = std::cos((angleDegree * 0.5f) * (PI / 180.0f));

		return dot >= cosHalf;
	}

	// 직선 충돌 판정 
	static bool CheckLine(const Protocol::PositionInfo& origin, const Protocol::PositionInfo& target, float width, float range)
	{
		float dx = target.x() - origin.x();
		float dy = target.y() - origin.y();
		float distSq = (dx * dx) + (dy * dy);

		if (distSq > (range * range)) return false;
		if (distSq == 0.0f) return true;

		float yawRad = origin.yaw() * (PI / 180.0f);
		float fwdX = std::cos(yawRad);
		float fwdY = std::sin(yawRad);

		float projDist = (dx * fwdX) + (dy * fwdY);

		if (projDist < 0.0f || projDist > range) return false;

		float perpDistSq = distSq - (projDist * projDist);
		float halfWidth = width / 2.0f;

		return perpDistSq <= (halfWidth * halfWidth);
	}
};

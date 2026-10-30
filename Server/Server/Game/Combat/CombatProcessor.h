#pragma once
#include "pch.h"

class Room;
class Character;

class CombatProcessor
{
public:
	static void ProcessSkillHit(std::shared_ptr<Room> room, std::shared_ptr<Character> attacker, int skillId, int targetId, float aimYaw, float aimX, float aimY);

};

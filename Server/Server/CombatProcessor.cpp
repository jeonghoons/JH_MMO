#include "pch.h"
#include "CombatProcessor.h"
#include "Room.h"
#include "Character.h"
#include "ServerData.h"
#include "NavmeshManager.h"

void CombatProcessor::ProcessSkillHit(std::shared_ptr<Room> room, std::shared_ptr<Character> attacker, int skillId)
{
	if (attacker == nullptr || attacker->GetStat().IsDead()) return;

	const SkillData* skillData = DataManager::GetSkillData(skillId);
	if (skillData == nullptr) return;

	Protocol::ObjectType targetTypeToFind = (attacker->GetType() == Protocol::ObjectType::OBJECT_TYPE_PLAYER) ? Protocol::ObjectType::OBJECT_TYPE_MONSTER : Protocol::ObjectType::OBJECT_TYPE_PLAYER;

	std::shared_ptr<Character> closestProjTarget = nullptr;
	float minProjDistSq = FLT_MAX;

	for (int viewId : attacker->_viewList)
	{
		auto targetObj = room->GetGameObject(viewId);
		if (!targetObj || targetObj->GetType() != targetTypeToFind) continue;

		auto targetChar = std::static_pointer_cast<Character>(targetObj);
		if (targetChar->GetStat().IsDead()) continue;

		bool bIsHit = false;

		if (skillData->hitShape == HitShape::Sector) {
			bIsHit = MathUtils::CheckSector(attacker->GetPosition(), targetChar->GetPosition(), skillData->radius, skillData->angle);
		}
		else if (skillData->hitShape == HitShape::Line || skillData->hitShape == HitShape::Projectile) {
			bIsHit = MathUtils::CheckLine(attacker->GetPosition(), targetChar->GetPosition(), skillData->width, skillData->range);
		}

		if (bIsHit)
		{
			bool hitRayCast = room->GetNavManager()->RayCast(attacker->GetPosition(), targetChar->GetPosition());

			if (hitRayCast)
			{
				if (skillData->hitShape == HitShape::Projectile)
				{
					float diffX = targetChar->GetPosition().x() - attacker->GetPosition().x();
					float diffY = targetChar->GetPosition().y() - attacker->GetPosition().y();
					float distSq = (diffX * diffX) + (diffY * diffY);

					if (distSq < minProjDistSq)
					{
						minProjDistSq = distSq;
						closestProjTarget = targetChar;
					}
				}
				else
				{
					int finalDamage = skillData->baseDamage;
					targetChar->OnDamaged(finalDamage, attacker);
					// std::cout << "Hit Target[" << targetChar->GetId() << "] Dmg: " << finalDamage << std::endl;
				}
			}
		}
	}

	// 투사체인 경우, 가장 가까운 적을 찾아 비행 시간 계산 후 데미지 예약
	if (skillData->hitShape == HitShape::Projectile && closestProjTarget != nullptr)
	{
		float dist = std::sqrt(minProjDistSq);
		int flightTimeMs = 0;

		if (skillData->projSpeed > 0.f) {
			flightTimeMs = static_cast<int>((dist / skillData->projSpeed) * 1000.f);
		}

		if (flightTimeMs <= 0) {
			closestProjTarget->OnDamaged(skillData->baseDamage, attacker);
		}
		else {
			room->ReserveJob(flightTimeMs, &Room::ApplyDelayedDamage, closestProjTarget->GetId(), attacker->GetId(), skillData->baseDamage);
			// std::cout << "Projectile Fired! Flight time: " << flightTimeMs << "ms" << std::endl;
		}
	}
}


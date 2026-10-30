#include "pch.h"
#include "Game/Combat/CombatProcessor.h"
#include "Game/Room/Room.h"
#include "Game/Object/Character.h"
#include "Data/ServerData.h"
#include "Game/World/NavmeshManager.h"

void CombatProcessor::ProcessSkillHit(std::shared_ptr<Room> room, std::shared_ptr<Character> attacker, int skillId, int targetId, float aimYaw, float aimX, float aimY)
{
	if (attacker == nullptr || attacker->GetStat().IsDead()) return;

	const SkillData* skillData = DataManager::GetSkillData(skillId);
	if (skillData == nullptr) return;

	// 시전 시점에 서버가 확정한 방향으로 판정한다. (히트 딜레이 동안 공격자가 이동/회전해도 판정 방향은 바뀌지 않는다)
	Protocol::PositionInfo origin = attacker->GetPosition();
	origin.set_yaw(aimYaw);

	// 지면 장판: 지점에 영역을 만들고 이후 Room::UpdateZones가 주기적으로 피해를 준다.
	if (skillData->hitShape == HitShape::Zone)
	{
		room->CreateZone(attacker, *skillData, aimX, aimY);
		return;
	}

	Protocol::ObjectType targetTypeToFind = (attacker->GetType() == Protocol::ObjectType::OBJECT_TYPE_PLAYER) ? Protocol::ObjectType::OBJECT_TYPE_MONSTER : Protocol::ObjectType::OBJECT_TYPE_PLAYER;

	// 대상 지정 투사체: 지정된 대상 한 명에게만 거리/비행 속도에 따른 지연 후 피해를 준다. (판정 모양 검사 없음)
	if (skillData->targeting == SkillTargeting::Targeted)
	{
		auto targetObj = room->GetGameObject(targetId);
		if (!targetObj || targetObj->GetType() != targetTypeToFind) return;

		auto targetChar = std::static_pointer_cast<Character>(targetObj);
		if (targetChar->GetStat().IsDead()) return;

		const float dx = targetChar->GetPosition().x() - attacker->GetPosition().x();
		const float dy = targetChar->GetPosition().y() - attacker->GetPosition().y();
		const float dist = std::sqrt(dx * dx + dy * dy);

		int flightTimeMs = 0;
		if (skillData->projSpeed > 0.f) {
			flightTimeMs = static_cast<int>((dist / skillData->projSpeed) * 1000.f);
		}

		if (flightTimeMs <= 0) {
			targetChar->OnDamaged(skillData->baseDamage, attacker->GetId(), attacker);
		}
		else {
			room->ReserveJob(flightTimeMs, &Room::ApplyDelayedDamage, targetChar->GetId(), attacker->GetId(), skillData->baseDamage);
		}
		return;
	}

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
			bIsHit = MathUtils::CheckSector(origin, targetChar->GetPosition(), skillData->radius, skillData->angle);
		}
		else if (skillData->hitShape == HitShape::Line || skillData->hitShape == HitShape::Projectile) {
			bIsHit = MathUtils::CheckLine(origin, targetChar->GetPosition(), skillData->width, skillData->range);
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
					targetChar->OnDamaged(finalDamage, attacker->GetId(), attacker);
				}
			}
		}
	}

	// 방향 지정 투사체: 가장 가까운 적을 찾아 비행 시간 계산 후 데미지 예약
	if (skillData->hitShape == HitShape::Projectile && closestProjTarget != nullptr)
	{
		float dist = std::sqrt(minProjDistSq);
		int flightTimeMs = 0;

		if (skillData->projSpeed > 0.f) {
			flightTimeMs = static_cast<int>((dist / skillData->projSpeed) * 1000.f);
		}

		if (flightTimeMs <= 0) {
			closestProjTarget->OnDamaged(skillData->baseDamage, attacker->GetId(), attacker);
		}
		else {
			room->ReserveJob(flightTimeMs, &Room::ApplyDelayedDamage, closestProjTarget->GetId(), attacker->GetId(), skillData->baseDamage);
		}
	}
}
#include "pch.h"
#include "GameObject.h"

GameObject::GameObject(Protocol::ObjectType objectType)
{
	_objectInfo.set_object_type(objectType);
}

MovableObject::MovableObject(Protocol::ObjectType objectType)
    : GameObject(objectType)
{
    _maxSpeed = 500.0f;
    _lastMoveTimePoint = chrono::steady_clock::now();
}

void MovableObject::Update(float deltaTime)
{
    if (deltaTime <= 0.f) return;

    ApplyMovement(deltaTime);
}


bool MovableObject::Move(const XMFLOAT3& desPos)
{
    Protocol::PositionInfo* pos = _objectInfo.mutable_position();

    // 1. 현재 위치에서 목적지를 향하는 벡터 계산
    XMVECTOR vCurr = XMVectorSet(pos->x(), pos->y(), pos->z(), 0.0f);
    XMVECTOR vDest = XMVectorSet(desPos.x, desPos.y, desPos.z, 0.0f);
    XMVECTOR vDir = XMVectorSubtract(vDest, vCurr);

    XMFLOAT3 newMoveDir;
    XMStoreFloat3(&newMoveDir, XMVector3Normalize(vDir)); // 정규화(길이를 1로 만듦)

    // 2. 방향이 유의미하게 꺾였는지 확인 (오차 허용)
    bool dirChanged = (abs(_moveDir.x - newMoveDir.x) > 0.01f ||
        abs(_moveDir.y - newMoveDir.y) > 0.01f ||
        abs(_moveDir.z - newMoveDir.z) > 0.01f);

    // 3. 내 이동 상태(방향, 속도) 갱신
    _moveDir = newMoveDir;
    _currentSpeed = _maxSpeed;

    _velocity.x = _moveDir.x * _currentSpeed;
    _velocity.y = _moveDir.y * _currentSpeed;
    _velocity.z = _moveDir.z * _currentSpeed;

    pos->set_v_x(_velocity.x);
    pos->set_v_y(_velocity.y);
    pos->set_v_z(_velocity.z);

    // 캐릭터가 바라보는 회전각도(Yaw) 갱신 (Z-up 기준)
    pos->set_yaw(XMConvertToDegrees(atan2f(_moveDir.y, _moveDir.x)));
    pos->set_state(Protocol::MOVE_STATE_RUN);

    // 방향이 바뀌었으면 클라이언트에게 패킷을 쏘라고 알려줌
    return dirChanged;
}

void MovableObject::StopMove()
{
    if (_objectInfo.position().state() == Protocol::MOVE_STATE_IDLE) return;

    _currentSpeed = 0.0f;
    _velocity = { 0.f, 0.f, 0.f };
    _moveDir = { 0.f, 0.f, 0.f };

    Protocol::PositionInfo* pos = _objectInfo.mutable_position();
    pos->set_v_x(0.f);
    pos->set_v_y(0.f);
    pos->set_v_z(0.f);
    pos->set_state(Protocol::MOVE_STATE_IDLE);
}

void MovableObject::ApplyMovement(float deltaTime)
{
    if (_currentSpeed <= 0.0f) return;

    Protocol::PositionInfo* pos = _objectInfo.mutable_position();

	XMVECTOR vDir = XMLoadFloat3(&_moveDir);
	XMVECTOR vVel = XMVectorScale(vDir, _currentSpeed);
	XMStoreFloat3(&_velocity, vVel); // 계산된 최종 속도 저장

	// (현재 위치 + 속도 * 시간)
	XMVECTOR vCurr = XMVectorSet(pos->x(), pos->y(), pos->z(), 0);
	XMVECTOR nextPos = XMVectorMultiplyAdd(vVel, XMVectorReplicate(deltaTime), vCurr);

	XMFLOAT3 finalPos;
	XMStoreFloat3(&finalPos, nextPos);

    pos->set_x(finalPos.x);
    pos->set_y(finalPos.y);
    pos->set_z(finalPos.z);
    pos->set_v_x(_velocity.x);
    pos->set_v_y(_velocity.y);
    pos->set_v_z(_velocity.z);

    auto now = chrono::steady_clock::now();
    _lastMoveTimePoint = now;
    _lastMoveTime = static_cast<unsigned int>(chrono::duration_cast<chrono::milliseconds>(now.time_since_epoch()).count());
}


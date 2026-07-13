#pragma once
#include "pch.h"
#include "Room.h"

class GameObject : public enable_shared_from_this<GameObject>
{
public:
	GameObject() = default;
	GameObject(Object_Type objectType);
	virtual ~GameObject(){ _ownerRoom.reset(); }

public:
	virtual void Update(float deltaTime) {}

	shared_ptr<Room> GetCurrentRoom() const { return _ownerRoom.lock(); }
	void SetOwnerRoom(shared_ptr<Room> room) { _ownerRoom = room; }

	PositionInfo& GetPosition() { return _objectInfo.position; }
	const PositionInfo& GetPosition() const { return _objectInfo.position; }
	void SetPosition(const PositionInfo& pos) { _objectInfo.position = pos; }

	Object_Type GetType() const { return _objectInfo.objectType; }
	ObjectInfo& GetInfo() { return _objectInfo; }
	
	const ObjectInfo& GetInfo() const { return _objectInfo; }
	void SetInfo(const ObjectInfo& info) { _objectInfo = info; }

	int GetId() const { return _objectInfo.id; }
	void SetId(int id) { _objectInfo.id = id; }

	unsigned int _last_moveTime{};
protected:
	ObjectInfo		_objectInfo{};
	weak_ptr<Room>	_ownerRoom;
};

class StaticObject : public GameObject
{
public:
	StaticObject(Object_Type objectType) : GameObject(objectType) {}
	virtual ~StaticObject() = default;
};

class MovableObject : public GameObject
{
public:
	MovableObject(Object_Type objectType);
	virtual ~MovableObject() = default;

public:
	virtual void Update(float deltaTime) override;
	virtual bool Move(const XMFLOAT3& desPos);
	virtual void StopMove();

	void SetMaxSpeed(float maxSpeed) { _maxSpeed = maxSpeed; }
	float GetCurrentSpeed() const { return _currentSpeed; }
	XMFLOAT3 GetVelocity() const { return _velocity; }

private:
	void ApplyMovement(float deltaTime);

protected:
	XMFLOAT3 _velocity = { 0.f, 0.f, 0.f }; // 현재 속도 벡터
	XMFLOAT3 _moveDir = { 0.f, 0.f, 0.f };  // 현재 이동 방향 (정규화된 벡터)

	float _currentSpeed = 0.0f;    // 현재 속력
	float _maxSpeed = 500.0f;        // 최대 도달 속력
	
	chrono::steady_clock::time_point _lastMoveTimePoint;
};

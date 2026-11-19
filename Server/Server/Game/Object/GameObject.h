#pragma once

class Room;

class GameObject : public enable_shared_from_this<GameObject>
{
public:
	GameObject() = default;
	explicit GameObject(Protocol::ObjectType objectType);
	virtual ~GameObject() = default;

public:
	virtual void Update(float deltaTime) {}

	// 속한 방. 플레이어의 방은 수신 스레드(입장 배정)·Room 잡이 쓰고 수신 스레드·Disconnect를 부른 스레드·Room 잡이 읽으므로
	// 원자적으로 읽고 쓴다(A5.0, 감사 F9 — Room 간 이동 DG3에서는 Room 잡이 바꾸는 동안 수신 스레드가 읽는다).
	std::shared_ptr<Room> GetCurrentRoom() const { return _ownerRoom.load().lock(); }
	void SetOwnerRoom(std::shared_ptr<Room> room) { _ownerRoom.store(std::weak_ptr<Room>(room)); }

	const Protocol::PositionInfo& GetPosition() const { return _objectInfo.position(); }
	Protocol::PositionInfo* GetMutablePosition() { return _objectInfo.mutable_position(); }
	void SetPosition(const Protocol::PositionInfo& pos) { _objectInfo.mutable_position()->CopyFrom(pos); }

	Protocol::ObjectType GetType() const { return _objectInfo.object_type(); }

	const Protocol::ObjectInfo& GetInfo() const { return _objectInfo; }
	Protocol::ObjectInfo* GetMutableInfo() { return &_objectInfo; }
	void SetInfo(const Protocol::ObjectInfo& info) { _objectInfo.CopyFrom(info); }

	int GetId() const { return _objectInfo.id(); }
	void SetId(int id) { _objectInfo.set_id(id); }

	unsigned int GetLastMoveTime() const { return _lastMoveTime; }
	void SetLastMoveTime(unsigned int time) { _lastMoveTime = time; }

protected:
	Protocol::ObjectInfo _objectInfo{};
	std::atomic<std::weak_ptr<Room>>  _ownerRoom;
	unsigned int         _lastMoveTime = 0;
};

class StaticObject : public GameObject
{
public:
	explicit StaticObject(Protocol::ObjectType objectType) : GameObject(objectType) {}
	virtual ~StaticObject() = default;
};

class MovableObject : public GameObject
{
public:
	explicit MovableObject(Protocol::ObjectType objectType);
	virtual ~MovableObject() = default;

public:
	virtual void Update(float deltaTime) override;
	virtual bool Move(const DirectX::XMFLOAT3& desPos);
	virtual void StopMove();

	void SetMaxSpeed(float maxSpeed) { _maxSpeed = maxSpeed; }
	float GetCurrentSpeed() const { return _currentSpeed; }
	DirectX::XMFLOAT3 GetVelocity() const { return _velocity; }

private:
	void ApplyMovement(float deltaTime);

protected:
	DirectX::XMFLOAT3 _velocity = { 0.f, 0.f, 0.f };
	DirectX::XMFLOAT3 _moveDir = { 0.f, 0.f, 0.f };

	float _currentSpeed = 0.0f;
	float _maxSpeed = 500.0f;

	std::chrono::steady_clock::time_point _lastMoveTimePoint;
};
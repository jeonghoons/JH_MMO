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

	std::shared_ptr<Room> GetCurrentRoom() const { return _ownerRoom.lock(); }
	void SetOwnerRoom(std::shared_ptr<Room> room) { _ownerRoom = room; }

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
	std::weak_ptr<Room>  _ownerRoom;
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
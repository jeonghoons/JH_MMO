#pragma once


class PacketSerializer
{
public:
	
public:
	static shared_ptr<SendBuffer> MAKE_SC_ADD_OBJECT(shared_ptr<GameObject> object);

	static shared_ptr<SendBuffer> MAKE_SC_REMOVE_OBJECT(int ObjectId);

	static shared_ptr<SendBuffer> MAKE_SC_MOVE_OBJECT(shared_ptr<GameObject> object);

	static shared_ptr<SendBuffer> MAKE_SC_ATTACK(int attackerId, int skillId, int targetId);
	
	static shared_ptr<SendBuffer> MAKE_SC_DAMAGE(int attackerId, int targetId, int damage, int remainHp);

	static shared_ptr<SendBuffer> MAKE_SC_DEAD(int objectId);
};


#pragma once
#include "ProtocolSturct.h"


// =======================
#pragma pack (push, 1)
struct CS_LOGIN_PACKET {
	PacketHeader header;
	char accountID[20];
	char accountPW[20];
	bool	isDummy = false;
};

struct CS_SIGNUP_PACKET {
	PacketHeader header;
	char accountID[20];
	char accountPW[20];
	PlayerType playerType;
};

struct CS_LOGOUT_PACKET {
	PacketHeader header;
};

struct CS_CHAT_PACKET {
	PacketHeader header;
	wchar_t message[MAX_CHAT_LEN];
};

struct CS_ENTER_ROOM_PACKET {
	PacketHeader header;
};

struct CS_MOVE_PACKET {
	PacketHeader header;
	PositionInfo posInfo;
	bool force = false;
};

struct CS_DMOVE_PACKET {
	PacketHeader header;
	float inputX;
	float inputY;
};

struct CS_ATTACK_PACKET {
	PacketHeader header;
	int			targetId;
};

// ------------------------------------------
struct SC_LOGIN_INFO_PACKET {
	PacketHeader header;
	ObjectInfo			objectInfo;
};

struct SC_SIGNUP_PACKET {
	PacketHeader header;
	bool result;
};

struct SC_ADD_OBJECT_PACKET {
	PacketHeader header;
	ObjectInfo	objectInfo;
};

struct SC_REMOVE_OBJECT_PACKET {
	PacketHeader header;
	int				objectId;
};

struct SC_CHAT_PACKET {
	PacketHeader header;
	int	senderId;
	wchar_t message[MAX_CHAT_LEN];
};

struct SC_MOVE_PACKET {
	PacketHeader header;
	ObjectInfo objectInfo;
	unsigned int move_time;
};

struct SC_ATTACK_PACKET {
	PacketHeader header;
	int attackerId;
	int skillId;
	int targetId;
};

struct SC_DAMAGE_PACKET {
	PacketHeader header;
	int			attackerId;
	int			targetId;
	int			damage;
	int			remainHp;
};

struct SC_DEAD_PACKET {
	PacketHeader header;
	int			objectId;
};






#pragma pack (pop)





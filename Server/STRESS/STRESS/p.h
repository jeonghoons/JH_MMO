#pragma once
// #include "pch.h"
#include <DirectXMath.h>
using namespace DirectX;

#define PORT_NUM 8888
constexpr int MAX_ROOM_CAPACITY = 20000;

// Packet Key
enum SC_PACKET_LIST : unsigned char
{
	SC_LOGIN, SC_LOGOUT, SC_ADD_PLAYER, SC_REMOVE_PLAYER,
	SC_CHAT, SC_MOVE_OBJECT, SC_ADD_OBJECT, SC_REMOVE_OBJECT
};

enum CS_PACKET_LIST : unsigned char
{
	CS_LOGIN, CS_LOGOUT, CS_CHAT, CS_MOVE
};
// =======================

#pragma pack (push, 1)
struct PacketHeader
{
	unsigned short size;
	unsigned short type;
};

struct PlayerInfo
{
	enum PLAYER_TYPE : unsigned char
	{

	};

	int id;
	std::pair<int, int> position;

};

struct CS_LOGIN_PACKET {
	PacketHeader header;
};

struct CS_LOGOUT_PACKET {
	PacketHeader header;
};

struct CS_CHAT_PACKET {
	PacketHeader header;
	char message[1024];
};

struct CS_MOVE_PACKET {
	PacketHeader header;
	int			direction; // 0 : UP, 1 : DOWN, 2 : LEFT, 3 : RIGHT
	unsigned	move_time;
};

// ------------------------------------------
struct SC_LOGIN_INFO_PACKET {
	PacketHeader header;
	PlayerInfo			player;
};

struct SC_ADD_PLAYER_PACKET {
	PacketHeader header;
	PlayerInfo			player;
};

struct SC_REMOVE_PLAYER_PACKET {
	PacketHeader header;
	int			playerId;
};

struct SC_CHAT_PACKET {
	PacketHeader header;
	char message[1024];
};

struct SC_MOVE_PACKET {
	PacketHeader header;
	int				id;
	std::pair<int, int> position;
	unsigned int move_time;
};

struct SC_ADD_OBJECT_PACKET {
	PacketHeader header;
	int				objectId;
	std::pair<int, int> position;
};

struct SC_REMOVE_OBJECT_PACKET {
	PacketHeader header;
	int				objectId;
};



#pragma pack (pop)

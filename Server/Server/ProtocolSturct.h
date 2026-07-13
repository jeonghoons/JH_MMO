#pragma once

#define PORT_NUM 8888

constexpr int MAX_ROOM_CAPACITY = 100;

constexpr int MAX_CHAT_LEN = 100;
// Packet Key
enum SC_PACKET_LIST : unsigned char
{
	SC_LOGIN, SC_SIGNUP, SC_LOGOUT,
	SC_ADD_OBJECT, SC_REMOVE_OBJECT,
	SC_CHAT,
	SC_MOVE_OBJECT, SC_CMOVE_OBJECT,
	SC_ATTACK, SC_DAMAGE, SC_DEAD
};

enum CS_PACKET_LIST : unsigned char
{
	CS_LOGIN, CS_SIGNUP, CS_LOGOUT,
	CS_CHAT,
	CS_MOVE, CS_CMOVE,
	CS_ENTER_ROOM, CS_LEAVE_ROOM,
	CS_ATTACK,
};

enum class Move_State
{
	NONE, IDLE, RUN, JUMP, ATTACK
};

enum class Object_Type
{
	NONE, Player, Monster, Npc, Item, ENVIRONMENT
};

enum class PlayerType
{
	None,
	Greystone, Sparrow, Gideon, Monster
};

#pragma pack(push, 1)
struct PacketHeader
{
	unsigned short size{};
	unsigned short type{};
};

struct PositionInfo
{
	float x;
	float y;
	float z;
	float yaw;
	float v_x;
	float v_y;
	float v_z;
	Move_State state;
};

struct StatInfo
{
	int maxHp;
	int hp;
	int attackDamage;
	float attackSpeed;
	float moveSpeed;
};

struct ObjectInfo
{
	int id{};
	Object_Type		objectType;
	PlayerType       playerType;
	PositionInfo		position;
	StatInfo			stat;
};
#pragma pack (pop)


#pragma once
#include <atomic>

void InitializeNetwork();
void GetPointCloud(int* size, float** points);

extern int global_delay;
extern std::atomic_int active_clients;

#pragma pack(push, 1)
struct PacketHeader {
    unsigned short size;
    unsigned short type;
};
#pragma pack(pop)
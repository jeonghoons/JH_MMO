#pragma once
class Session;

class PacketHandler {
public:
    static void Process(Session* session, BYTE* buffer, size_t len);
};


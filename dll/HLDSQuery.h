//
// Created by home on 13.02.26.
//

#ifndef HLDS_QUERY_HLDSQUERY_H
#define HLDS_QUERY_HLDSQUERY_H

#include "base.h"

class HLDSQuery {
    std::string ip_gl;
    int sock;
    struct sockaddr_in server_addr;
    static constexpr uint32_t NETWORK_TIMEOUT_MS = 500;

    void drain_socket();
    void parse_info_buffer(uint8_t* buffer, ssize_t res, Gameserver* out_data);

    std::string read_string(uint8_t*& ptr, uint8_t* end);
    ssize_t send_and_receive(const std::vector<uint8_t>& request, uint8_t* buffer, size_t buf_size);
    ssize_t query_with_challenge(uint8_t type, uint8_t* buffer, size_t size);

    template<typename T>
    T read_num(uint8_t*& ptr, uint8_t* end);

public:
    HLDSQuery(const std::string& ip, uint16_t port);

    ~HLDSQuery();

    bool get_info(Gameserver* out_data);

    void get_players(PlayerServerResult *result);

    void get_rules();
};

#endif //HLDS_QUERY_HLDSQUERY_H
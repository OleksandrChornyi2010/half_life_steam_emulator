//
// Created by home on 13.02.26.
//

#ifndef HLDS_QUERY_HLDSQUERY_H
#define HLDS_QUERY_HLDSQUERY_H

//#include <iostream>
//#include <vector>
//#include <string>
//#include <cstring>
//#include <sys/socket.h>
//#include <netinet/in.h>
//#include <arpa/inet.h>
//#include <unistd.h>
//#include <chrono>
//#include <iomanip>

#include "base.h"

class HLDSQuery {
private:
    int sock;
    struct sockaddr_in server_addr;

    void drain_socket();

    std::string read_string(uint8_t*& ptr, uint8_t* end);

    template<typename T>
    T read_num(uint8_t*& ptr, uint8_t* end);

public:
    HLDSQuery(const std::string& ip, uint16_t port);

    ~HLDSQuery();

    ssize_t send_and_receive(const std::vector<uint8_t>& request, uint8_t* buffer, size_t buf_size);

    bool get_info(Gameserver* out_data);

    void get_players();

    void get_rules();

private:
    ssize_t query_with_challenge(uint8_t type, uint8_t* buffer, size_t size);
    void parse_info_buffer(uint8_t* buffer, ssize_t res, Gameserver* out_data);
};


#endif //HLDS_QUERY_HLDSQUERY_H
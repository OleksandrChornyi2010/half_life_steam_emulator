/* Copyright (C) 2026 OleksandrChornyi2010 (SaNNa)
   This file is part of the half_life_steam_emulator

   The half_life_steam_emulator is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 3 of the License, or (at your option) any later version.

   The half_life_steam_emulator is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with the half_life_steam_emulator; if not, see
   <http://www.gnu.org/licenses/>.  */

#ifndef HLDS_QUERY_HLDSQUERY_H
#define HLDS_QUERY_HLDSQUERY_H

#include "base.h"

class HLDSQuery {
    std::string ip_gl;
    int sock;
    struct sockaddr_in server_addr;
    static constexpr uint32_t NETWORK_TIMEOUT_MS = 500;

    void drain_socket();
    void parse_info_buffer(uint8_t *buffer, ssize_t res, Gameserver *out_data);

    std::string read_string(uint8_t *&ptr, uint8_t *end);
    ssize_t send_and_receive(const std::vector<uint8_t> &request, uint8_t *buffer, size_t buf_size);
    ssize_t query_with_challenge(uint8_t type, uint8_t *buffer, size_t size);

    template <typename T>
    T read_num(uint8_t *&ptr, uint8_t *end);

  public:
    HLDSQuery(const std::string &ip, uint16_t port);

    ~HLDSQuery();

    bool get_info(Gameserver *out_data);

    void get_players(PlayerServerResult *result);

    void get_rules();
};

#endif // HLDS_QUERY_HLDSQUERY_H
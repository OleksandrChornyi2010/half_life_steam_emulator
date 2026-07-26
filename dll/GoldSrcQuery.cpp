/* Copyright (C) 2026 OleksandrChornyi2010 (SaNNa)
   This file is part of the half_life_steam_emulator

   The half_life_steam_emulator is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 3 of the License, or (at your option) any later version.

   The half_life_steam_emulator is distributed in the hope that it will be
   useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with the half_life_steam_emulator; if not, see
   <http://www.gnu.org/licenses/>.  */

#include "GoldSrcQuery.h"
#include "steam_matchmaking_servers.h"

struct AutoSocket {
    int fd;
    explicit AutoSocket(int s) : fd(s) {}
    ~AutoSocket() {
        if (is_valid()) {
#ifdef _WIN32
            closesocket(fd);
#else
            close(fd);
#endif
        }
    }
    operator int() const { return fd; }
    bool is_valid() const {
#ifdef _WIN32
        return fd != INVALID_SOCKET;
#else
        return fd >= 0;
#endif
    }
};

GoldSrcQuery::GoldSrcQuery() : is_running(false) {
#ifdef _WIN32
    m_async_sock = INVALID_SOCKET;
#else
    m_async_sock = -1;
#endif

    m_async_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

#ifdef _WIN32
    if (m_async_sock != INVALID_SOCKET) {
#else
    if (m_async_sock >= 0) {
#endif
        setsockopt(m_async_sock, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char *>(&SOCKET_BUFFER_SIZE), sizeof(SOCKET_BUFFER_SIZE));
        setsockopt(m_async_sock, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<const char *>(&SOCKET_BUFFER_SIZE), sizeof(SOCKET_BUFFER_SIZE));

#ifdef _WIN32
        u_long mode = 1;
        ioctlsocket(m_async_sock, FIONBIO, &mode);
#ifndef SIO_UDP_CONNRESET
#define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
#endif
        BOOL bNewBehavior = FALSE;
        DWORD dwBytesReturned = 0;
        WSAIoctl(m_async_sock, SIO_UDP_CONNRESET, &bNewBehavior,
                 sizeof(bNewBehavior), NULL, 0, &dwBytesReturned, NULL, NULL);
#else
        int flags = fcntl(m_async_sock, F_GETFL, 0);
        fcntl(m_async_sock, F_SETFL, flags | O_NONBLOCK);
#endif
        is_running = true;
        worker_thread = std::thread(&GoldSrcQuery::AsyncWorkerLoop, this);
    } else {
        std::cout << "GoldSrcQuery: Failed to create async socket" << std::endl;
    }
}

GoldSrcQuery::~GoldSrcQuery() {
    is_running = false;
    if (worker_thread.joinable()) {
        worker_thread.join();
    }
    if (m_async_sock >= 0) {
#ifdef _WIN32
        closesocket(m_async_sock);
#else
        close(m_async_sock);
#endif
    }
}

std::string GoldSrcQuery::read_string(uint8_t *&ptr, uint8_t *end) {
    if (ptr >= end)
        return "";
    uint8_t *null_pos = std::find(ptr, end, '\0');
    if (null_pos == end) {
        std::string str(reinterpret_cast<char *>(ptr), end - ptr);
        ptr = end;
        return str;
    }
    std::string str(reinterpret_cast<char *>(ptr), null_pos - ptr);
    ptr = null_pos + 1;
    return str;
}

void GoldSrcQuery::parse_info_buffer(uint8_t *buffer, ssize_t res, Gameserver *out_data, const std::string &ip, uint16_t port) {
    uint8_t *ptr = &buffer[4];
    uint8_t *end = buffer + res;
    uint8_t header = read_num<uint8_t>(ptr, end);

    std::cout << "\n=== (Header: 0x" << std::hex << (int)header << std::dec
              << ") ===" << std::endl;

    if (header == 0x6D) { // GoldSrc
        std::cout << "Protocol: GoldSrc (Obsolete)" << std::endl;
        std::string addr = read_string(ptr, end);
        std::cout << "Address: " << addr << std::endl;
        std::string name = read_string(ptr, end);
        std::cout << "Name:    " << name << std::endl;
        std::string map = read_string(ptr, end);
        std::cout << "Map:     " << map << std::endl;
        std::string dir = read_string(ptr, end);
        std::cout << "Folder:  " << dir << std::endl;
        std::string game = read_string(ptr, end);
        std::cout << "Game:    " << game << std::endl;

        uint8_t players = read_num<uint8_t>(ptr, end);
        uint8_t max_players = read_num<uint8_t>(ptr, end);
        std::cout << "Players: " << (int)players << "/" << (int)max_players
                  << std::endl;

        uint8_t prot_ver = read_num<uint8_t>(ptr, end);
        std::cout << "Protocol version: " << (int)prot_ver << std::endl;

        uint8_t server_type = read_num<uint8_t>(ptr, end);
        std::cout << "Server type: " << server_type << std::endl;

        uint8_t environment = read_num<uint8_t>(ptr, end);
        std::cout << "Env: " << environment << std::endl;

        uint8_t visibility = read_num<uint8_t>(ptr, end);
        std::cout << "Visibility: " << (int)visibility << std::endl;

        uint8_t is_mod = read_num<uint8_t>(ptr, end);
        std::cout << "Is mod: " << (int)is_mod << std::endl;

        if (is_mod == 1) {
            std::string mod_link = read_string(ptr, end);
            std::cout << "Mod link: " << mod_link << std::endl;
            std::string mod_download = read_string(ptr, end);
            std::cout << "mod_download: " << mod_download << std::endl;

            read_num<uint8_t>(ptr, end);
            uint32_t mod_version = read_num<uint32_t>(ptr, end);
            std::cout << "mod_version: " << mod_version << std::endl;
            uint32_t mod_size = read_num<uint32_t>(ptr, end);
            std::cout << "mod_size: " << mod_size << std::endl;
            uint8_t mod_type = read_num<uint8_t>(ptr, end);
            std::cout << "mod_type: " << mod_type << std::endl;
            uint8_t mod_dll = read_num<uint8_t>(ptr, end);
            std::cout << "mod_dll: " << mod_dll << std::endl;
        }

        uint8_t vac = read_num<uint8_t>(ptr, end);
        std::cout << "vac: " << (int)vac << std::endl;

        uint8_t bots = read_num<uint8_t>(ptr, end);
        std::cout << "bots: " << (int)bots << std::endl;

        out_data->set_server_name(name);
        out_data->set_map_name(map);
        out_data->set_mod_dir(dir);
        out_data->set_num_players(players);
        out_data->set_max_player_count(max_players);
        out_data->set_version(prot_ver);
        out_data->set_password_protected(visibility);
        out_data->set_secure(vac);
        out_data->set_game_description(game);
        out_data->set_bot_player_count(bots);

    } else if (header == 0x49) { // Source
        std::cout << "Protocol: Source (Modern)" << std::endl;
        uint8_t prot_ver = read_num<uint8_t>(ptr, end);
        std::cout << "Protocol version: " << (int)prot_ver << std::endl;
        std::string name = read_string(ptr, end);
        std::cout << "Name:    " << name << std::endl;
        std::string map = read_string(ptr, end);
        std::cout << "Map:     " << map << std::endl;
        std::string dir = read_string(ptr, end);
        std::cout << "Folder:  " << dir << std::endl;
        std::string game = read_string(ptr, end);
        std::cout << "Game:    " << game << std::endl;

        uint16_t app_id = read_num<uint16_t>(ptr, end);
        std::cout << "AppID:   " << app_id << std::endl;

        uint8_t players = read_num<uint8_t>(ptr, end);
        uint8_t max_players = read_num<uint8_t>(ptr, end);
        std::cout << "Players: " << (int)players << "/" << (int)max_players
                  << std::endl;

        uint8_t bots = read_num<uint8_t>(ptr, end);
        std::cout << "bots: " << (int)bots << std::endl;

        uint8_t server_type = read_num<uint8_t>(ptr, end);
        std::cout << "Server type: " << server_type << std::endl;

        uint8_t environment = read_num<uint8_t>(ptr, end);
        std::cout << "Env: " << environment << std::endl;

        uint8_t visibility = read_num<uint8_t>(ptr, end);
        std::cout << "Visibility: " << (int)visibility << std::endl;

        uint8_t vac = read_num<uint8_t>(ptr, end);
        std::cout << "vac: " << (int)vac << std::endl;

        std::string game_ver = read_string(ptr, end);
        std::cout << "game_ver: " << game_ver << std::endl;
        std::cout << "Address: " << ip << " port: " << port << std::endl;

        out_data->set_server_name(name);
        out_data->set_map_name(map);
        out_data->set_mod_dir(dir);
        out_data->set_num_players(players);
        out_data->set_max_player_count(max_players);
        out_data->set_version(prot_ver);
        out_data->set_password_protected(visibility);
        out_data->set_secure(vac);
        out_data->set_game_description(game);
        out_data->set_appid(app_id);
    } else {
        std::cout << "Unknown header type." << std::endl;
    }
}

void GoldSrcQuery::GetServersFromMasterServer(const std::string &masterDomain, int masterPort, const std::string &filter, std::vector<ServerItem> &out, std::recursive_mutex &out_mutex, std::atomic<bool> &cancel_flag) {
    AutoSocket master_sock(socket(AF_INET, SOCK_DGRAM, 0));
    if (!master_sock.is_valid())
        return;

#ifdef _WIN32
    DWORD timeout = REQUEST_TIMEOUT;
    setsockopt(master_sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char *>(&timeout), sizeof(timeout));
#else
    struct timeval tv;
    tv.tv_sec = REQUEST_TIMEOUT / 1000;
    tv.tv_usec = (REQUEST_TIMEOUT % 1000) * 1000;
    setsockopt(master_sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char *>(&tv), sizeof(tv));
#endif

    struct hostent *host = gethostbyname(masterDomain.c_str());
    if (!host)
        return;

    struct sockaddr_in masterAddr;
    memset(&masterAddr, 0, sizeof(masterAddr));
    masterAddr.sin_family = AF_INET;
    masterAddr.sin_port = htons(masterPort);
    memcpy(&masterAddr.sin_addr, host->h_addr, host->h_length);

    std::string seed = "0.0.0.0:0";

    while (!cancel_flag.load()) {
        std::vector<uint8_t> request;
        request.reserve(64);
        request.push_back(0x31);
        request.push_back(0xFF);
        for (char c : seed)
            request.push_back(static_cast<uint8_t>(c));
        request.push_back(0x00);
        for (char c : filter)
            request.push_back(static_cast<uint8_t>(c));
        request.push_back(0x00);

        sendto(master_sock, reinterpret_cast<const char *>(request.data()),
               request.size(), 0, (struct sockaddr *)&masterAddr,
               sizeof(masterAddr));

        uint8_t buffer[4096];
        socklen_t addrLen = sizeof(masterAddr);
        int received = recvfrom(master_sock, reinterpret_cast<char *>(buffer), sizeof(buffer), 0, (struct sockaddr *)&masterAddr, &addrLen);

        if (received < 6 || buffer[0] != 0xFF || buffer[4] != 0x66)
            break;

        int offset = 6;
        std::string lastIp;
        std::vector<ServerItem> batch;

        while (offset + 6 <= received) {
            uint8_t ip1 = buffer[offset++];
            uint8_t ip2 = buffer[offset++];
            uint8_t ip3 = buffer[offset++];
            uint8_t ip4 = buffer[offset++];
            uint16_t sPort = (static_cast<uint16_t>(buffer[offset++]) << 8) |
                             static_cast<uint16_t>(buffer[offset++]);

            std::string addr = std::to_string(ip1) + "." + std::to_string(ip2) +
                               "." + std::to_string(ip3) + "." +
                               std::to_string(ip4);
            lastIp = addr + ":" + std::to_string(sPort);

            if (lastIp == "0.0.0.0:0") {
                std::lock_guard<std::recursive_mutex> lock(out_mutex);
                out.insert(out.end(), batch.begin(), batch.end());
                return;
            }
            batch.push_back({addr, sPort});
        }

        if (!batch.empty()) {
            std::lock_guard<std::recursive_mutex> lock(out_mutex);
            out.insert(out.end(), batch.begin(), batch.end());
        }

        if (lastIp.empty() || seed == lastIp)
            break;
        seed = lastIp;
    }
}

void GoldSrcQuery::GetServerInfo(const std::string &ip, uint16_t port, std::function<void(const Gameserver &)> on_response) {
    if (!on_response)
        return;

#if 0 // Set to 0 to restore asynchronous queue behavior
    // Synchronous temporary mode for reliability testing
    AutoSocket sock(socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP));
    if (!sock.is_valid()) {
        Gameserver failed_gs;
        failed_gs.set_had_successful_response(false);
        on_response(failed_gs);
        return;
    }

    // Set receive timeout (2.5 seconds)
    struct timeval tv;
    tv.tv_sec = 2;
    tv.tv_usec = 500000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char *>(&tv), sizeof(tv));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);

    std::vector<uint8_t> info_req = {0xFF, 0xFF, 0xFF, 0xFF, 0x54, 'S', 'o', 'u', 'r', 'c', 'e', ' ', 'E', 'n', 'g', 'i', 'n', 'e', ' ', 'Q', 'u', 'e', 'r', 'y', 0x00};

    auto start_time = std::chrono::steady_clock::now();
    sendto(sock, reinterpret_cast<const char *>(info_req.data()), info_req.size(), 0, (struct sockaddr *)&addr, sizeof(addr));

    uint8_t buffer[2048];
    ssize_t received = recvfrom(sock, reinterpret_cast<char *>(buffer), sizeof(buffer), 0, nullptr, nullptr);

    // Handle A2S_INFO challenge protocol (0x41) if the server requests it
    if (received > 5 && buffer[4] == 0x41) {
        std::vector<uint8_t> chal_req = {0xFF, 0xFF, 0xFF, 0xFF, 0x54, 'S', 'o', 'u', 'r', 'c', 'e', ' ', 'E', 'n', 'g', 'i', 'n', 'e', ' ', 'Q', 'u', 'e', 'r', 'y', 0x00};
        for (int i = 0; i < 4; i++) {
            chal_req.push_back(buffer[5 + i]);
        }
        sendto(sock, reinterpret_cast<const char *>(chal_req.data()), chal_req.size(), 0, (struct sockaddr *)&addr, sizeof(addr));
        received = recvfrom(sock, reinterpret_cast<char *>(buffer), sizeof(buffer), 0, nullptr, nullptr);
    }

    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();

    // Verify response header (Source 0x49 or GoldSrc 0x6D)
    if (received > 4 && (buffer[4] == 0x49 || buffer[4] == 0x6D)) {
        Gameserver gs;
        parse_info_buffer(buffer, received, &gs, ip, port);
        gs.set_latency(static_cast<uint32_t>(duration));
        gs.set_had_successful_response(true);
        gs.set_ip(ntohl(addr.sin_addr.s_addr));
        gs.set_port(port);
        gs.set_query_port(port);

        on_response(gs);
    } else {
        Gameserver failed_gs;
        failed_gs.set_had_successful_response(false);
        on_response(failed_gs);
    }
#else
    if (m_async_sock < 0 || !is_running)
        return;

    uint32_t ip_net;
    inet_pton(AF_INET, ip.c_str(), &ip_net);
    uint16_t port_net = htons(port);

    std::lock_guard<std::mutex> lock(queue_mutex);
    request_queue.push_back({ip_net, port_net, on_response});
#endif
}

void GoldSrcQuery::AsyncWorkerLoop() {
    std::unordered_map<uint64_t, PendingState> pending;
    const std::vector<uint8_t> info_req = {0xFF, 0xFF, 0xFF, 0xFF, 0x54, 'S', 'o', 'u', 'r', 'c', 'e', ' ', 'E', 'n', 'g', 'i', 'n', 'e', ' ', 'Q', 'u', 'e', 'r', 'y', 0x00};

    while (is_running) {
        if (pending.size() < MAX_ASYNC_CONCURRENT_QUERIES) {
            std::lock_guard<std::mutex> lock(queue_mutex);
            size_t sent_count = 0;
            while (sent_count < request_queue.size() && pending.size() < MAX_ASYNC_CONCURRENT_QUERIES) {
                const auto &req = request_queue[sent_count];
                uint64_t key = (static_cast<uint64_t>(req.ip_net) << 32) | req.port_net;
                if (pending.find(key) == pending.end()) {
                    struct sockaddr_in addr;
                    memset(&addr, 0, sizeof(addr));
                    addr.sin_family = AF_INET;
                    addr.sin_port = req.port_net;
                    addr.sin_addr.s_addr = req.ip_net;

                    sendto(m_async_sock, reinterpret_cast<const char *>(info_req.data()), info_req.size(), 0, (struct sockaddr *)&addr, sizeof(addr));

                    pending[key] = {std::chrono::steady_clock::now(), req.callback};
                } else {
                    std::cout << "Dropping duplicate server" << std::endl;
                    Gameserver dup_gs;
                    dup_gs.set_had_successful_response(false);
                    req.callback(dup_gs);
                }

                sent_count++;
            }
            if (sent_count > 0) {
                request_queue.erase(request_queue.begin(), request_queue.begin() + sent_count);
            }
        }

        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(m_async_sock, &read_fds);

        struct timeval tv;
        tv.tv_sec = LOOP_INTERVAL_SECONDS;
        tv.tv_usec = LOOP_INTERVAL_MICROSECONDS;

        int ret = select(m_async_sock + 1, &read_fds, nullptr, nullptr, &tv);

        std::vector<std::pair<std::function<void(const Gameserver &)>, Gameserver>> ready_callbacks;
        if (ret > 0 && FD_ISSET(m_async_sock, &read_fds)) {
            while (true) {
                uint8_t buffer[2048];
                struct sockaddr_in from;
                socklen_t from_len = sizeof(from);

                int received = recvfrom(
                    m_async_sock, reinterpret_cast<char *>(buffer),
                    sizeof(buffer), 0, (struct sockaddr *)&from, &from_len);
                if (received < 0) {
#ifdef _WIN32
                    int err = WSAGetLastError();
                    if (err == WSAEWOULDBLOCK) {
                        break;
                    }
                    continue;
#else
                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        break;
                    }
                    continue;
#endif
                }

                if (received < 5)
                    continue;

                uint64_t key =
                    (static_cast<uint64_t>(from.sin_addr.s_addr) << 32) |
                    from.sin_port;
                auto it = pending.find(key);
                if (it == pending.end())
                    continue;

                uint8_t header = buffer[4];

                if (header == 0x41) {
                    std::vector<uint8_t> chal_req = {0xFF, 0xFF, 0xFF, 0xFF, 0x54, 'S', 'o', 'u', 'r', 'c', 'e', ' ', 'E', 'n', 'g', 'i', 'n', 'e', ' ', 'Q', 'u', 'e', 'r', 'y', 0x00};
                    for (int i = 0; i < 4; i++)
                        chal_req.push_back(buffer[5 + i]);
                    sendto(m_async_sock, reinterpret_cast<const char *>(chal_req.data()), chal_req.size(), 0, (struct sockaddr *)&from, sizeof(from));
                    it->second.start = std::chrono::steady_clock::now();
                } else if (header == 0x49 || header == 0x6D) {
                    // std::cout << "PENDING_SIZE: " << pending.size() << std::endl;
                    auto now = std::chrono::steady_clock::now();
                    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - it->second.start).count();

                    char ip_str[INET_ADDRSTRLEN];
                    inet_ntop(AF_INET, &(from.sin_addr), ip_str, INET_ADDRSTRLEN);

                    Gameserver gs;
                    parse_info_buffer(buffer, received, &gs, std::string(ip_str), ntohs(from.sin_port));
                    gs.set_latency(static_cast<uint32_t>(duration));
                    gs.set_had_successful_response(true);
                    gs.set_ip(ntohl(from.sin_addr.s_addr));
                    gs.set_port(ntohs(from.sin_port));
                    gs.set_query_port(ntohs(from.sin_port));
                    ready_callbacks.push_back({it->second.callback, gs});
                    pending.erase(it);
                }
            }
        }

        for (const auto &item : ready_callbacks) {
            callback_worker.EnqueueTask([cb = item.first, gs = item.second]() {
                cb(gs);
            });
        }

        auto now = std::chrono::steady_clock::now();
        for (auto it = pending.begin(); it != pending.end();) {
            if (std::chrono::duration_cast<std::chrono::milliseconds>(now - it->second.start).count() > REQUEST_TIMEOUT) {
                Gameserver failed_gs;
                failed_gs.set_had_successful_response(false);
                it->second.callback(failed_gs);
                it = pending.erase(it);
            } else {
                ++it;
            }
        }
    }
}

ssize_t GoldSrcQuery::query_with_challenge_sync(int sock, const sockaddr_in &addr, uint8_t type, uint8_t *buffer, size_t size) {
    std::vector<uint8_t> req = {0xFF, 0xFF, 0xFF, 0xFF, type, 0xFF, 0xFF, 0xFF, 0xFF};
    sendto(sock, reinterpret_cast<const char *>(req.data()), req.size(), 0, (struct sockaddr *)&addr, sizeof(addr));

    ssize_t res = recvfrom(sock, reinterpret_cast<char *>(buffer), size, 0, nullptr, nullptr);

    if (res > 5 && buffer[4] == 0x41) {
        req.clear();
        req.insert(req.end(), {0xFF, 0xFF, 0xFF, 0xFF, type});
        for (int i = 0; i < 4; i++)
            req.push_back(buffer[5 + i]);

        sendto(sock, reinterpret_cast<const char *>(req.data()), req.size(), 0,
               (struct sockaddr *)&addr, sizeof(addr));
        res = recvfrom(sock, reinterpret_cast<char *>(buffer), size, 0, nullptr,
                       nullptr);
    }
    return res;
}

void GoldSrcQuery::GetServerPlayers(const std::string &ip, uint16_t port, PlayerServerResult *result) {
    if (!result)
        return;

    AutoSocket sock(socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP));
    if (!sock.is_valid())
        return;

#ifdef _WIN32
    DWORD timeout = REQUEST_TIMEOUT;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char *>(&timeout), sizeof(timeout));
#else
    struct timeval tv;
    tv.tv_sec = REQUEST_TIMEOUT / 1000;
    tv.tv_usec = (REQUEST_TIMEOUT % 1000) * 1000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char *>(&tv), sizeof(tv));
#endif

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);

    uint8_t buffer[8192];
    ssize_t res = query_with_challenge_sync(sock, addr, 0x55, buffer, sizeof(buffer));

    if (res <= 5 || buffer[4] != 0x44) {
        std::cout << "Players: Failed (Header 0x" << std::hex
                  << (res > 4 ? (int)buffer[4] : 0) << std::dec << ")"
                  << std::endl;
        return;
    }

    uint8_t *ptr = &buffer[5];
    uint8_t *end = buffer + res;
    uint8_t count = read_num<uint8_t>(ptr, end);

    std::cout << "\n=== [PLAYERS] (" << (int)count << ") ===" << std::endl;
    for (int i = 0; i < count; i++) {
        read_num<uint8_t>(ptr, end); // Index
        std::string name = read_string(ptr, end);
        int32_t score = read_num<int32_t>(ptr, end);
        float time = read_num<float>(ptr, end);

        result->players.push_back({name, score, time});
        std::cout << std::setw(2) << i << ". " << std::setw(20)
                  << (name.empty() ? "-" : name) << " | Frags: " << std::setw(3)
                  << score << " | Time: " << (int)time << "s" << std::endl;
    }
    result->finished = true;
}

void GoldSrcQuery::GetServerRules(const std::string &ip, uint16_t port) {
    AutoSocket sock(socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP));
    if (!sock.is_valid())
        return;

#ifdef _WIN32
    DWORD timeout = REQUEST_TIMEOUT;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char *>(&timeout), sizeof(timeout));
#else
    struct timeval tv;
    tv.tv_sec = REQUEST_TIMEOUT / 1000;
    tv.tv_usec = (REQUEST_TIMEOUT % 1000) * 1000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char *>(&tv), sizeof(tv));
#endif

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);

    uint8_t buffer[16384];
    ssize_t res =
        query_with_challenge_sync(sock, addr, 0x56, buffer, sizeof(buffer));

    if (res <= 5 || buffer[4] != 0x45)
        return;

    uint8_t *ptr = &buffer[5];
    uint8_t *end = buffer + res;
    uint16_t count = read_num<uint16_t>(ptr, end);

    std::cout << "\n=== [RULES] (" << count << ") ===" << std::endl;
    for (int i = 0; i < count; i++) {
        std::string k = read_string(ptr, end);
        std::string v = read_string(ptr, end);
        std::cout << std::setw(20) << k << " = " << v << std::endl;
    }
}
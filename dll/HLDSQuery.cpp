//
// Created by home on 13.02.26.
//

#include "HLDSQuery.h"

// Discard all pending packets in the socket buffer
void HLDSQuery::drain_socket() {
    uint8_t dummy[2048];
    struct sockaddr_in from;
    socklen_t len = sizeof(from);
    while (recvfrom(sock, dummy, sizeof(dummy), MSG_DONTWAIT, (struct sockaddr*)&from, &len) > 0) {
        // Just dropping packets
    }
}

std::string HLDSQuery::read_string(uint8_t*& ptr, uint8_t* end) {
    if (ptr >= end) return "";
    std::string str = (char*)ptr;
    ptr += str.length() + 1;
    return str;
}

template<typename T>
T HLDSQuery::read_num(uint8_t*& ptr, uint8_t* end) {
    if (ptr + sizeof(T) > end) return 0;
    T val = *(T*)ptr;
    ptr += sizeof(T);
    return val;
}

HLDSQuery::HLDSQuery(const std::string& ip, uint16_t port) {
    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    struct timeval tv;
    tv.tv_sec = 1;
    tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    inet_pton(AF_INET, ip.c_str(), &server_addr.sin_addr);
}

HLDSQuery::~HLDSQuery() { close(sock); }

ssize_t HLDSQuery::send_and_receive(const std::vector<uint8_t>& request, uint8_t* buffer, size_t buf_size) {
    sendto(sock, request.data(), request.size(), 0, (struct sockaddr*)&server_addr, sizeof(server_addr));
    return recvfrom(sock, buffer, buf_size, 0, nullptr, nullptr);
}

void HLDSQuery::parse_info_buffer(uint8_t* buffer, ssize_t res, Gameserver* out_data) { // TODO: Make getting a reference, not a pointer
    uint8_t* ptr = &buffer[4];
    uint8_t* end = buffer + res;
    uint8_t header = read_num<uint8_t>(ptr, end);

    std::cout << "\n=== (Header: 0x" << std::hex << (int)header << std::dec << ") ===" << std::endl;

    if (header == 0x6D) { // GoldSrc
        std::cout << "Protocol: GoldSrc (Obsolete)" << std::endl;
        std::string addr = read_string(ptr, end);
        std::cout << "Address: " << addr << std::endl;

        std::string name = read_string(ptr, end);
        std::cout << "Name:    " << read_string(ptr, end) << std::endl;

        std::string map = read_string(ptr, end);
        std::cout << "Map:     " << map << std::endl;

        std::string dir = read_string(ptr, end);
        std::cout << "Folder:     " << dir << std::endl;

        std::string game = read_string(ptr, end);
        std::cout << "Game:     " << game << std::endl;

        uint8_t players = read_num<uint8_t>(ptr, end);
        uint8_t max_players = read_num<uint8_t>(ptr, end);
        std::cout << "Players: " << (int)players << "/" << (int)max_players << std::endl;

        uint8_t prot_ver = read_num<uint8_t>(ptr, end);
        std::cout << "Protocol version: " << (int)prot_ver << std::endl;

        uint8_t server_type = read_num<uint8_t>(ptr, end);
        std::cout << "Server type" << server_type << std::endl;

        uint8_t environment = read_num<uint8_t>(ptr, end);
        std::cout << "Env: " << environment << std::endl;

        uint8_t visibility = read_num<uint8_t>(ptr, end); // Is password protected
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

        uint8_t vac  = read_num<uint8_t>(ptr, end);
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
        out_data->set_appid(Local_Storage::i_appid);
    }
    else if (header == 0x49) { // Source
        std::cout << "Protocol: Source (Modern)" << std::endl;

        uint8_t prot_ver = read_num<uint8_t>(ptr, end);
        std::cout << "Protocol version: " << (int)prot_ver << std::endl;

        std::string name = read_string(ptr, end);
        std::cout << "Name:    " << name << std::endl;

        std::string map = read_string(ptr, end);
        std::cout << "Map:     " << map << std::endl;

        std::string dir = read_string(ptr, end);
        std::cout << "Folder:     " << dir << std::endl;

        std::string game = read_string(ptr, end);
        std::cout << "Game:     " << game << std::endl;

        uint16_t app_id = read_num<uint16_t>(ptr, end);
        std::cout << "AppID:   " << app_id << std::endl;

        uint8_t players = read_num<uint8_t>(ptr, end);
        uint8_t max_players = read_num<uint8_t>(ptr, end);
        std::cout << "Players: " << (int)players << "/" << (int)max_players << std::endl;

        uint8_t bots = read_num<uint8_t>(ptr, end);
        std::cout << "bots: " << (int)bots << std::endl;

        uint8_t server_type = read_num<uint8_t>(ptr, end);
        std::cout << "Server type: " << server_type << std::endl;

        uint8_t environment = read_num<uint8_t>(ptr, end);
        std::cout << "Env: " << environment << std::endl;

        uint8_t visibility = read_num<uint8_t>(ptr, end); // Is password protected
        std::cout << "Visibility: " << (int)visibility << std::endl;

        uint8_t vac  = read_num<uint8_t>(ptr, end);
        std::cout << "vac: " << (int)vac << std::endl;

        std::string game_ver  = read_string(ptr, end);
        std::cout << "game_ver: " << game_ver << std::endl;

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

bool HLDSQuery::get_info(Gameserver* out_data) {
    drain_socket(); // TODO: Check if draining socket needed

    // A2S_INFO request
    std::vector<uint8_t> req = {0xFF, 0xFF, 0xFF, 0xFF, 0x54, 'S', 'o', 'u', 'r', 'c', 'e', ' ', 'E', 'n', 'g', 'i', 'n', 'e', ' ', 'Q', 'u', 'e', 'r', 'y', 0x00};

    // Send request once
    auto start_time = std::chrono::high_resolution_clock::now();
    sendto(sock, req.data(), req.size(), 0, (struct sockaddr*)&server_addr, sizeof(server_addr));

    uint8_t buffer[2048];
    // Try to read first packet (GoldSrc)
    ssize_t goldsrc_response = recvfrom(sock, buffer, sizeof(buffer), 0, nullptr, nullptr);
    auto end_time = std::chrono::high_resolution_clock::now();
    // Try to read second response (Source)
    ssize_t source_response = recvfrom(sock, buffer, sizeof(buffer), 0, nullptr, nullptr);

    if (source_response > 0) {
        end_time = std::chrono::high_resolution_clock::now();
    }

    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    uint32_t ping = static_cast<uint32_t>(duration.count());

    if (source_response > 5) { // Prioritize source response
        parse_info_buffer(buffer, source_response, out_data);
    }
    else if (goldsrc_response > 5) {
        std::cout << "\n[Note] Second packet didn't arrive (Timeout or single-protocol server)." << std::endl;
        parse_info_buffer(buffer, goldsrc_response, out_data);
    }
    else {
        std::cout << "No response from server." << std::endl;
        out_data->set_had_successful_response(false);
        return false;
    }
    out_data->set_latency(ping);
    out_data->set_had_successful_response(true);
    return true;
}

void HLDSQuery::get_players(PlayerServerResult *result) {
    if (!result) return;
    uint8_t buffer[8192];
    drain_socket();
    ssize_t res = query_with_challenge(0x55, buffer, sizeof(buffer));
    
    if (res <= 5 || buffer[4] != 0x44) {
        std::cout << "Players: Failed (Header 0x" << std::hex << (int)buffer[4] << std::dec << ")" << std::endl;
        return;
    }

    uint8_t* ptr = &buffer[5];
    uint8_t* end = buffer + res;
    uint8_t count = read_num<uint8_t>(ptr, end);

    std::cout << "\n=== [PLAYERS] (" << (int)count << ") ===" << std::endl;
    for (int i = 0; i < count; i++) {
        read_num<uint8_t>(ptr, end); // Index
        std::string name = read_string(ptr, end);
        int32_t score = read_num<int32_t>(ptr, end);
        float time = read_num<float>(ptr, end);

        result->players.push_back({name, score, time});
        std::cout << std::setw(2) << i << ". " << std::setw(20) << (name.empty() ? "-" : name)
                  << " | Frags: " << std::setw(3) << score << " | Time: " << (int)time << "s" << std::endl;
    }
    result->finished = true;
}

void HLDSQuery::get_rules() {
    uint8_t buffer[16384];
    drain_socket();
    ssize_t res = query_with_challenge(0x56, buffer, sizeof(buffer));

    if (res <= 5 || buffer[4] != 0x45) return;

    uint8_t* ptr = &buffer[5];
    uint8_t* end = buffer + res;
    uint16_t count = read_num<uint16_t>(ptr, end);

    std::cout << "\n=== [RULES] (" << count << ") ===" << std::endl;
    for (int i = 0; i < count; i++) {
        std::string k = read_string(ptr, end);
        std::string v = read_string(ptr, end);
        std::cout << std::setw(20) << k << " = " << v << std::endl;
    }
}

ssize_t HLDSQuery::query_with_challenge(uint8_t type, uint8_t* buffer, size_t size) {
    std::vector<uint8_t> req = {0xFF, 0xFF, 0xFF, 0xFF, type, 0xFF, 0xFF, 0xFF, 0xFF};
    ssize_t res = send_and_receive(req, buffer, size);

    if (res > 5 && buffer[4] == 0x41) {
        req.clear();
        req.insert(req.end(), {0xFF, 0xFF, 0xFF, 0xFF, type});
        for (int i = 0; i < 4; i++) req.push_back(buffer[5 + i]);
        res = send_and_receive(req, buffer, size);
    }
    return res;
}
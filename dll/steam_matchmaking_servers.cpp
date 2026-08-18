/* Copyright (C) 2019 Mr Goldberg
   Copyright (C) 2026 OleksandrChornyi2010 (SaNNa)
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

#include "steam_matchmaking_servers.h"

static void network_callback(void *object, Common_Message *msg) {
    PRINT_DEBUG("steam_matchmaking_servers_callback\n");

    Steam_Matchmaking_Servers *obj = (Steam_Matchmaking_Servers *)object;
    obj->Callback(msg);
}

Steam_Matchmaking_Servers::Steam_Matchmaking_Servers(class Settings *settings, class Networking *network) {
    this->settings = settings;
    this->network = network;
    this->network->setCallback(CALLBACK_ID_GAMESERVER, (uint64)0, &network_callback, this);
}

void Steam_Matchmaking_Servers::ProcessLANServerList(HServerListRequest id, size_t request_index) {
    if (!gameservers_lan.empty()) {
        gameservers.insert(gameservers.end(), gameservers_lan.begin(), gameservers_lan.end());
    }
    if (request_index < requests.size() && requests[request_index].id == id) {
        requests[request_index].finished_pushing = true;
    }
}

VDFNode Steam_Matchmaking_Servers::ConvertToNode() {
    VDFNode root;
    root.name = "root";

    auto &filters = root.children["Filters"];
    filters.name = "Filters";

    if (favorite_servers.size() == 0) {
        ParseServersFile(eFavoritesServer, favorite_servers);
    }
    auto &favoritesNode = filters.children["favorites"];
    favoritesNode.name = "favorites";

    if (history_servers.size() == 0) {
        ParseServersFile(eHistoryServer, history_servers);
    }
    auto &historyNode = filters.children["history"];
    historyNode.name = "history";

    // favorites
    int fav_i = 1;
    for (auto &fav_item : favorite_servers) {
        VDFNode item;
        std::string index_str = std::to_string(fav_i);
        item.name = index_str;

        std::string full_addr = fav_item.ip + ":" + std::to_string(fav_item.port);
        item.values["name"] = full_addr;
        item.values["address"] = full_addr;
        item.values["LastPlayed"] = std::to_string(fav_item.last_played);
        item.values["appid"] = std::to_string(Local_Storage::i_appid);
        item.values["accountid"] = "0";

        favoritesNode.children[index_str] = item;
        fav_i++;
    }

    // history
    int his_i = 1;
    for (auto &his_item : history_servers) {
        VDFNode item;
        std::string index_str = std::to_string(his_i);
        item.name = index_str;

        std::string full_addr = his_item.ip + ":" + std::to_string(his_item.port);
        item.values["name"] = full_addr;
        item.values["address"] = full_addr;
        item.values["LastPlayed"] = std::to_string(his_item.last_played);
        item.values["appid"] = std::to_string(Local_Storage::i_appid);
        item.values["accountid"] = "0";

        historyNode.children[index_str] = item;
        his_i++;
    }
    return root;
}

void Steam_Matchmaking_Servers::ParseMasterServersFile(std::vector<MasterServerItem> &vec) {
    std::string path = Local_Storage::get_master_servers_file_path();
    std::cout << "Save dir master: " << path << std::endl;
    std::string root_name = "MasterServers";
    std::string category_name = "hl1";
    VDFNode root = VDFParser::parse(path);

    if (root.hasChild(root_name)) {
        auto &masterServers = root.children[root_name];
        if (masterServers.hasChild(category_name)) {
            auto &category = masterServers.children[category_name];
            for (auto &[key, child] : category.children) {
                if (child.hasValue("addr")) {
                    auto &addr = child.values["addr"];

                    bool has_space = std::any_of(addr.begin(), addr.end(), [](unsigned char c) {
                        return std::isspace(c);
                    });

                    if (has_space) {
                        std::cout << "Skipped invalid address (contains spaces): " << addr << std::endl;
                        continue;
                    }
                    // Find the position of the colon
                    size_t colon_pos = addr.find(':');

                    if (colon_pos == std::string::npos) {
                        vec.push_back({addr, 27010});
                        continue;
                    }

                    std::string ip = addr.substr(0, colon_pos);

                    std::string port_str = addr.substr(colon_pos + 1);
                    try {
                        uint16_t port = std::stoi(port_str);
                        vec.push_back({ip, port});
                    } catch (const std::exception &e) {
                        std::cout << "Skipped invalid address (invalid port): " << addr << std::endl;
                    }
                }
            }
        }
    } else {
        VDFNode master_servers_node;
        master_servers_node.name = root_name;
        root.children[root_name] = master_servers_node;

        VDFNode category_node;
        category_node.name = category_name;
        root.children[root_name].children[category_name] = category_node;

        VDFNode master_server;
        master_server.name = "0";
        master_server.values["addr"] = "master server ip/domain goes here";
        root.children[root_name].children[category_name].children["0"] = master_server;

        VDFParser::write(path, root);
    }
}

void Steam_Matchmaking_Servers::GetInternetServers(HServerListRequest id, EMatchMakingType type, std::string filters) {
    if (master_servers.empty()) {
        ParseMasterServersFile(master_servers);
    }

    {
        std::lock_guard<std::recursive_mutex> lock(global_mutex);
        for (auto &req : requests) {
            if (req.id == id) {
                req.pending_responses++;
                break;
            }
        }
    }

    std::vector<std::future<void>> futures;
    for (auto &ms : master_servers) {
        {
            std::lock_guard<std::recursive_mutex> lock(global_mutex);
            bool is_cancelled = false;
            for (auto &req : requests) {
                if (req.id == id) {
                    if (req.cancelled) {
                        is_cancelled = true;
                    }
                    break;
                }
            }
            if (is_cancelled)
                break;
        }
        futures.push_back(std::async(std::launch::async, [this, ms, type, id, filters]() {
            std::cout << "Processing MS: " << ms.ip << ":" << ms.port << std::endl;
            ProcessMasterServer(id, type, ms.ip, ms.port, filters);
        }));
    }

    for (auto &f : futures) {
        f.get();
    }
    std::lock_guard<std::recursive_mutex> lock(global_mutex);
    for (auto &req : requests) {
        if (req.id == id) {
            req.pending_responses--;
            if (req.pending_responses <= 0) {
                req.finished_pushing = true;
                std::cout << "Master server search complete. Marking request as finished: " << id << std::endl;
            }
            break;
        }
    }
}

void Steam_Matchmaking_Servers::ProcessMasterServer(HServerListRequest id, EMatchMakingType type, std::string address, int port, std::string filter) {
    std::vector<ServerItem> servers;
    std::recursive_mutex servers_mutex;
    std::atomic<bool> cancel_flag{false};
    auto future = std::async(std::launch::async, GoldSrcQuery::GetServersFromMasterServer, address, port, filter, std::ref(servers), std::ref(servers_mutex), std::ref(cancel_flag));
    bool is_task_finished = false;
    while (!is_task_finished) {
        is_task_finished = (future.wait_for(std::chrono::milliseconds(50)) == std::future_status::ready);
        std::vector<ServerItem> batch;
        {
            std::lock_guard<std::recursive_mutex> lock(servers_mutex);
            if (!servers.empty()) {
                batch = std::move(servers);
            } else {
                continue;
            }
        }

        {
            std::lock_guard<std::recursive_mutex> lock(global_mutex);
            for (auto &req : requests) {
                if (req.id == id) {
                    if (req.cancelled) {
                        cancel_flag.store(true);
                        return;
                    } else {
                        req.pending_responses += batch.size();
                    }
                    break;
                }
            }
        }
        for (auto &s : batch) {
            {
                std::lock_guard<std::recursive_mutex> lock(global_mutex);
                for (auto &req : requests) {
                    if (req.id == id) {
                        if (req.cancelled) {
                            cancel_flag.store(true);
                            return;
                        }
                        break;
                    }
                }
            }
            std::string server_ip = s.ip;
            uint16_t server_port = s.port;
            uint32_t last_played = s.last_played;
            m_goldsrc_query.GetServerInfo(server_ip, server_port, [this, server_ip, server_port, last_played, type, id](const Gameserver &_gs) {
                this->OnInternetServerInfoReceived(_gs, server_ip, server_port, last_played, type, id);
            });
        }
    }
}

void Steam_Matchmaking_Servers::OnInternetServerInfoReceived(const Gameserver &_gs, std::string server_ip, uint16_t server_port, uint32_t last_played, EMatchMakingType type, HServerListRequest id, bool isRefreshQuery, int iServer) {
    if (_gs.had_successful_response()) {
        Steam_Matchmaking_Servers_Gameserver g{};
        g.server = _gs;

        g.server.set_last_played(last_played);
        g.type = type;
        g.last_recv = std::chrono::high_resolution_clock::now();
        if (isRefreshQuery && iServer != -2) {
            g.single_server_refresh = true;
            g.list_position = iServer;
        }
        std::lock_guard<std::recursive_mutex> lock(global_mutex);
        for (auto &r : requests) {
            if (r.id == id && !r.cancelled) {
                gameservers.push_back(g);
                if (isRefreshQuery && iServer != -2) {
                    r.gameservers_filtered[iServer] = g;
                }
            }
        }
    } else {
        std::cout << "No succesful response from (internet) ip: " << server_ip << " port: " << server_port << std::endl;
    }
    std::lock_guard<std::recursive_mutex> lock(global_mutex);
    for (auto &req : requests) {
        if (req.id == id) {
            req.pending_responses--;
            std::cout << "req.pending: " << req.pending_responses << std::endl;
            if (req.pending_responses <= 0) {
                req.finished_pushing = true;
                std::cout << "All internet servers processed for request id: " << id << std::endl;
            }
            break;
        }
    }
}

void Steam_Matchmaking_Servers::ParseServersFile(EMatchMakingType request_type, std::vector<ServerItem> &vec) {
    std::string path = Local_Storage::get_history_file_path();
    std::cout << "Save dir: " << path << std::endl;

    std::string category_name =
        request_type == eHistoryServer ? "history" : "favorites";

    VDFNode root = VDFParser::parse(path);
    if (root.hasChild("Filters")) {
        auto &filters = root.children["Filters"];
        if (filters.hasChild(category_name)) {
            auto &category = filters.children[category_name];
            std::cout << category_name << ": " << std::endl;

            for (auto &[key, child] : category.children) {
                if (child.hasValue("address")) {
                    std::string serverAddress = child.values["address"];
                    uint32_t last_played = 0;
                    auto it = child.values.find("LastPlayed");
                    if (it != child.values.end() && !it->second.empty()) {
                        try {
                            last_played = static_cast<uint32_t>(std::stoul(it->second));
                        } catch (const std::exception &) {
                            last_played = 0;
                        }
                    }
                    bool has_space = std::any_of(serverAddress.begin(), serverAddress.end(), [](unsigned char c) {
                        return std::isspace(c);
                    });

                    if (has_space) {
                        std::cout << "Skipped invalid address (contains spaces): " << serverAddress << std::endl;
                        continue;
                    }
                    // Find the position of the colon
                    size_t colon_pos = serverAddress.find(':');

                    if (colon_pos == std::string::npos) {
                        vec.push_back({serverAddress, 27015, last_played});
                    }

                    std::string ip = serverAddress.substr(0, colon_pos);

                    std::string port_str = serverAddress.substr(colon_pos + 1);
                    try {
                        uint16_t port = std::stoi(port_str);
                        vec.push_back({ip, port, last_played});
                    } catch (const std::exception &e) {
                        std::cout << "Skipped invalid address (invalid port): " << serverAddress << std::endl;
                    }
                }
            }
        }
    }
}

void Steam_Matchmaking_Servers::RefreshServersFromFile(HServerListRequest id, EMatchMakingType type) {
    auto &servers = type == eHistoryServer ? history_servers : favorite_servers;
    if (servers.empty()) {
        ParseServersFile(type, servers);
    }

    {
        std::lock_guard<std::recursive_mutex> lock(global_mutex);
        for (auto &req : requests) {
            if (req.id == id) {
                req.pending_responses = servers.size();

                if (servers.empty()) {
                    req.finished_pushing = true;
                }
                break;
            }
        }
    }

    for (auto &server : servers) {
        std::string server_ip = server.ip;
        uint16_t server_port = server.port;
        uint32_t last_played = server.last_played;

        m_goldsrc_query.GetServerInfo(server.ip, server.port, [this, type, id, server_ip, server_port, last_played](const Gameserver &_gs) {
            if (_gs.had_successful_response()) {
                Steam_Matchmaking_Servers_Gameserver g{};
                g.server = _gs;

                g.server.set_last_played(last_played);
                g.type = type;
                g.last_recv = std::chrono::high_resolution_clock::now();
                std::lock_guard<std::recursive_mutex> lock(global_mutex);
                auto &target_list = type == eHistoryServer ? history_servers : favorite_servers;
                for (auto &s : target_list) {
                    if (s.ip == server_ip && s.port == server_port) {
                        s.gameserver = g.server;
                        break;
                    }
                }
                for (auto &req : requests) {
                    if (req.id == id && !req.cancelled) {
                        gameservers.push_back(g);
                        std::cout << "Added server: " << server_ip << ":" << server_port << " appid: " << g.server.appid() << std::endl;
                        break;
                    }
                }
            } else {
                std::cout << "No succesful response form ip: " << server_ip << " port: " << server_port << std::endl;
            }

            std::lock_guard<std::recursive_mutex> lock(global_mutex);
            for (auto &req : requests) {
                if (req.id == id) {
                    req.pending_responses--;

                    if (req.pending_responses <= 0) {
                        req.finished_pushing = true;
                        std::cout << "All servers processed for request: " << id << std::endl;
                    }
                    break;
                }
            }
        });
    }
}

std::string Steam_Matchmaking_Servers::ParseFilters(AppId_t iApp, STEAM_ARRAY_COUNT(nFilters) MatchMakingKeyValuePair_t **ppchFilters, uint32 nFilters) {
    std::string filter_string = "";
    if (ppchFilters && nFilters > 0) {
        std::cout << "ppchFilters: " << ppchFilters << std::endl;
        std::cout << "--- Filters for AppID " << iApp << " (" << nFilters << " total) ---" << std::endl;

        for (uint32 i = 0; i < nFilters; ++i) {
            if (ppchFilters[i] && ppchFilters[i]->m_szKey && ppchFilters[i]->m_szValue) {
                std::cout << " [" << i << "] Key: \"" << ppchFilters[i]->m_szKey
                          << "\" | Value: \"" << ppchFilters[i]->m_szValue << "\""
                          << std::endl;

                // Build the master server filter string format: \key\value
                filter_string += "\\";
                filter_string += ppchFilters[i]->m_szKey;
                filter_string += "\\";
                filter_string += ppchFilters[i]->m_szValue;
            } else {
                std::cout << " [" << i << "] Filter pointer (or its strings) is NULL" << std::endl;
            }
        }
        std::cout << "------------------------------------------" << std::endl;
    } else {
        std::cout << "No filters provided for RequestFavoritesServerList." << std::endl;
    }
    return filter_string;
}

void Steam_Matchmaking_Servers::switchServers(HServerListRequest id, EMatchMakingType type, std::string filters) {
    switch (type) {
    case eInternetServer: {
        std::thread worker(&Steam_Matchmaking_Servers::GetInternetServers, this, id, eInternetServer, filters);
        worker.detach();
        break;
    }
    case eLANServer:
        ProcessLANServerList(id, requests.size() - 1);
        break;
    case eFriendsServer:
        requests.back().finished_pushing = true; // Probably not safe
        break;
    case eFavoritesServer: {
        std::thread worker(&Steam_Matchmaking_Servers::RefreshServersFromFile, this, id, eFavoritesServer);
        worker.detach();
        break;
    }
    case eHistoryServer: {
        std::thread worker(&Steam_Matchmaking_Servers::RefreshServersFromFile, this, id, eHistoryServer);
        worker.detach();
        break;
    }
    case eSpectatorServer: {
        std::thread worker(&Steam_Matchmaking_Servers::GetInternetServers, this, id, eSpectatorServer, filters);
        worker.detach();
        break;
    }
    default:
        break;
    }
}

HServerListRequest Steam_Matchmaking_Servers::RequestServerList(AppId_t iApp, ISteamMatchmakingServerListResponse *pRequestServersResponse, ISteamMatchmakingServerListResponse001 *pOldRequestServersResponse, EMatchMakingType type, std::string filters) {
    PRINT_DEBUG("%u %p, %i", iApp, pRequestServersResponse, (int)type);
    std::lock_guard<std::recursive_mutex> lock(global_mutex);
    if (settings->matchmaking_server_list_always_lan_type) {
        PRINT_DEBUG("forcing request type to LAN");
        std::cout << "Forced LAN";
        type = EMatchMakingType::eLANServer;
    }
    if (pOldRequestServersResponse) {
        auto r = std::begin(requests);
        while (r != std::end(requests)) {
            if (r->old_callbacks != NULL && r->type == type) {
                std::cout << "Duplicate request for type: " << type << " already exists. Releasing..." << std::endl;
                ReleaseRequest((void *)type);
                break;
            }

            ++r;
        }
    }

    ++server_list_request;
    HServerListRequest id = (void *)server_list_request;

    struct Steam_Matchmaking_Request request{};
    request.appid = iApp;

    request.callbacks = pRequestServersResponse;
    request.old_callbacks = pOldRequestServersResponse;
    request.cancelled = false;
    request.completed = false;
    request.type = type;
    request.id = id;
    requests.push_back(request);
    PRINT_DEBUG("pushed new request with id: %p", request.id);
    std::cout << "New server request id: " << request.id << " type: " << request.type << " oldcb: " << pOldRequestServersResponse << std::endl;
    switchServers(id, type, filters);
    return id;
}

// Request a new list of servers of a particular type.  These calls each
// correspond to one of the EMatchMakingType values. Each call allocates a new
// asynchronous request object. Request object must be released by calling
// ReleaseRequest( hServerListRequest )
HServerListRequest Steam_Matchmaking_Servers::RequestInternetServerList(AppId_t iApp, STEAM_ARRAY_COUNT(nFilters) MatchMakingKeyValuePair_t **ppchFilters, uint32 nFilters, ISteamMatchmakingServerListResponse *pRequestServersResponse) {
    PRINT_DEBUG("RequestInternetServerList\n");
    std::string filters = ParseFilters(iApp, ppchFilters, nFilters);
    return RequestServerList(iApp, pRequestServersResponse, NULL, eInternetServer, filters);
}

HServerListRequest Steam_Matchmaking_Servers::RequestLANServerList(AppId_t iApp, ISteamMatchmakingServerListResponse *pRequestServersResponse) {
    PRINT_DEBUG("RequestLANServerList %u\n", iApp);
    return RequestServerList(iApp, pRequestServersResponse, NULL, eLANServer);
}

HServerListRequest Steam_Matchmaking_Servers::RequestFriendsServerList(AppId_t iApp, STEAM_ARRAY_COUNT(nFilters) MatchMakingKeyValuePair_t **ppchFilters, uint32 nFilters, ISteamMatchmakingServerListResponse *pRequestServersResponse) {
    PRINT_DEBUG("RequestFriendsServerList\n");
    return RequestServerList(iApp, pRequestServersResponse, NULL, eFriendsServer);
}

HServerListRequest Steam_Matchmaking_Servers::RequestFavoritesServerList(AppId_t iApp, STEAM_ARRAY_COUNT(nFilters) MatchMakingKeyValuePair_t **ppchFilters, uint32 nFilters, ISteamMatchmakingServerListResponse *pRequestServersResponse) {
    PRINT_DEBUG("RequestFavoritesServerList\n");
    std::string filters = ParseFilters(iApp, ppchFilters, nFilters);
    return RequestServerList(iApp, pRequestServersResponse, NULL, eFavoritesServer);
}

HServerListRequest Steam_Matchmaking_Servers::RequestHistoryServerList(AppId_t iApp, STEAM_ARRAY_COUNT(nFilters) MatchMakingKeyValuePair_t **ppchFilters, uint32 nFilters, ISteamMatchmakingServerListResponse *pRequestServersResponse) {
    PRINT_DEBUG("RequestHistoryServerList\n");
    std::string filters = ParseFilters(iApp, ppchFilters, nFilters);
    return RequestServerList(iApp, pRequestServersResponse, NULL, eHistoryServer);
}

HServerListRequest Steam_Matchmaking_Servers::RequestSpectatorServerList(AppId_t iApp, STEAM_ARRAY_COUNT(nFilters) MatchMakingKeyValuePair_t **ppchFilters, uint32 nFilters, ISteamMatchmakingServerListResponse *pRequestServersResponse) {
    PRINT_DEBUG("RequestSpectatorServerList\n");
    std::string filters = ParseFilters(iApp, ppchFilters, nFilters);
    return RequestServerList(iApp, pRequestServersResponse, NULL, eSpectatorServer, filters);
}

void Steam_Matchmaking_Servers::RequestInternetServerList(AppId_t iApp, MatchMakingKeyValuePair_t **ppchFilters, uint32 nFilters, ISteamMatchmakingServerListResponse001 *pRequestServersResponse) {
    PRINT_DEBUG("%s old\n", __FUNCTION__);
    std::string filters = ParseFilters(iApp, ppchFilters, nFilters);
    RequestServerList(iApp, NULL, pRequestServersResponse, eInternetServer, filters);
}

void Steam_Matchmaking_Servers::RequestLANServerList(AppId_t iApp, ISteamMatchmakingServerListResponse001 *pRequestServersResponse) {
    PRINT_DEBUG("%s old\n", __FUNCTION__);
    RequestServerList(iApp, NULL, pRequestServersResponse, eLANServer);
}

void Steam_Matchmaking_Servers::RequestFriendsServerList(AppId_t iApp, MatchMakingKeyValuePair_t **ppchFilters, uint32 nFilters, ISteamMatchmakingServerListResponse001 *pRequestServersResponse) {
    PRINT_DEBUG("%s old\n", __FUNCTION__);
    RequestServerList(iApp, NULL, pRequestServersResponse, eFriendsServer);
}

void Steam_Matchmaking_Servers::RequestFavoritesServerList(AppId_t iApp, MatchMakingKeyValuePair_t **ppchFilters, uint32 nFilters, ISteamMatchmakingServerListResponse001 *pRequestServersResponse) {
    PRINT_DEBUG("%s old\n", __FUNCTION__);
    std::string filters = ParseFilters(iApp, ppchFilters, nFilters);
    RequestServerList(iApp, NULL, pRequestServersResponse, eFavoritesServer);
}

void Steam_Matchmaking_Servers::RequestHistoryServerList(AppId_t iApp, MatchMakingKeyValuePair_t **ppchFilters, uint32 nFilters, ISteamMatchmakingServerListResponse001 *pRequestServersResponse) {
    PRINT_DEBUG("%s old\n", __FUNCTION__);
    std::string filters = ParseFilters(iApp, ppchFilters, nFilters);
    RequestServerList(iApp, NULL, pRequestServersResponse, eHistoryServer);
}

void Steam_Matchmaking_Servers::RequestSpectatorServerList(AppId_t iApp, MatchMakingKeyValuePair_t **ppchFilters, uint32 nFilters, ISteamMatchmakingServerListResponse001 *pRequestServersResponse) {
    PRINT_DEBUG("%s old\n", __FUNCTION__);
    std::string filters = ParseFilters(iApp, ppchFilters, nFilters);
    RequestServerList(iApp, NULL, pRequestServersResponse, eSpectatorServer, filters);
}

// Releases the asynchronous request object and cancels any pending query on it
// if there's a pending query in progress. RefreshComplete callback is not
// posted when request is released.
void Steam_Matchmaking_Servers::ReleaseRequest(HServerListRequest hRequest) {
    PRINT_DEBUG("ReleaseRequest %p\n", hRequest);
    std::cout << "=================== Releasing request id: " << hRequest << std::endl;
    std::lock_guard<std::recursive_mutex> lock(global_mutex);
    auto r = std::begin(requests);
    while (r != std::end(requests)) {
        if (r->old_callbacks != NULL ? (void *)r->type == hRequest : r->id == hRequest) {
            // NOTE: some garbage games release the request before getting server
            // details from it.
            r = requests.erase(r);
        } else {
            ++r;
        }
    }
}

/* the filter operation codes that go in the key part of
   MatchMakingKeyValuePair_t should be one of these:

    "map"
        - Server passes the filter if the server is playing the specified map.
    "gamedataand"
        - Server passes the filter if the server's game data
   (ISteamGameServer::SetGameData) contains all of the specified strings.  The
   value field is a comma-delimited list of strings to match. "gamedataor"
        - Server passes the filter if the server's game data
   (ISteamGameServer::SetGameData) contains at least one of the specified
   strings.  The value field is a comma-delimited list of strings to match.
    "gamedatanor"
        - Server passes the filter if the server's game data
   (ISteamGameServer::SetGameData) does not contain any of the specified
   strings.  The value field is a comma-delimited list of strings to check.
    "gametagsand"
        - Server passes the filter if the server's game tags
   (ISteamGameServer::SetGameTags) contains all of the specified strings.  The
   value field is a comma-delimited list of strings to check. "gametagsnor"
        - Server passes the filter if the server's game tags
   (ISteamGameServer::SetGameTags) does not contain any of the specified
   strings.  The value field is a comma-delimited list of strings to check.
    "and" (x1 && x2 && ... && xn)
    "or" (x1 || x2 || ... || xn)
    "nand" !(x1 && x2 && ... && xn)
    "nor" !(x1 || x2 || ... || xn)
        - Performs Boolean operation on the following filters.  The operand to
   this filter specifies the "size" of the Boolean inputs to the operation, in
   Key/value pairs.  (The keyvalue pairs must immediately follow, i.e. this is a
   prefix logical operator notation.) In the simplest case where Boolean
   expressions are not nested, this is simply the number of operands.

        For example, to match servers on a particular map or with a particular
   tag, would would use these filters.

            ( server.map == "cp_dustbowl" || server.gametags.contains("payload")
   ) "or", "2" "map", "cp_dustbowl" "gametagsand", "payload"

        If logical inputs are nested, then the operand specifies the size of the
   entire "length" of its operands, not the number of immediate children.

            ( server.map == "cp_dustbowl" || (
   server.gametags.contains("payload") &&
   !server.gametags.contains("payloadrace") ) ) "or", "4" "map", "cp_dustbowl"
            "and", "2"
            "gametagsand", "payload"
            "gametagsnor", "payloadrace"

        Unary NOT can be achieved using either "nand" or "nor" with a single
   operand.

    "addr"
        - Server passes the filter if the server's query address matches the
   specified IP or IP:port. "gameaddr"
        - Server passes the filter if the server's game address matches the
   specified IP or IP:port.

    The following filter operations ignore the "value" part of
   MatchMakingKeyValuePair_t

    "dedicated"
        - Server passes the filter if it passed true to SetDedicatedServer.
    "secure"
        - Server passes the filter if the server is VAC-enabled.
    "notfull"
        - Server passes the filter if the player count is less than the reported max player count.
   "hasplayers"
        - Server passes the filter if the player count is greater than zero.
    "noplayers"
        - Server passes the filter if it doesn't have any players.
    "linux"
        - Server passes the filter if it's a linux server
*/

void Steam_Matchmaking_Servers::server_details(Gameserver *g, gameserveritem_t *server) {
    uint16 query_port = g->query_port();
    if (g->query_port() == 0xFFFF) {
        query_port = g->port();
    }

    server->m_NetAdr.Init(g->ip(), query_port, g->port());
    server->m_nPing = g->latency();
    server->m_bHadSuccessfulResponse = g->had_successful_response();
    server->m_bDoNotRefresh = false;
    strncpy(server->m_szGameDir, g->mod_dir().c_str(),
            k_cbMaxGameServerGameDir - 1);
    strncpy(server->m_szMap, g->map_name().c_str(), k_cbMaxGameServerMapName - 1);
    strncpy(server->m_szGameDescription, g->game_description().c_str(),
            k_cbMaxGameServerGameDescription - 1);

    server->m_szGameDir[k_cbMaxGameServerGameDir - 1] = 0;
    server->m_szMap[k_cbMaxGameServerMapName - 1] = 0;
    server->m_szGameDescription[k_cbMaxGameServerGameDescription - 1] = 0;

    server->m_nAppID = g->appid();
    server->m_nPlayers = g->num_players();
    server->m_nMaxPlayers = g->max_player_count();
    server->m_nBotPlayers = g->bot_player_count();
    server->m_bPassword = g->password_protected();
    server->m_bSecure = g->secure();
    server->m_ulTimeLastPlayed = g->last_played();
    server->m_nServerVersion = g->version();
    server->SetName(g->server_name().c_str());
    server->m_steamID = CSteamID((uint64)g->id());
    PRINT_DEBUG("server_details %llu\n", g->id());

    strncpy(server->m_szGameTags, g->tags().c_str(), k_cbMaxGameServerTags - 1);
    server->m_szGameTags[k_cbMaxGameServerTags - 1] = 0;
}

// Get details on a given server in the list, you can get the valid range of
// index values by calling GetServerCount().  You will also receive index values
// in ISteamMatchmakingServerListResponse::ServerResponded() callbacks
gameserveritem_t *Steam_Matchmaking_Servers::GetServerDetails(HServerListRequest hRequest, int iServer) {
    std::lock_guard<std::recursive_mutex> lock(global_mutex);

    auto r = std::begin(requests);
    while (r != std::end(requests)) {
        if (r->old_callbacks != NULL ? (void *)r->type == hRequest : r->id == hRequest) {
            if (iServer >= 0 && iServer < r->gameservers_filtered.size()) {
                Gameserver *gs = &r->gameservers_filtered[iServer].server;
                m_server_info = gameserveritem_t();
                server_details(gs, &m_server_info);
                return &m_server_info;
            }
            break;
        }
        ++r;
    }
    return NULL;
}

// Cancel a request which is operation on the given list type.  You should call
// this to cancel any in-progress requests before destructing a callback object
// that may have been passed to one of the above list request calls.  Not doing
// so may result in a crash when a callback occurs on the destructed object.
// Canceling a query does not release the allocated request handle.
// The request handle must be released using ReleaseRequest( hRequest )
void Steam_Matchmaking_Servers::CancelQuery(HServerListRequest hRequest) {
    PRINT_DEBUG("CancelQuery %p\n", hRequest);
    std::cout << "Canceling Query ============================= " << hRequest << std::endl;
    std::lock_guard<std::recursive_mutex> lock(global_mutex);

    for (auto &r : requests) {
        if (r.old_callbacks != NULL ? (void *)r.type == hRequest : r.id == hRequest) {
            r.cancelled = true;
            gameservers.erase(
                std::remove_if(gameservers.begin(), gameservers.end(), [&r](const Steam_Matchmaking_Servers_Gameserver &gs) {
                    return gs.type == r.type;
                }),
                gameservers.end());
            return;
        }
    }
    std::cout << "No query with such id: " << hRequest << std::endl;
}

// Ping every server in your list again but don't update the list of servers
// Query callback installed when the server list was requested will be used
// again to post notifications and RefreshComplete, so the callback must remain
// valid until another RefreshComplete is called on it or the request
// is released with ReleaseRequest( hRequest )
void Steam_Matchmaking_Servers::RefreshQuery(HServerListRequest hRequest) {
    PRINT_DEBUG("RefreshQuery %p\n", hRequest);
    std::cout << "Refresh query id: " << hRequest << std::endl;
    std::lock_guard<std::recursive_mutex> lock(global_mutex);
    for (auto &r : requests) {
        if (r.old_callbacks != NULL ? (void *)r.type == hRequest : r.id == hRequest) {
            reactivate_request(r); // This would reset r.pending_responses
            for (auto &gs : r.gameservers_filtered) {
                r.pending_responses++;
                int iServer = static_cast<int>(&gs - r.gameservers_filtered.data());
                std::string str_ip = ip_to_string(gs.server.ip());
                m_goldsrc_query.GetServerInfo(str_ip, gs.server.port(), [this, str_ip, port = gs.server.port(), last_played = gs.server.last_played(), type = r.type, id = r.id, iServer](const Gameserver &_gs) mutable {
                    this->OnInternetServerInfoReceived(_gs, str_ip, port, last_played, type, id, true, iServer);
                });
            }
            break;
        }
    }
}

// Returns true if the list is currently refreshing its server list
bool Steam_Matchmaking_Servers::IsRefreshing(HServerListRequest hRequest) {
    PRINT_DEBUG("IsRefreshing %p\n", hRequest);
    for (auto &r : requests) {
        if ((r.old_callbacks != NULL ? (void *)r.type == hRequest : r.id == hRequest) && !r.cancelled && !r.responded && !r.finished_pushing) {
            return true;
        }
    }
    return false;
}

// How many servers in the given list, GetServerDetails above takes 0...
// GetServerCount() - 1
int Steam_Matchmaking_Servers::GetServerCount(HServerListRequest hRequest) {
    PRINT_DEBUG("GetServerCount %p\n", hRequest);
    std::lock_guard<std::recursive_mutex> lock(global_mutex);
    int size = 0;
    auto r = std::begin(requests);
    while (r != std::end(requests)) {
        if (r->old_callbacks != NULL ? (void *)r->type == hRequest : r->id == hRequest) {
            size = r->gameservers_filtered.size();
            break;
        }
        ++r;
    }
    return size;
}

// Refresh a single server inside of a query (rather than all the servers )
void Steam_Matchmaking_Servers::RefreshServer(HServerListRequest hRequest, int iServer) {
    PRINT_DEBUG("RefreshServer %p\n", hRequest);
    std::cout << "RefreshServer int id: " << hRequest << " i: " << iServer << std::endl;
    std::lock_guard<std::recursive_mutex> lock(global_mutex);
    for (auto &r : requests) {
        if (r.old_callbacks != NULL ? (void *)r.type == hRequest : r.id == hRequest) {
            if (iServer < r.gameservers_filtered.size()) {
                Steam_Matchmaking_Servers_Gameserver gs = r.gameservers_filtered[iServer];
                reactivate_request(r);
                std::string str_ip = ip_to_string(gs.server.ip());
                m_goldsrc_query.GetServerInfo(str_ip, gs.server.port(), [this, &r, gs, iServer, str_ip, id = r.id](const Gameserver &_gs) mutable {
                    if (_gs.had_successful_response()) {
                        gs.server = _gs;
                        gs.last_recv = std::chrono::high_resolution_clock::now();
                        gs.single_server_refresh = true;
                        gs.list_position = iServer;
                        std::lock_guard<std::recursive_mutex> lock(global_mutex);
                        gameservers.push_back(gs);
                        for (auto &req : requests) {
                            if (req.id == id) {
                                req.gameservers_filtered[iServer] = gs;
                                req.finished_pushing = true;
                                std::cout << "Finished pushing from RefreshServer" << std::endl;
                                break;
                            }
                        }
                        std::cout << "Refreshed server: " << str_ip << ":" << gs.server.port() << " appid: " << gs.server.appid() << std::endl;
                    } else {
                        std::cout << "No response from server while refreshing: " << str_ip << std::endl;
                    }
                });
            }
            break;
        }
    }
}

void Steam_Matchmaking_Servers::reactivate_request(Steam_Matchmaking_Request &request) {
    std::cout << "Reactivate requ r.id: " << request.id << std::endl;
    request.cancelled = false;
    request.completed = false;
    request.finished_pushing = false;
    request.responded = false;
    request.pending_responses = 0;
}

static HServerQuery new_server_query() {
    static int a;
    ++a;
    if (!a)
        ++a;
    return a;
}

//-----------------------------------------------------------------------------
// Queries to individual servers directly via IP/Port
//-----------------------------------------------------------------------------

std::string Steam_Matchmaking_Servers::ip_to_string(uint32_t ip_host_order) {
    struct in_addr addr;
    // Convert from host to network order
    addr.s_addr = htonl(ip_host_order);

    char buffer[INET_ADDRSTRLEN];
    if (inet_ntop(AF_INET, &addr, buffer, INET_ADDRSTRLEN)) {
        return std::string(buffer);
    }
    return "0.0.0.0";
}

void Steam_Matchmaking_Servers::ProcessPingRequest(uint32 unIP, uint16 usPort, HServerQuery id) {
    std::string ip = ip_to_string(unIP);
    std::cout << "Ping with ip: " << ip << " id: " << id << std::endl;
    m_goldsrc_query.GetServerInfo(ip, usPort, [this, ip, usPort, unIP, id](const Gameserver &_gs) {
        if (_gs.had_successful_response()) {
            Gameserver server = _gs;
            server.set_ip(unIP);
            server.set_port(usPort);
            server.set_query_port(usPort);
            std::lock_guard<std::recursive_mutex> lock(global_mutex);
            for (auto &r : direct_ip_requests) {
                if (r.id == id) {
                    server_details(&server, &r.ping_server_info);
                    r.processed = true;
                }
            }
            std::cout << "ProcessPingRequest successful: " << ip << ":" << usPort << std::endl;
        } else {
            std::cout << "No response from server while refreshing: " << ip << ":" << usPort << std::endl;
        }
    });
}

// Request updated ping time and other details from a single server
HServerQuery Steam_Matchmaking_Servers::PingServer(uint32 unIP, uint16 usPort, ISteamMatchmakingPingResponse *pRequestServersResponse) {
    std::cout << "PingServer " << unIP << " pt " << usPort << std::endl;
    PRINT_DEBUG("PingServer %hhu.%hhu.%hhu.%hhu:%hu\n", ((unsigned char *)&unIP)[3], ((unsigned char *)&unIP)[2], ((unsigned char *)&unIP)[1], ((unsigned char *)&unIP)[0], usPort);
    std::lock_guard<std::recursive_mutex> lock(global_mutex);
    std::thread::id this_id = std::this_thread::get_id();
    Steam_Matchmaking_Servers_Direct_IP_Request r;
    r.id = new_server_query();
    r.ip = unIP;
    r.port = usPort;
    r.ping_response = pRequestServersResponse;
    r.created = std::chrono::high_resolution_clock::now();
    direct_ip_requests.push_back(r);
    std::thread worker(&Steam_Matchmaking_Servers::ProcessPingRequest, this, unIP,
                       usPort, r.id);
    worker.detach();
    std::cout << "Pushed r: " << r.id << std::endl;
    return r.id;
}

void Steam_Matchmaking_Servers::ProcessPlayerRequest(HServerQuery id, uint32 unIP, uint16 usPort) {
    std::string ip = ip_to_string(unIP);

    for (auto &r : direct_ip_requests) {
        if (r.id == id) {
            m_goldsrc_query.GetServerPlayers(ip, usPort, r.player_server_info);
            r.processed = true;
        }
    }
}

void Steam_Matchmaking_Servers::ProcessRulesRequest(HServerQuery id, uint32 unIP, uint16 usPort) {
    std::string ip = ip_to_string(unIP);

    for (auto &r : direct_ip_requests) {
        if (r.id == id) {
            m_goldsrc_query.GetServerRules(ip, usPort, r.rules_server_info);
            r.processed = true;
        }
    }
}

// Request the list of players currently playing on a server
HServerQuery Steam_Matchmaking_Servers::PlayerDetails(uint32 unIP, uint16 usPort, ISteamMatchmakingPlayersResponse *pRequestServersResponse) {
    std::cout << "PlayerDetails " << unIP << " pt " << usPort << std::endl;
    PRINT_DEBUG("PlayerDetails %hhu.%hhu.%hhu.%hhu:%hu\n", ((unsigned char *)&unIP)[3], ((unsigned char *)&unIP)[2], ((unsigned char *)&unIP)[1], ((unsigned char *)&unIP)[0], usPort);
    Steam_Matchmaking_Servers_Direct_IP_Request r;
    r.id = new_server_query();
    r.ip = unIP;
    r.port = usPort;
    r.players_response = pRequestServersResponse;
    r.created = std::chrono::high_resolution_clock::now();

    std::thread worker([this, r, unIP, usPort]() {
        std::lock_guard<std::recursive_mutex> lock(global_mutex);
        direct_ip_requests.push_back(r);
        std::cout << "Pushed r: " << r.id << std::endl;
        ProcessPlayerRequest(r.id, unIP, usPort);
    });
    worker.detach();
    return r.id;
}

// Request the list of rules that the server is running (See
// ISteamGameServer::SetKeyValue() to set the rules server side)
HServerQuery Steam_Matchmaking_Servers::ServerRules(uint32 unIP, uint16 usPort, ISteamMatchmakingRulesResponse *pRequestServersResponse) {
    std::cout << "ServerRules " << unIP << " pt " << usPort << std::endl;
    PRINT_DEBUG("ServerRules %hhu.%hhu.%hhu.%hhu:%hu\n", ((unsigned char *)&unIP)[3], ((unsigned char *)&unIP)[2], ((unsigned char *)&unIP)[1], ((unsigned char *)&unIP)[0], usPort);
    std::lock_guard<std::recursive_mutex> lock(global_mutex);
    Steam_Matchmaking_Servers_Direct_IP_Request r;
    r.id = new_server_query();
    r.ip = unIP;
    r.port = usPort;
    r.rules_response = pRequestServersResponse;
    r.created = std::chrono::high_resolution_clock::now();
    std::thread worker([this, r, unIP, usPort]() {
        std::lock_guard<std::recursive_mutex> lock(global_mutex);
        direct_ip_requests.push_back(r);
        std::cout << "Pushed r: " << r.id << std::endl;
        ProcessRulesRequest(r.id, unIP, usPort);
    });
    worker.detach();
    return r.id;
}

// Cancel an outstanding Ping/Players/Rules query from above.  You should call
// this to cancel any in-progress requests before destructing a callback object
// that may have been passed to one of the above calls to avoid crashing when
// callbacks occur.
void Steam_Matchmaking_Servers::CancelServerQuery(HServerQuery hServerQuery) {
    PRINT_DEBUG("CancelServerQuery\n");
    std::lock_guard<std::recursive_mutex> lock(global_mutex);
    auto r = std::find_if(
        direct_ip_requests.begin(), direct_ip_requests.end(),
        [&hServerQuery](Steam_Matchmaking_Servers_Direct_IP_Request const &item) {
            return item.id == hServerQuery;
        });
    if (direct_ip_requests.end() == r)
        return;
    direct_ip_requests.erase(r);
}

void Steam_Matchmaking_Servers::ReadInput() {
    std::string value;
    std::cout << "Enter an integer: ";
    if (std::cin >> value) {
        if (value == "GS") {
            std::cout << "GS: " << std::endl;
            for (const auto &item : gameservers) {
                std::cout << item.type << " ";
                std::cout << "Name: " << item.server.server_name() << " ";
                std::cout << "Map: " << item.server.map_name() << " ";
                std::cout << "IP+Port " << item.server.ip() << " " << item.server.port()
                          << " ";
            }
            std::cout << "]" << std::endl;
        }
    }
}

void Steam_Matchmaking_Servers::DebugListServers(const Steam_Matchmaking_Request &r, const std::string &label) {
    std::cout << "=== [" << label
              << "] Server List (Size: " << r.gameservers_filtered.size()
              << ") ===" << std::endl;

    for (size_t i = 0; i < r.gameservers_filtered.size(); ++i) {
        const auto &gs = r.gameservers_filtered[i];

        std::cout << i << ". " << gs.server.server_name() << std::endl;
        std::cout << "   Addr: " << ip_to_string(gs.server.ip()) << ":"
                  << gs.server.port() << " | Ping: " << gs.server.latency() << "ms"
                  << std::endl;
        // std::cout << "   Players: " << gs.server.players() << "/" <<
        // gs.server.max_players() << std::endl;
        std::cout << "   Map: " << gs.server.map_name()
                  << " | AppID: " << gs.server.appid() << std::endl;
        std::cout << "------------------------------------------" << std::endl;
    }
}

void Steam_Matchmaking_Servers::RunCallbacks() {
    {
        PRINT_DEBUG("Steam_Matchmaking_Servers::RunCallbacks\n");
        std::lock_guard<std::recursive_mutex> lock(global_mutex);

        PRINT_DEBUG("REQUESTS %zu gs: %zu\n", requests.size(), gameservers.size());

        for (auto &r : requests) {
            if (r.responded)
                continue;

            if (r.completed || r.cancelled) {
                EMatchMakingServerResponse response;
                if (!r.gameservers_filtered.empty()) {
                    response = eServerResponded;
                    std::cout << "Refresh complete (responded): " << r.finished_pushing << " r.gmf.size: " << r.gameservers_filtered.size() << std::endl;
                } else {
                    response = eNoServersListedOnMasterServer;
                    std::cout << "\nRefresh no-listed type: " << r.type << " id: " << r.id << std::endl;
                }
                if (r.callbacks) {
                    ISteamMatchmakingServerListResponse *cb = r.callbacks;
                    HServerListRequest req_id = r.id;
                    cb->RefreshComplete(req_id, response);
                } else if (r.old_callbacks) {
                    ISteamMatchmakingServerListResponse001 *cb = r.old_callbacks;
                    cb->RefreshComplete(response);
                }
                r.responded = true;
                continue;
            }
            auto g = std::begin(gameservers);
            while (g != std::end(gameservers)) {
                if ((g->type != r.type || g->type == eLANServer) && check_timedout(g->last_recv, SERVER_TIMEOUT)) {
                    g = gameservers.erase(g);
                    PRINT_DEBUG("SERVER TIMEOUT\n");
                    std::cout << "SERVER TIMEOUT" << std::endl;
                } else {
                    if (g->type == r.type) {
                        char buffer[INET_ADDRSTRLEN];
                        struct in_addr addr_in;
                        addr_in.s_addr = g->server.ip();

                        if (inet_ntop(AF_INET, &addr_in, buffer, INET_ADDRSTRLEN)) {
                            std::string formatted_ip(buffer);
                        }
                        int item_position = 0;

                        if (g->single_server_refresh) {
                            item_position = g->list_position;
                            g->single_server_refresh = false;
                            g->list_position = 0;
                        } else {
                            r.gameservers_filtered.push_back(std::move(*g));
                            item_position = r.gameservers_filtered.size() - 1;
                        }
                        if (r.callbacks) {
                            ISteamMatchmakingServerListResponse *cb = r.callbacks;
                            HServerListRequest req_id = r.id;
                            cb->ServerResponded(req_id, item_position);
                        } else if (r.old_callbacks) {
                            ISteamMatchmakingServerListResponse001 *cb = r.old_callbacks;
                            cb->ServerResponded(item_position);
                        }
                        g = gameservers.erase(g);
                    } else {
                        ++g;
                    }
                }
            }
            if (r.finished_pushing && gameservers.empty()) {
                std::cout << "Finished pushing" << std::endl;
                r.completed = true;
            }
        }
        auto dip = std::begin(direct_ip_requests);
        while (dip != std::end(direct_ip_requests)) {
            if (!dip->processed && check_timedout(dip->created, DIRECT_IP_TIMEOUT)) {
                dip = direct_ip_requests.erase(dip);
                std::cout << "DIP timeout" << std::endl;
            } else {
                if (dip->processed) {
                    if (dip->ping_response) {
                        ISteamMatchmakingPingResponse *cb = dip->ping_response;
                        gameserveritem_t info = dip->ping_server_info;
                        if (info.m_bHadSuccessfulResponse) {
                            cb->ServerResponded(info);
                        } else {
                            cb->ServerFailedToRespond();
                        }
                        dip = direct_ip_requests.erase(dip);
                    } else if (dip->players_response) {
                        ISteamMatchmakingPlayersResponse *cb = dip->players_response;
                        PlayerServerResult info = dip->player_server_info;
                        if (info.finished) {
                            for (auto &player_info : info.players) {
                                cb->AddPlayerToList(player_info.name.c_str(),
                                                    player_info.score,
                                                    player_info.time);
                            }
                            cb->PlayersRefreshComplete();
                        } else {
                            cb->PlayersFailedToRespond();
                        }

                        dip = direct_ip_requests.erase(dip);
                    } else if (dip->rules_response) {
                        ISteamMatchmakingRulesResponse *cb = dip->rules_response;
                        RulesServerResult info = dip->rules_server_info;
                        if (info.finished) {
                            for (const auto &rule : info.rules) {
                                cb->RulesResponded(rule.first.c_str(), rule.second.c_str());
                            }
                            cb->RulesRefreshComplete();
                        } else {
                            cb->RulesFailedToRespond();
                        }

                        dip = direct_ip_requests.erase(dip);
                    }
                } else {
                    ++dip;
                }
            }
        }
    }
}
void Steam_Matchmaking_Servers::Callback(Common_Message *msg) {
    if (msg->has_gameserver()) {
        std::lock_guard<std::recursive_mutex> lock(global_mutex);
        PRINT_DEBUG("got SERVER %llu, offline:%u\n", msg->gameserver().id(),
                    msg->gameserver().offline());
        if (msg->gameserver().offline()) {
            for (auto &g : gameservers_lan) {
                if (g.server.id() == msg->gameserver().id()) {
                    g.last_recv = std::chrono::high_resolution_clock::time_point();
                }
            }
        } else {
            bool already = false;
            for (auto &g : gameservers_lan) {
                if (g.server.id() == msg->gameserver().id()) {
                    g.last_recv = std::chrono::high_resolution_clock::now();
                    g.server = msg->gameserver();
                    g.server.set_ip(msg->source_ip());
                    g.type = eLANServer;
                    already = true;
                }
            }

            if (!already) {
                struct Steam_Matchmaking_Servers_Gameserver g;
                g.last_recv = std::chrono::high_resolution_clock::now();
                g.server = msg->gameserver();
                g.server.set_ip(msg->source_ip());
                gameservers_lan.push_back(g);
                PRINT_DEBUG("SERVER ADDED\n");
            }
        }
    }
}

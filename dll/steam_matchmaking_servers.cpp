/* Copyright (C) 2019 Mr Goldberg
   This file is part of the Goldberg Emulator

   The Goldberg Emulator is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 3 of the License, or (at your option) any later version.

   The Goldberg Emulator is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with the Goldberg Emulator; if not, see
   <http://www.gnu.org/licenses/>.  */

#include "steam_matchmaking_servers.h"

#include "HLDSQuery.h"
#include "vdf_parser.h"
#include <future>

static void network_callback(void *object, Common_Message *msg)
{
    PRINT_DEBUG("steam_matchmaking_servers_callback\n");

    Steam_Matchmaking_Servers *obj = (Steam_Matchmaking_Servers *)object;
    obj->Callback(msg);
}

Steam_Matchmaking_Servers::Steam_Matchmaking_Servers(class Settings *settings, class Networking *network)
{
    this->settings = settings;
    this->network = network;
    this->network->setCallback(CALLBACK_ID_GAMESERVER, (uint64) 0, &network_callback, this);

}

HServerListRequest Steam_Matchmaking_Servers::RequestServerList(AppId_t iApp, ISteamMatchmakingServerListResponse *pRequestServersResponse, EMatchMakingType type)
{
    PRINT_DEBUG("%u %p, %i", iApp, pRequestServersResponse, (int)type);
    std::lock_guard<std::recursive_mutex> lock(global_mutex);
    ++server_list_request;
    HServerListRequest id = (void *)server_list_request;

    if (settings->matchmaking_server_list_always_lan_type) {
        PRINT_DEBUG("forcing request type to LAN");
        std::cout << "Forced LAN";
        type = EMatchMakingType::eLANServer;
    }

    struct Steam_Matchmaking_Request request{};
    request.appid = iApp;
    request.callbacks = pRequestServersResponse;
    request.old_callbacks = NULL;
    request.cancelled = false;
    request.completed = false;
    request.type = type;
    request.id = id;
    requests.push_back(request);
    PRINT_DEBUG("pushed new request with id: %p", request.id);
    std::cout << "New server request id: " << request.id << " type: " << request.type << std::endl;
    if (type == eLANServer) ProcessLANServerList(id, requests.size() - 1);

    if (type == eFriendsServer) {
        requests.back().finished_pushing = true; // Probably not safe
    }

    if (type == eInternetServer || type == eSpectatorServer) {
    } else if (type == eHistoryServer || type == eFavoritesServer) {
        std::thread worker(&Steam_Matchmaking_Servers::RefreshServersFromFile, this, id, type, requests.size() - 1, type);
        worker.detach();
    }
    return id;
}

void Steam_Matchmaking_Servers::ProcessLANServerList(HServerListRequest id, size_t request_index) {
    if (!gameservers_lan.empty()) {
        gameservers.insert(gameservers.end(),
                      gameservers_lan.begin(),
                      gameservers_lan.end());
    }
    if (request_index < requests.size() && requests[request_index].id == id) {
        requests[request_index].finished_pushing = true;
    }
}

void Steam_Matchmaking_Servers::RefreshServersFromFile(HServerListRequest id, EMatchMakingType type, size_t request_index, EMatchMakingType request_type) {
    auto &servers = request_type == eHistoryServer ? history_servers : favorite_servers;
    if (servers.empty()) ParseServersFile(request_type, servers);

    // Vector to store futures for each parallel task
    std::vector<std::future<void>> futures;

    for (auto& server : servers) {
        // Checking if the request was cancelled before spawning a new thread
        {
            std::lock_guard<std::recursive_mutex> lock(global_mutex);
            if (request_index < requests.size() && requests[request_index].id == id) {
                if (requests[request_index].cancelled) {
                    break;
                }
            }
        }

        // Launching ProcessSingleServer in a separate thread
        futures.push_back(std::async(std::launch::async, [this, server, type, request_index, id]() {
            Steam_Matchmaking_Servers_Gameserver g{};
            this->ProcessSingleServer(server, type, g);

            std::lock_guard<std::recursive_mutex> lock(global_mutex);
            if (request_index < requests.size() && requests[request_index].id == id) {
                if (requests[request_index].cancelled) {
                    return;
                }
            }
            gameservers.push_back(g);
            std::cout << "Added server: " << server.ip << ":" << server.port << " appid: " << g.server.appid() << std::endl;
        }));
        //ProcessSingleServer(server, type);
    }

    // Wait for all threads to finish
    for (auto& f : futures) {
        f.get();
    }
    std::lock_guard<std::recursive_mutex> lock(global_mutex);
    if (request_index < requests.size() && requests[request_index].id == id) {
        requests[request_index].finished_pushing = true;
        // std::cout << "////////////////////////////////Marking as complete by request_index: " << id << std::endl;
    }
}

void Steam_Matchmaking_Servers::ProcessSingleServer(ServerItem server, EMatchMakingType type, Steam_Matchmaking_Servers_Gameserver& g) {
    // Heavy network operation happens here in parallel
    if (FetchServerData(server.ip, server.port, &g.server)) {
        uint32_t network_ip = inet_addr(server.ip.c_str());
        uint32_t host_ip = ntohl(network_ip);

        g.server.set_ip(host_ip);
        g.server.set_port(server.port);
        g.server.set_query_port(server.port);
        g.server.set_last_played(server.last_played);
        g.type = type;
        g.last_recv = std::chrono::high_resolution_clock::now();
    }
}

void Steam_Matchmaking_Servers::ParseServersFile(EMatchMakingType request_type, std::vector<ServerItem> &vec) {
    std::string path = Local_Storage::data_path + PATH_SEPARATOR + Local_Storage::historyFileName;
    std::cout << "Save dir: " << path << std::endl;

    std::string category_name = request_type == eHistoryServer ? "history" : "favorites";

    VDFNode root = VDFParser::parse(path);
    if (root.hasChild("Filters")) {
        auto& filters = root.children["Filters"];
        if (filters.hasChild(category_name)) {
            auto& category = filters.children[category_name];
            std::cout << category_name << ": " << std::endl;

            for (auto& [key, value] : category.children) {
                std::cout << category_name << " Server " << key << " data:" << std::endl;
                std::string serverAddress = value.values["address"]; // TODO: Add hasValue check
                uint32_t last_played = 0;
                auto it = value.values.find("LastPlayed");
                if (it != value.values.end() && !it->second.empty()) {
                    try {
                        last_played = static_cast<uint32_t>(std::stoul(it->second));
                    } catch (const std::exception&) {
                        last_played = 0;
                    }
                }
                // Find the position of the colon
                size_t colon_pos = serverAddress.find(':');

                if (colon_pos == std::string::npos) {
                    vec.push_back({serverAddress, 27015, last_played});
                }

                std::string ip = serverAddress.substr(0, colon_pos);

                std::string port_str = serverAddress.substr(colon_pos + 1);
                int port = std::stoi(port_str);
                vec.push_back({ip, port, last_played});
            }
        }
    }
}

bool Steam_Matchmaking_Servers::FetchServerData(std::string ip, uint16_t port, Gameserver* out_data)
{
    HLDSQuery query(ip, port);
    return query.get_info(out_data);
}

// Request a new list of servers of a particular type.  These calls each correspond to one of the EMatchMakingType values.
// Each call allocates a new asynchronous request object.
// Request object must be released by calling ReleaseRequest( hServerListRequest )
HServerListRequest Steam_Matchmaking_Servers::RequestInternetServerList( AppId_t iApp, STEAM_ARRAY_COUNT(nFilters) MatchMakingKeyValuePair_t **ppchFilters, uint32 nFilters, ISteamMatchmakingServerListResponse *pRequestServersResponse )
{
    PRINT_DEBUG("RequestInternetServerList\n");

    //TODO
    return RequestLANServerList(iApp, pRequestServersResponse);
}

HServerListRequest Steam_Matchmaking_Servers::RequestLANServerList( AppId_t iApp, ISteamMatchmakingServerListResponse *pRequestServersResponse )
{
    PRINT_DEBUG("RequestLANServerList %u\n", iApp);
    return RequestServerList(iApp, pRequestServersResponse, eLANServer);
}

HServerListRequest Steam_Matchmaking_Servers::RequestFriendsServerList( AppId_t iApp, STEAM_ARRAY_COUNT(nFilters) MatchMakingKeyValuePair_t **ppchFilters, uint32 nFilters, ISteamMatchmakingServerListResponse *pRequestServersResponse )
{
    PRINT_DEBUG("RequestFriendsServerList\n");
    return RequestServerList(iApp, pRequestServersResponse, eFriendsServer);
}

HServerListRequest Steam_Matchmaking_Servers::RequestFavoritesServerList( AppId_t iApp, STEAM_ARRAY_COUNT(nFilters) MatchMakingKeyValuePair_t **ppchFilters, uint32 nFilters, ISteamMatchmakingServerListResponse *pRequestServersResponse )
{
    PRINT_DEBUG("RequestFavoritesServerList\n");
    return RequestServerList(iApp, pRequestServersResponse, eFavoritesServer);
}

HServerListRequest Steam_Matchmaking_Servers::RequestHistoryServerList( AppId_t iApp, STEAM_ARRAY_COUNT(nFilters) MatchMakingKeyValuePair_t **ppchFilters, uint32 nFilters, ISteamMatchmakingServerListResponse *pRequestServersResponse )
{
    PRINT_DEBUG("RequestHistoryServerList\n");
    return RequestServerList(iApp, pRequestServersResponse, eHistoryServer);
}

HServerListRequest Steam_Matchmaking_Servers::RequestSpectatorServerList( AppId_t iApp, STEAM_ARRAY_COUNT(nFilters) MatchMakingKeyValuePair_t **ppchFilters, uint32 nFilters, ISteamMatchmakingServerListResponse *pRequestServersResponse )
{
    PRINT_DEBUG("RequestSpectatorServerList\n");
    return RequestServerList(iApp, pRequestServersResponse, eSpectatorServer);
}

void Steam_Matchmaking_Servers::RequestOldServerList(AppId_t iApp, ISteamMatchmakingServerListResponse001 *pRequestServersResponse, EMatchMakingType type)
{
    PRINT_DEBUG("RequestOldServerList %u\n", iApp);
    std::cout << "Old server list" << std::endl;
    std::lock_guard<std::recursive_mutex> lock(global_mutex);
    auto g = std::begin(requests);
    while (g != std::end(requests)) {
        if (g->id == ((void *)type)) {
            return;
        }

        ++g;
    }

    struct Steam_Matchmaking_Request request;
    request.appid = iApp;
    request.callbacks = NULL;
    request.old_callbacks = pRequestServersResponse;
    request.cancelled = false;
    request.completed = false;
    requests.push_back(request);
    requests[requests.size() - 1].id = (void *)type;
}

void Steam_Matchmaking_Servers::RequestInternetServerList( AppId_t iApp, MatchMakingKeyValuePair_t **ppchFilters, uint32 nFilters, ISteamMatchmakingServerListResponse001 *pRequestServersResponse )
{
    PRINT_DEBUG("%s old\n", __FUNCTION__);
    RequestOldServerList(iApp, pRequestServersResponse, eInternetServer);
}

void Steam_Matchmaking_Servers::RequestLANServerList( AppId_t iApp, ISteamMatchmakingServerListResponse001 *pRequestServersResponse )
{
    PRINT_DEBUG("%s old\n", __FUNCTION__);
    RequestOldServerList(iApp, pRequestServersResponse, eLANServer);
}

void Steam_Matchmaking_Servers::RequestFriendsServerList( AppId_t iApp, MatchMakingKeyValuePair_t **ppchFilters, uint32 nFilters, ISteamMatchmakingServerListResponse001 *pRequestServersResponse )
{
    PRINT_DEBUG("%s old\n", __FUNCTION__);
    RequestOldServerList(iApp, pRequestServersResponse, eFriendsServer);
}

void Steam_Matchmaking_Servers::RequestFavoritesServerList( AppId_t iApp, MatchMakingKeyValuePair_t **ppchFilters, uint32 nFilters, ISteamMatchmakingServerListResponse001 *pRequestServersResponse )
{
    PRINT_DEBUG("%s old\n", __FUNCTION__);
    RequestOldServerList(iApp, pRequestServersResponse, eFavoritesServer);
}

void Steam_Matchmaking_Servers::RequestHistoryServerList( AppId_t iApp, MatchMakingKeyValuePair_t **ppchFilters, uint32 nFilters, ISteamMatchmakingServerListResponse001 *pRequestServersResponse )
{
    PRINT_DEBUG("%s old\n", __FUNCTION__);
    RequestOldServerList(iApp, pRequestServersResponse, eHistoryServer);
}

void Steam_Matchmaking_Servers::RequestSpectatorServerList( AppId_t iApp, MatchMakingKeyValuePair_t **ppchFilters, uint32 nFilters, ISteamMatchmakingServerListResponse001 *pRequestServersResponse )
{
    PRINT_DEBUG("%s old\n", __FUNCTION__);
    RequestOldServerList(iApp, pRequestServersResponse, eSpectatorServer);
}


// Releases the asynchronous request object and cancels any pending query on it if there's a pending query in progress.
// RefreshComplete callback is not posted when request is released.
void Steam_Matchmaking_Servers::ReleaseRequest( HServerListRequest hServerListRequest )
{
    PRINT_DEBUG("ReleaseRequest %p\n", hServerListRequest);
    std::cout << "=================== Releasing request id: " << hServerListRequest << std::endl;

    auto r = std::begin(requests);
    while (r != std::end(requests)) {
        if (r->id == hServerListRequest) {
            //NOTE: some garbage games release the request before getting server details from it.
            std::lock_guard<std::recursive_mutex> lock(global_mutex);
            r = requests.erase(r);
        }
        else {
            ++r;
        }
    }
}


/* the filter operation codes that go in the key part of MatchMakingKeyValuePair_t should be one of these:

    "map"
        - Server passes the filter if the server is playing the specified map.
    "gamedataand"
        - Server passes the filter if the server's game data (ISteamGameServer::SetGameData) contains all of the
        specified strings.  The value field is a comma-delimited list of strings to match.
    "gamedataor"
        - Server passes the filter if the server's game data (ISteamGameServer::SetGameData) contains at least one of the
        specified strings.  The value field is a comma-delimited list of strings to match.
    "gamedatanor"
        - Server passes the filter if the server's game data (ISteamGameServer::SetGameData) does not contain any
        of the specified strings.  The value field is a comma-delimited list of strings to check.
    "gametagsand"
        - Server passes the filter if the server's game tags (ISteamGameServer::SetGameTags) contains all
        of the specified strings.  The value field is a comma-delimited list of strings to check.
    "gametagsnor"
        - Server passes the filter if the server's game tags (ISteamGameServer::SetGameTags) does not contain any
        of the specified strings.  The value field is a comma-delimited list of strings to check.
    "and" (x1 && x2 && ... && xn)
    "or" (x1 || x2 || ... || xn)
    "nand" !(x1 && x2 && ... && xn)
    "nor" !(x1 || x2 || ... || xn)
        - Performs Boolean operation on the following filters.  The operand to this filter specifies
        the "size" of the Boolean inputs to the operation, in Key/value pairs.  (The keyvalue
        pairs must immediately follow, i.e. this is a prefix logical operator notation.)
        In the simplest case where Boolean expressions are not nested, this is simply
        the number of operands.

        For example, to match servers on a particular map or with a particular tag, would would
        use these filters.

            ( server.map == "cp_dustbowl" || server.gametags.contains("payload") )
            "or", "2"
            "map", "cp_dustbowl"
            "gametagsand", "payload"

        If logical inputs are nested, then the operand specifies the size of the entire
        "length" of its operands, not the number of immediate children.

            ( server.map == "cp_dustbowl" || ( server.gametags.contains("payload") && !server.gametags.contains("payloadrace") ) )
            "or", "4"
            "map", "cp_dustbowl"
            "and", "2"
            "gametagsand", "payload"
            "gametagsnor", "payloadrace"

        Unary NOT can be achieved using either "nand" or "nor" with a single operand.

    "addr"
        - Server passes the filter if the server's query address matches the specified IP or IP:port.
    "gameaddr"
        - Server passes the filter if the server's game address matches the specified IP or IP:port.

    The following filter operations ignore the "value" part of MatchMakingKeyValuePair_t

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

void Steam_Matchmaking_Servers::server_details(Gameserver *g, gameserveritem_t *server)
{
    uint16 query_port = g->query_port();
    if (g->query_port() == 0xFFFF) {
        query_port = g->port();
    }

    server->m_NetAdr.Init(g->ip(), query_port, g->port());
    //std::cout << "server_details_ip_9876: " << g->ip() << " mp: " << g-> map_name() << " q-pt: " << query_port << " pt: " << g->port() << " ping: " << g->latency() << " had_response: " << g->had_successful_response() << std::endl;
    server->m_nPing = g->latency();
    server->m_bHadSuccessfulResponse = g->had_successful_response();
    server->m_bDoNotRefresh = false;
    strncpy(server->m_szGameDir, g->mod_dir().c_str(), k_cbMaxGameServerGameDir - 1);
    strncpy(server->m_szMap, g->map_name().c_str(), k_cbMaxGameServerMapName - 1);
    strncpy(server->m_szGameDescription, g->game_description().c_str(), k_cbMaxGameServerGameDescription - 1);

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

    // Outputting all fields for debugging
    /*std::cout << "--- Gameserveritem_t Debug Output ---" << std::endl;
    std::cout << "HadSuccessfulResponse: " << server->m_bHadSuccessfulResponse << std::endl;
    std::cout << "Server Name: " << server->GetName() << std::endl;
    std::cout << "IP/Port (NetAdr): " << server->m_NetAdr.GetIP() << ":" << server->m_NetAdr.GetConnectionPort()
              << " (Query: " << server->m_NetAdr.GetQueryPort() << ")" << std::endl;
    std::cout << "AppID: " << server->m_nAppID << std::endl;
    std::cout << "GameDir: " << server->m_szGameDir << std::endl;
    std::cout << "Map: " << server->m_szMap << std::endl;
    std::cout << "GameDescription: " << server->m_szGameDescription << std::endl;
    std::cout << "Players: " << (int)server->m_nPlayers << " / " << (int)server->m_nMaxPlayers
              << " (Bots: " << (int)server->m_nBotPlayers << ")" << std::endl;
    std::cout << "Ping: " << server->m_nPing << std::endl;
    std::cout << "Password: " << (server->m_bPassword ? "Yes" : "No") << std::endl;
    std::cout << "Secure: " << (server->m_bSecure ? "Yes" : "No") << std::endl;
    std::cout << "HadSuccessfulResponse: " << (server->m_bHadSuccessfulResponse ? "True" : "False") << std::endl;
    std::cout << "DoNotRefresh: " << (server->m_bDoNotRefresh ? "True" : "False") << std::endl;
    std::cout << "Last Played: " << server->m_ulTimeLastPlayed << std::endl;
    std::cout << "Version: " << server->m_nServerVersion << std::endl;
    std::cout << "SteamID: " << server->m_steamID.ConvertToUint64() << std::endl;
    std::cout << "Tags: " << server->m_szGameTags << std::endl;
    std::cout << "-------------------------------------" << std::endl;*/
}

// Get details on a given server in the list, you can get the valid range of index
// values by calling GetServerCount().  You will also receive index values in
// ISteamMatchmakingServerListResponse::ServerResponded() callbacks
gameserveritem_t *Steam_Matchmaking_Servers::GetServerDetails( HServerListRequest hRequest, int iServer )
{
    //std::cout << "GetServerDetails iServer: " << iServer << std::endl;
    PRINT_DEBUG("GetServerDetails %p %i\n", hRequest, iServer);
    std::lock_guard<std::recursive_mutex> lock(global_mutex);

    std::vector <struct Steam_Matchmaking_Servers_Gameserver> gameservers_filtered;
    auto g = std::begin(requests);
    while (g != std::end(requests)) {
        PRINT_DEBUG("equal? %p %p\n", hRequest, g->id);
        if (g->id == hRequest) {
            gameservers_filtered = g->gameservers_filtered;
            PRINT_DEBUG("found %u\n", gameservers_filtered.size());
            break;
        }

        ++g;
    }

    if (iServer >= gameservers_filtered.size() || iServer < 0) {
        std::cout << "R-NULL: " << iServer << std::endl;
        return NULL;
    }

    Gameserver *gs = &gameservers_filtered[iServer].server;
    gameserveritem_t *server = new gameserveritem_t(); //TODO: is the new here ok?
    server_details(gs, server);
    PRINT_DEBUG("Returned server details\n");
    return server;
}


// Cancel a request which is operation on the given list type.  You should call this to cancel
// any in-progress requests before destructing a callback object that may have been passed
// to one of the above list request calls.  Not doing so may result in a crash when a callback
// occurs on the destructed object.
// Canceling a query does not release the allocated request handle.
// The request handle must be released using ReleaseRequest( hRequest )
void Steam_Matchmaking_Servers::CancelQuery( HServerListRequest hRequest )
{
    PRINT_DEBUG("CancelQuery %p\n", hRequest);
    std::cout << "Canceling Query ============================= " << hRequest << std::endl;
    std::lock_guard<std::recursive_mutex> lock(global_mutex);

    auto r = std::begin(requests);
    while (r != std::end(requests)) {
        if (r->id == hRequest) {
            r->cancelled = true;
            return;
        }
        ++r;
    }
    std::cout << "No query with such id: " << hRequest << std::endl;
}

// Ping every server in your list again but don't update the list of servers
// Query callback installed when the server list was requested will be used
// again to post notifications and RefreshComplete, so the callback must remain
// valid until another RefreshComplete is called on it or the request
// is released with ReleaseRequest( hRequest )
void Steam_Matchmaking_Servers::RefreshQuery( HServerListRequest hRequest )
{
    PRINT_DEBUG("RefreshQuery %p\n", hRequest);
    std::cout << "Refresh query id: " << hRequest << std::endl;
}

// Returns true if the list is currently refreshing its server list
bool Steam_Matchmaking_Servers::IsRefreshing( HServerListRequest hRequest )
{
    PRINT_DEBUG("IsRefreshing %p\n", hRequest);
    std::cout << "IsRefreshing h: " << hRequest << std::endl;
    for (auto &r : requests) {
        if (r.id == hRequest && !r.cancelled && !r.responded && !r.finished_pushing) {
            return true;
        }
    }
    return false;
}

// How many servers in the given list, GetServerDetails above takes 0... GetServerCount() - 1
int Steam_Matchmaking_Servers::GetServerCount( HServerListRequest hRequest )
{
    PRINT_DEBUG("GetServerCount %p\n", hRequest);
    std::lock_guard<std::recursive_mutex> lock(global_mutex);
    int size = 0;
    auto r = std::begin(requests);
    while (r != std::end(requests)) {
        if (r->id == hRequest) {
            size = r->gameservers_filtered.size();
            break;
        }

        ++r;
    }

    return size;
}

// Refresh a single server inside of a query (rather than all the servers )
void Steam_Matchmaking_Servers::RefreshServer( HServerListRequest hRequest, int iServer )
{
    PRINT_DEBUG("RefreshServer %p\n", hRequest);
    std::cout << "RefreshServer int id: " << (int)hRequest << " i: " << iServer << std::endl;
    std::lock_guard<std::recursive_mutex> lock(global_mutex);
    for (auto &r : requests) {
        if (r.id == hRequest) {
            if (iServer < r.gameservers_filtered.size()) {
                Steam_Matchmaking_Servers_Gameserver gs_copy = r.gameservers_filtered[iServer];
                reactivate_request(r);
                std::thread([this, hRequest, gs_copy, iServer]() mutable {
                    RefreshSingleServer(gs_copy, iServer);
                    std::lock_guard<std::recursive_mutex> lock(global_mutex);

                    for (auto &req : requests) {
                        if (req.id == hRequest) {
                            req.gameservers_filtered[iServer] = gs_copy;
                            req.finished_pushing = true;
                            break;
                        }
                    }
                }).detach();
            }
            break;
        }

    }
}

void Steam_Matchmaking_Servers::RefreshSingleServer(Steam_Matchmaking_Servers_Gameserver &gs, int iServer) {
    std::string str_ip = ip_to_string(gs.server.ip());
    if (FetchServerData(str_ip, gs.server.port(), &gs.server)) {
        gs.last_recv = std::chrono::high_resolution_clock::now();
        gs.single_server_refresh = true;
        gs.list_position = iServer;
        std::lock_guard<std::recursive_mutex> lock(global_mutex);
        gameservers.push_back(gs);
        std::cout << "Refreshed server: " << str_ip << ":" << gs.server.port() << " appid: " << gs.server.appid() << std::endl;
    }
    else {
        std::cout << "No response from server while refreshing: " << str_ip << std::endl;
    }
}

void Steam_Matchmaking_Servers::reactivate_request(Steam_Matchmaking_Request &request) {
    std::cout << "Reactivate requ r.id: " << request.id << std::endl;
    request.cancelled = false;
    request.completed = false;
    request.finished_pushing = false;
    request.responded = false;
}

static HServerQuery new_server_query()
{
    static int a;
    ++a;
    if (!a) ++a;
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

void Steam_Matchmaking_Servers::ProcessPingRequest( uint32 unIP, uint16 usPort, HServerQuery id ) {
    std::thread::id this_id = std::this_thread::get_id();
    std::cout << "Thread ID ProcessPing: " << this_id << std::endl;
    std::string ip = ip_to_string(unIP);
    std::cout << "Ping with ip: " << ip << " id: " << id << std::endl;
    Gameserver server;
    FetchServerData(ip, usPort, &server);
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
}

// Request updated ping time and other details from a single server
HServerQuery Steam_Matchmaking_Servers::PingServer( uint32 unIP, uint16 usPort, ISteamMatchmakingPingResponse *pRequestServersResponse )
{
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
    std::thread worker(&Steam_Matchmaking_Servers::ProcessPingRequest, this, unIP, usPort, r.id);
    worker.detach();
    std::cout << "Pushed r: " << r.id << std::endl;
    return r.id;
}

void Steam_Matchmaking_Servers::ProcessPlayerRequest (HServerQuery id, uint32 unIP, uint16 usPort) {
    std::string ip = ip_to_string(unIP);
    HLDSQuery query(ip, usPort);

    for (auto &r : direct_ip_requests) {
        if (r.id == id) {
            query.get_players(&r.player_server_info);
            r.processed = true;
        }
    }
}

// Request the list of players currently playing on a server
HServerQuery Steam_Matchmaking_Servers::PlayerDetails( uint32 unIP, uint16 usPort, ISteamMatchmakingPlayersResponse *pRequestServersResponse )
{
    std::cout << "PlayerDetails " << unIP << " pt " << usPort << std::endl;
    PRINT_DEBUG("PlayerDetails %hhu.%hhu.%hhu.%hhu:%hu\n", ((unsigned char *)&unIP)[3], ((unsigned char *)&unIP)[2], ((unsigned char *)&unIP)[1], ((unsigned char *)&unIP)[0], usPort);
    std::thread::id this_id = std::this_thread::get_id();
    std::cout << "Thread ID PlayerDetails: " << this_id << std::endl;
    Steam_Matchmaking_Servers_Direct_IP_Request r;
    r.id = new_server_query();
    r.ip = unIP;
    r.port = usPort;
    r.players_response = pRequestServersResponse;
    r.created = std::chrono::high_resolution_clock::now();
    std::thread worker ([this, r, unIP, usPort]() {
        std::thread::id this_id = std::this_thread::get_id();
        std::cout << "Thread ID PlayerDetails-Inner: " << this_id << std::endl;
        std::lock_guard<std::recursive_mutex> lock(global_mutex);
        direct_ip_requests.push_back(r);
        std::cout << "Pushed r: " << r.id << std::endl;
        ProcessPlayerRequest(r.id, unIP, usPort);
    });
    worker.detach();
    return r.id;
}


// Request the list of rules that the server is running (See ISteamGameServer::SetKeyValue() to set the rules server side)
HServerQuery Steam_Matchmaking_Servers::ServerRules( uint32 unIP, uint16 usPort, ISteamMatchmakingRulesResponse *pRequestServersResponse )
{
    std::cout << "ServerRules " << unIP << " pt " << usPort << std::endl;
    PRINT_DEBUG("ServerRules %hhu.%hhu.%hhu.%hhu:%hu\n", ((unsigned char *)&unIP)[3], ((unsigned char *)&unIP)[2], ((unsigned char *)&unIP)[1], ((unsigned char *)&unIP)[0], usPort);
    std::lock_guard<std::recursive_mutex> lock(global_mutex);
    Steam_Matchmaking_Servers_Direct_IP_Request r;
    r.id = new_server_query();
    r.ip = unIP;
    r.port = usPort;
    r.rules_response = pRequestServersResponse;
    r.created = std::chrono::high_resolution_clock::now();
    direct_ip_requests.push_back(r);
    return r.id;
}


// Cancel an outstanding Ping/Players/Rules query from above.  You should call this to cancel
// any in-progress requests before destructing a callback object that may have been passed
// to one of the above calls to avoid crashing when callbacks occur.
void Steam_Matchmaking_Servers::CancelServerQuery( HServerQuery hServerQuery )
{
    PRINT_DEBUG("CancelServerQuery\n");
    std::lock_guard<std::recursive_mutex> lock(global_mutex);
    auto r = std::find_if(direct_ip_requests.begin(), direct_ip_requests.end(), [&hServerQuery](Steam_Matchmaking_Servers_Direct_IP_Request const& item) { return item.id == hServerQuery; });
    if (direct_ip_requests.end() == r) return;
    direct_ip_requests.erase(r);
}

void Steam_Matchmaking_Servers::ReadInput() {
    std::string value;
    std::cout << "Enter an integer: ";
    if (std::cin >> value) {
        if (value == "GS") {
            std::cout << "GS: " << std::endl;
            for (const auto& item : gameservers) {
                std::cout << item.type << " ";
                std::cout << "Name: " << item.server.server_name() << " ";
                std::cout << "Map: " << item.server.map_name() << " ";
                std::cout << "IP+Port " << item.server.ip() << " " << item.server.port() <<  " ";
            }
            std::cout << "]" << std::endl;
        }
    }
}

void Steam_Matchmaking_Servers::DebugListServers(const Steam_Matchmaking_Request &r, const std::string &label) {
    std::cout << "=== [" << label << "] Server List (Size: " << r.gameservers_filtered.size() << ") ===" << std::endl;

    for (size_t i = 0; i < r.gameservers_filtered.size(); ++i) {
        // Taking a constant reference to each server element
        const auto &gs = r.gameservers_filtered[i];

        // Assuming gs.server is the Protobuf object with getters
        std::cout << i << ". " << gs.server.server_name() << std::endl;
        std::cout << "   Addr: " << ip_to_string(gs.server.ip()) << ":" << gs.server.port()
                  << " | Ping: " << gs.server.latency() << "ms" << std::endl;
        //std::cout << "   Players: " << gs.server.players() << "/" << gs.server.max_players() << std::endl;
        std::cout << "   Map: " << gs.server.map_name() << " | AppID: " << gs.server.appid() << std::endl;
        std::cout << "------------------------------------------" << std::endl;
    }
}

void Steam_Matchmaking_Servers::RunCallbacks()
{
    PRINT_DEBUG("Steam_Matchmaking_Servers::RunCallbacks\n");
    std::lock_guard<std::recursive_mutex> lock(global_mutex);

    PRINT_DEBUG("REQUESTS %zu gs: %zu\n", requests.size(), gameservers.size());

    for (auto &r : requests) {
        if (r.responded) {
            //std::cout << "Responded: " << r.id << std::endl;
            continue;
        }

        if (r.completed || r.cancelled) {
            if (!r.gameservers_filtered.empty()) {
                r.callbacks->RefreshComplete(r.id, eServerResponded);
                std::cout << "Refresh complete (responded): " << r.finished_pushing << " r.gmf.size: " << r.gameservers_filtered.size() << std::endl;
                //DebugListServers(r, "RefreshComplete");
            } else {
                r.callbacks->RefreshComplete(r.id, eNoServersListedOnMasterServer);
                std::cout << "\nRefresh no-listed type: " << r.type << " id: " << r.id << std::endl;
            }
            r.responded = true;
            continue;
        }
        if (r.callbacks) {
            auto g = std::begin(gameservers);
            while (g != std::end(gameservers)) {
                if (check_timedout(g->last_recv, SERVER_TIMEOUT)) {
                    g = gameservers.erase(g);
                    PRINT_DEBUG("SERVER TIMEOUT\n");
                    std::cout << "Time out" << std::endl;
                } else {
                    if (g->type == r.type) {
                        char buffer[INET_ADDRSTRLEN];
                        struct in_addr addr_in;
                        addr_in.s_addr = g->server.ip();

                        if (inet_ntop(AF_INET, &addr_in, buffer, INET_ADDRSTRLEN)) {
                            std::string normal_ip(buffer);
                            std::cout << "Pushing back server (1): " << g->server.ip() << "(2)" << normal_ip << " i: " << r.i << " r.finished: " << r.finished_pushing << " r.comp: " << r.completed << " pre-r.gf: " << r.gameservers_filtered.size() << std::endl;
                        }
                        int item_position = 0;

                        if (g->single_server_refresh) {
                            item_position = g->list_position;
                            g->single_server_refresh = false;
                            g->list_position = 0;
                        }
                        else {
                            r.gameservers_filtered.push_back(std::move(*g));
                            item_position = r.gameservers_filtered.size() - 1;
                        }

                        r.callbacks->ServerResponded(r.id, item_position);
                        g = gameservers.erase(g);
                    } else {
                        ++g;
                    }
                }
            }
        }
        if (r.finished_pushing) {
            std::cout << "Finished pushing" << std::endl;
            r.completed = true;
        }
    }
    auto dip = std::begin(direct_ip_requests);
    while (dip != std::end(direct_ip_requests)) {
        if (!dip->processed && check_timedout(dip->created, DIRECT_IP_DELAY)) {
            dip = direct_ip_requests.erase(dip);
            std::cout << "DIP timeout" << std::endl;
        } else {
            if (dip->processed) {
                if (dip->ping_response) {
                    if (dip->ping_server_info.m_bHadSuccessfulResponse) {

                        dip->ping_response->ServerResponded(dip->ping_server_info);
                    }
                    else {
                        dip->ping_response->ServerFailedToRespond();

                    }
                    dip = direct_ip_requests.erase(dip);
                }
                if (dip->players_response) {
                    if (dip->player_server_info.finished) {
                        for (auto &player_info : dip->player_server_info.players) {
                            dip->players_response->AddPlayerToList(player_info.name.c_str(), player_info.score, player_info.time);
                        }
                        dip->players_response->PlayersRefreshComplete();
                    }
                    else {
                        dip->players_response->PlayersFailedToRespond();
                    }
                    dip = direct_ip_requests.erase(dip);
                }
            }
            else {
                ++dip;
            }
        }
    }
    /*for (auto &r : direct_ip_requests) {

        PRINT_DEBUG("dip request: %lu:%hu\n", r.ip, r.port);
        if (r.processed) continue;

        if (r.ping_response && r.processed == false) {
            if (r.ping_server_info.m_bHadSuccessfulResponse) {
                r.ping_response->ServerResponded(r.ping_server_info);
                std::cout << "*****************************Responded" << std::endl;
            }
            else {
                r.ping_response->ServerFailedToRespond();
                std::cout << "*****************************Failed to respond" << std::endl;
            }
            std::cout << "%%%%%%%%%% POST info: " << std::endl;
            std::cout << "Ping " << r.ping_server_info.m_nPing << std::endl;
            std::cout << "Name " << r.ping_server_info.GetName() << std::endl;
            r.processed = true;
        }*/
        /*for (auto &g : gameservers) {
            PRINT_DEBUG("server: %lu:%hu\n", g.server.ip(), g.server.query_port());
            uint16 query_port = g.server.query_port();
            if (query_port == 0xFFFF) {
                query_port = g.server.port();
            }

            if (query_port == r.port && g.server.ip() == r.ip) {
                if (r.rules_response) {
                    int number_rules = g.server.values().size();
                    PRINT_DEBUG("rules: %lu\n", number_rules);
                    auto rule = g.server.values().begin();
                    for (int i = 0; i < number_rules; ++i) {
                        PRINT_DEBUG("RULE %s %s\n", rule->first.c_str(), rule->second.c_str());
                        r.rules_response->RulesResponded(rule->first.c_str(), rule->second.c_str());
                        ++rule;
                    }

                    r.rules_response->RulesRefreshComplete();
                    r.rules_response = NULL;
                }

                if (r.ping_response) {
                    gameserveritem_t server;
                    server_details(&(g.server), &server);
                    r.ping_response->ServerResponded(server);
                    r.ping_response = NULL;
                }
            }
        }*/

        /*if (r.rules_response) r.rules_response->RulesRefreshComplete();
        if (r.players_response) r.players_response->PlayersRefreshComplete();
        if (r.ping_response) r.ping_response->ServerFailedToRespond();*/
    // }
}

void Steam_Matchmaking_Servers::Callback(Common_Message *msg) { // TODO: Recode to receive LAN servers when user request arrives
    if (msg->has_gameserver()) {
        std::cout << "Got new LAN server: " << msg->gameserver().id() << std::endl;
        std::lock_guard<std::recursive_mutex> lock(global_mutex);
        PRINT_DEBUG("got SERVER %llu, offline:%u\n", msg->gameserver().id(), msg->gameserver().offline());
        if (msg->gameserver().offline()) {
            std::cout << "Server offline" << std::endl;
            for (auto &g : gameservers_lan) {
                if (g.server.id() == msg->gameserver().id()) {
                    std::cout << "Found in list while offline" << std::endl;
                    g.last_recv = std::chrono::high_resolution_clock::time_point();
                }
            }
        } else {
            std::cout << "Online" << std::endl;
            bool already = false;
            for (auto &g : gameservers_lan) {
                if (g.server.id() == msg->gameserver().id()) {
                    std::cout << "Found in list while ONLINE" << std::endl;
                    g.last_recv = std::chrono::high_resolution_clock::now();
                    g.server = msg->gameserver();
                    g.server.set_ip(msg->source_ip());
                    g.type = eLANServer;
                    already = true;
                }
            }

            if (!already) {
                std::cout << "ONLINE and not already" << std::endl;
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

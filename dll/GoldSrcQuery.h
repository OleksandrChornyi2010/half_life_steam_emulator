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

#ifndef HLDS_QUERY_GoldSrcQuery_H
#define HLDS_QUERY_GoldSrcQuery_H

#include "base.h"

struct ServerItem;
struct PlayerServerResult;
class Gameserver;

class CallbackWorker {
  private:
    std::queue<std::function<void()>> task_queue;
    std::mutex worker_queue_mutex;
    std::condition_variable cv;
    std::atomic<bool> is_running;
    std::thread worker_thread;

    void WorkerLoop() {
        while (true) {
            std::function<void()> task;

            {
                std::unique_lock<std::mutex> lock(worker_queue_mutex);

                cv.wait(lock, [this]() {
                    return !task_queue.empty() || !is_running;
                });

                if (!is_running && task_queue.empty()) {
                    return;
                }

                task = std::move(task_queue.front());
                task_queue.pop();
            }

            if (task) {
                task();
            }
        }
    }

  public:
    CallbackWorker() : is_running(true) {
        worker_thread = std::thread(&CallbackWorker::WorkerLoop, this);
    }

    ~CallbackWorker() {
        is_running = false;
        cv.notify_one();

        if (worker_thread.joinable()) {
            worker_thread.join();
        }
    }

    void EnqueueTask(std::function<void()> task) {
        {
            std::lock_guard<std::mutex> lock(worker_queue_mutex);
            task_queue.push(std::move(task));
        }
        cv.notify_one();
    }
};

class GoldSrcQuery {
  public:
    GoldSrcQuery();

    ~GoldSrcQuery();

    static void GetServersFromMasterServer(const std::string &masterDomain, int masterPort, const std::string &filter, std::vector<ServerItem> &out, std::recursive_mutex &out_mutex, std::atomic<bool> &cancel_flag);

    void GetServerInfo(const std::string &ip, uint16_t port, std::function<void(const Gameserver &)> on_response);

    void GetServerPlayers(const std::string &ip, uint16_t port, PlayerServerResult *result);
    void GetServerRules(const std::string &ip, uint16_t port);

  private:
    const time_t LOOP_INTERVAL_SECONDS = 0;
    const suseconds_t LOOP_INTERVAL_MICROSECONDS = 50 * 100;
    const size_t MAX_ASYNC_CONCURRENT_QUERIES = 75;
    static const uint16_t REQUEST_TIMEOUT = 2500;
    const int SOCKET_BUFFER_SIZE = 1024 * 1024;

    CallbackWorker callback_worker;

    std::string read_string(uint8_t *&ptr, uint8_t *end);
    template <typename T>
    T read_num(uint8_t *&ptr, uint8_t *end) {
        if (ptr + sizeof(T) > end)
            return 0;
        T val = *reinterpret_cast<T *>(ptr);
        ptr += sizeof(T);
        return val;
    }

    void parse_info_buffer(uint8_t *buffer, ssize_t res, Gameserver *out_data, const std::string &ip, uint16_t port);
    ssize_t query_with_challenge_sync(int sock, const sockaddr_in &addr, uint8_t type, uint8_t *buffer, size_t size);
    struct AsyncRequest {
        uint32_t ip_net;
        uint16_t port_net;
        std::function<void(const Gameserver &)> callback;
    };

    struct PendingState {
        std::chrono::time_point<std::chrono::steady_clock> start;
        std::function<void(const Gameserver &)> callback;
    };

    int m_async_sock;
    std::atomic<bool> is_running;
    std::thread worker_thread;
    std::mutex queue_mutex;

    std::vector<AsyncRequest> request_queue;

    void AsyncWorkerLoop();
};

#endif // HLDS_QUERY_GoldSrcQuery_H
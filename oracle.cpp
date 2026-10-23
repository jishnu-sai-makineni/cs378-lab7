#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#define DEFAULT_PORT 5000
#define BACKLOG 26
#define WAIT_TIME 10
#define MAX_NODES 26
#define TUPLE_SIZE 13
#define MAX_MSG_SIZE (1 + MAX_NODES * TUPLE_SIZE)

struct VNInfo {
    std::string ip;
    int port = 0;
    int fd = -1;
};

static bool read_config(const char *path, std::vector<std::vector<int>> &costs) {
    std::ifstream fin(path);
    if (!fin) {
        std::cerr << "Cannot open config file: " << path << "\n";
        return false;
    }

    costs.clear();
    std::string line;
    while (std::getline(fin, line)) {
        size_t first = line.find_first_not_of(" \t\r\n");
        if (first == std::string::npos || line[first] == '#') continue;
        std::stringstream ss(line);
        std::vector<int> row;
        int value;
        while (ss >> value) row.push_back(value);
        if (!ss.eof()) {
            std::cerr << "Invalid value in config line: " << line << "\n";
            return false;
        }
        if (!row.empty()) costs.push_back(row);
    }

    if (costs.empty() || costs.size() >= MAX_NODES) {
        std::cerr << "Config must describe between 2 and 26 nodes.\n";
        return false;
    }
    const size_t n = costs.size() + 1;
    for (size_t i = 0; i < costs.size(); ++i) {
        if (costs[i].size() != n - i - 1) {
            std::cerr << "Config row " << (i + 1) << " should contain "
                      << (n - i - 1) << " costs, but contains "
                      << costs[i].size() << ".\n";
            return false;
        }
        for (int c : costs[i]) {
            if (c == 0 || c < -1 || c > 65535) {
                std::cerr << "Costs must be -1 (no link) or positive integers <= 65535.\n";
                return false;
            }
        }
    }
    return true;
}

static bool recv_all(int fd, unsigned char *buf, size_t len) {
    size_t done = 0;
    while (done < len) {
        ssize_t n = recv(fd, buf + done, len - done, 0);
        if (n == 0) return false;
        if (n < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        done += static_cast<size_t>(n);
    }
    return true;
}

static bool send_all(int fd, const unsigned char *buf, size_t len) {
    size_t done = 0;
    while (done < len) {
        ssize_t n = send(fd, buf + done, len - done, 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        done += static_cast<size_t>(n);
    }
    return true;
}

/* CONNECT format retained from the current Oracle: IPv4 bytes + native 4-byte int port. */
static bool decode_connect_msg(const unsigned char buf[8], VNInfo &vn) {
    struct in_addr addr;
    int port;
    memcpy(&addr.s_addr, buf, 4);
    memcpy(&port, buf + 4, 4);
    char ip[INET_ADDRSTRLEN];
    if (!inet_ntop(AF_INET, &addr, ip, sizeof(ip)) || port == 0 || port > 65535)
        return false;
    vn.ip = ip;
    vn.port = static_cast<int>(port);
    return true;
}

/* LINK-STATE: 1-byte count; tuple = ID(1), IPv4(4), port(2), cost(2). */
static bool encode_link_state(const std::vector<std::pair<char, int>> &node_costs,
                              const std::vector<VNInfo> &vns,
                              unsigned char buf[MAX_MSG_SIZE], size_t &len) {
    if (node_costs.size() > MAX_NODES) return false;
    memset(buf, 0, MAX_MSG_SIZE);
    buf[0] = static_cast<unsigned char>(node_costs.size());
    size_t off = 1;
    for (const auto &entry : node_costs) {
        int node = entry.first - 'A';
        int cost = entry.second;
        if (node < 0 || node >= static_cast<int>(vns.size()) || cost < 0 || cost > 65535)
            return false;
        buf[off++] = static_cast<unsigned char>(entry.first);
        struct in_addr ip;
        if (inet_pton(AF_INET, vns[node].ip.c_str(), &ip) != 1) return false;
        memcpy(buf + off, &ip.s_addr, 4); off += 4;
        int port_wire = vns[node].port;
        int cost_wire = cost;
        memcpy(buf + off, &port_wire, 4); off += 4;
        memcpy(buf + off, &cost_wire, 4); off += 4;
    }
    len = off;
    return true;
}

static int link_cost(const std::vector<std::vector<int>> &costs, int i, int j) {
    if (i == j) return 0;
    int a = std::min(i, j), b = std::max(i, j);
    return costs[a][b - a - 1];
}

static bool send_node_state(int i, const std::vector<std::vector<int>> &costs,
                            const std::vector<VNInfo> &vns) {
    std::vector<std::pair<char, int>> entries;
    entries.push_back({static_cast<char>('A' + i), 0});
    for (int j = 0; j < static_cast<int>(vns.size()); ++j) {
        if (j == i) continue;
        int cost = link_cost(costs, i, j);
        if (cost > 0) entries.push_back({static_cast<char>('A' + j), cost});
    }
    unsigned char buf[MAX_MSG_SIZE];
    size_t len = 0;
    if (!encode_link_state(entries, vns, buf, len)) {
        std::cerr << "Failed to encode LINK-STATE for node " << char('A' + i) << "\n";
        return false;
    }
    return send_all(vns[i].fd, buf, len);
}

int main(int argc, char *argv[]) {
    if (argc < 2 || argc > 3) {
        std::cerr << "Usage: " << argv[0] << " <config-file> [listen-port]\n";
        return 1;
    }
    int listen_port = DEFAULT_PORT;
    if (argc == 3) {
        listen_port = std::stoi(argv[2]);
        if (listen_port < 1 || listen_port > 65535) {
            std::cerr << "Listen port must be in 1..65535\n";
            return 1;
        }
    }

    std::vector<std::vector<int>> costs;
    if (!read_config(argv[1], costs)) return 1;
    const int n = static_cast<int>(costs.size()) + 1;

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) { perror("socket"); return 1; }
    int reuse = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    struct sockaddr_in my_addr{};
    my_addr.sin_family = AF_INET;
    my_addr.sin_port = htons(listen_port);
    my_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(sockfd, reinterpret_cast<struct sockaddr *>(&my_addr), sizeof(my_addr)) < 0) {
        perror("bind"); close(sockfd); return 1;
    }
    if (listen(sockfd, BACKLOG) < 0) { perror("listen"); close(sockfd); return 1; }

    std::vector<VNInfo> vns(n);
    std::cout << "Oracle listening on all IPv4 interfaces, TCP port " << listen_port
              << "; waiting for " << n << " virtual nodes\n";

    for (int i = 0; i < n; ++i) {
        struct sockaddr_in peer{};
        socklen_t peer_len = sizeof(peer);
        int fd = accept(sockfd, reinterpret_cast<struct sockaddr *>(&peer), &peer_len);
        if (fd < 0) { perror("accept"); close(sockfd); return 1; }
        unsigned char connect_buf[8];
        if (!recv_all(fd, connect_buf, sizeof(connect_buf)) || !decode_connect_msg(connect_buf, vns[i])) {
            std::cerr << "Invalid CONNECT for node " << char('A' + i) << "\n";
            close(fd); --i; continue;
        }
        vns[i].fd = fd;
        std::cout << char('A' + i) << " connected at " << vns[i].ip << ":" << vns[i].port << "\n";
    }
    

    for (int i = 0; i < n; ++i) {
        if (!send_node_state(i, costs, vns))
            std::cerr << "Failed to send initial LINK-STATE to " << char('A' + i) << "\n";
    }

    while (true) {
        sleep(WAIT_TIME);
        std::vector<std::vector<int>> updated;
        if (!read_config(argv[1], updated) || updated.size() + 1 != vns.size()) {
            std::cerr << "Could not reload config or node count changed; keeping old topology.\n";
            continue;
        }
        if (updated == costs) continue;
        costs = updated;
        std::cout << "Topology changed; refreshing all nodes.\n";
        for (int i = 0; i < n; ++i) {
            if (!send_node_state(i, costs, vns))
                std::cerr << "Failed to refresh node " << char('A' + i) << "\n";
        }
    }
}
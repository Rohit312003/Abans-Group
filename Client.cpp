#include <iostream>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <vector>
#include <map>
#include <sstream>
#include <fcntl.h>
#include <cstring>
#include <fstream>
#include <thread>  // For std::this_thread::sleep_for
#include <chrono>  // For std::chrono::milliseconds


#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 3000
#define TIMEOUT_MS 300   
#define POLL_DELAY_MS 100
struct Packet {
    std::string symbol;
    char buySell;
    int quantity;
    int price;
    int sequence;
};

// Save JSON output to file
void saveToFile(const std::string& jsonOutput, const std::string& filename) {
    std::ofstream outFile(filename);
    if (outFile.is_open()) {
        outFile << jsonOutput;
        outFile.close();
        std::cout << "Output saved to " << filename << std::endl;
    } else {
        std::cerr << "Failed to write to file " << filename << std::endl;
    }
}

// Parse received packet
Packet parsePacket(const char* buffer) {
    Packet packet;
    packet.symbol = std::string(buffer, 4);
    packet.buySell = buffer[4];
    packet.quantity = ntohl(*reinterpret_cast<const int*>(buffer + 5));
    packet.price = ntohl(*reinterpret_cast<const int*>(buffer + 9));
    packet.sequence = ntohl(*reinterpret_cast<const int*>(buffer + 13));
    return packet;
}

// Generate JSON output
std::string generateJSON(const std::vector<Packet>& packets) {
    std::ostringstream json;
    json << "[\n";
    for (size_t i = 0; i < packets.size(); ++i) {
        json << "  {\n";
        json << "    \"symbol\": \"" << packets[i].symbol << "\",\n";
        json << "    \"buySell\": \"" << packets[i].buySell << "\",\n";
        json << "    \"quantity\": " << packets[i].quantity << ",\n";
        json << "    \"price\": " << packets[i].price << ",\n";
        json << "    \"sequence\": " << packets[i].sequence << "\n";
        json << "  }" << (i < packets.size() - 1 ? "," : "") << "\n";
    }
    json << "]";
    return json.str();
}

// Open a persistent connection to the server
int connectToServer() {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        std::cerr << "Socket creation failed\n";
        return -1;
    }

    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = inet_addr(SERVER_IP);
    serverAddr.sin_port = htons(SERVER_PORT);

    if (connect(sock, (sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        std::cerr << "Connection failed\n";
        close(sock);
        return -1;
    }

    // Set the socket to non-blocking mode
    fcntl(sock, F_SETFL, O_NONBLOCK);

    return sock;
}

// Fetch packets from the server using epoll
std::vector<Packet> GetPackets(int sock, char callType, int resendSeq = 0) {
    std::vector<Packet> packets;

    // Set up epoll
    int epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) {
        std::cerr << "Failed to create epoll instance\n";
        return packets;
    }

    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLET;  // Edge-triggered mode, wait for input
    ev.data.fd = sock;

    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, sock, &ev) == -1) {
        std::cerr << "Failed to add socket to epoll\n";
        close(epoll_fd);
        return packets;
    }

    // Send request to the server
    char request[2] = { callType, static_cast<char>(resendSeq) };
    if (send(sock, request, sizeof(request), 0) < 0) {
        std::cerr << "Send failed\n";
        close(epoll_fd);
        return packets;
    }

    char buffer[17];  // Each packet is expected to be 17 bytes
    while (true) {
        struct epoll_event events[1];  // Array to hold events
        int n = epoll_wait(epoll_fd, events, 1, TIMEOUT_MS);

        if (n == 0) {
            std::cout << "Timeout: No more data received.\n";
            break;
        } else if (n < 0) {
            std::cerr << "epoll_wait error\n";
            break;
        }

        // Read data from the socket
        if (events[0].events & EPOLLIN) {
            int bytesReceived = recv(sock, buffer, sizeof(buffer), 0);

            // Handle EAGAIN or EWOULDBLOCK errors
      // Handle EAGAIN or EWOULDBLOCK errors
            if (bytesReceived < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    // No data available, continue the loop with a delay
                    std::this_thread::sleep_for(std::chrono::milliseconds(POLL_DELAY_MS));  // Add delay
                    continue;
                } else {
                    std::cerr << "Recv error\n";
                    break;
                }
            }

            if (bytesReceived == 0) {
                std::cout << "Connection closed by server.\n";
                break;
            }

            // Data received successfully
            
            packets.push_back(parsePacket(buffer));
        }
    
    }

    close(epoll_fd);
    return packets;
}

int main() {
    // Open a single connection to the server
    int sock = connectToServer();
    if (sock < 0) {
        return 1;
    }

    // Fetch all packets initially
    std::vector<Packet> packets = GetPackets(sock, 1);

    // Track received sequence numbers
    std::map<int, Packet> packetMap;
    /*if we take maxSeq >14 the server crashed so i take maxSeq=14 also same for min sequence so 
      so i take minSeq=1
    */

    int minSeq = 1, maxSeq = 14;

    for (const auto& p : packets) {
        packetMap[p.sequence] = p;
    }

    // Identify missing sequences
    std::vector<int> missingSequences;
    for (int seq = minSeq; seq <= maxSeq; ++seq) {
        if (packetMap.find(seq) == packetMap.end()) {
            missingSequences.push_back(seq);
        }
    }

    // Close the connection
    close(sock);

    // Open a new connection to fetch missing packets
    sock = connectToServer();
    if (sock < 0) {
        return 1;
    }

    // Request missing packets
    for (int seq : missingSequences) {
        std::vector<Packet> missingPacket = GetPackets(sock, 2, seq);
        if (!missingPacket.empty()) {
            packetMap[seq] = missingPacket[0];
        } else {
            std::cerr << "Warning: Sequence " << seq << " could not be retrieved.\n";
        }
    }

    // Close the connection
    close(sock);

    // Store packets in sorted order (including missing ones)
    std::vector<Packet> sortedPackets;
    for (auto& it : packetMap) {
        sortedPackets.push_back(it.second);  // Ensures all sequences are in order
    }

    // Output JSON with sorted sequences
    std::cout << generateJSON(sortedPackets) << std::endl;
    std::string jsonOutput = generateJSON(sortedPackets);
    saveToFile(jsonOutput, "output.json");

    return 0;
}

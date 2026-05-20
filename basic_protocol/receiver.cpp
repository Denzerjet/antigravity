#include <iostream>
#include <string>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

// Struct to represent an individual bit and its coordinate index (0 to 8)
struct BitPacket {
    int index;
    int value;
};

constexpr int PORT = 8080;
constexpr int BUFFER_SIZE = 1024;

int main() {
    // 1. Create UDP Socket
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        std::cerr << "[-] Error creating socket\n";
        return 1;
    }
    std::cout << "[+] Socket created successfully\n";

    // 2. Set socket options to allow address/port reuse
    int opt = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        std::cerr << "[-] Error setting socket options\n";
        close(sockfd);
        return 1;
    }

    // Set socket receive timeout to 3 seconds for reassembly/timer-out purposes
    struct timeval tv;
    tv.tv_sec = 3;
    tv.tv_usec = 0;
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
        std::cerr << "[-] Error setting socket timeout option\n";
        close(sockfd);
        return 1;
    }
    std::cout << "[+] Socket receive timeout set to 3 seconds\n";

    // 3. Define the server address structure
    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    if (inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr) <= 0) {
        std::cerr << "[-] Invalid address/Address not supported\n";
        close(sockfd);
        return 1;
    }
    server_addr.sin_port = htons(PORT);

    // 4. Bind the socket to the port
    if (bind(sockfd, reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr)) < 0) {
        std::cerr << "[-] Error binding to port " << PORT << "\n";
        close(sockfd);
        return 1;
    }
    std::cout << "[+] Bound to 127.0.0.1:" << PORT << "\n";
    std::cout << "[*] Listening for incoming bit-by-bit transmissions...\n\n";

    // 5. Main loop to receive individual bits and print grid at the end
    int grid[3][3] = {};
    bool received_bits[9] = {false};
    bool active_transmission = false;
    char buffer[BUFFER_SIZE];

    while (true) {
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);

        std::memset(buffer, 0, BUFFER_SIZE);

        // Block until a bit packet is received (with timeout)
        ssize_t bytes_received = recvfrom(
            sockfd,
            buffer,
            BUFFER_SIZE,
            0,
            reinterpret_cast<sockaddr*>(&client_addr),
            &client_len
        );

        if (bytes_received < 0) {
            // Check if error is due to a timeout (EAGAIN/EWOULDBLOCK)
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                if (active_transmission) {
                    std::cout << "\n[!] Timeout waiting for remaining packets. Reconstructing grid using adjacent cell scanning...\n";

                    // Create a copy of the received grid to use as the baseline
                    int original_grid[3][3];
                    std::memcpy(original_grid, grid, sizeof(grid));

                    // 4-connected adjacent offsets: Up, Down, Left, Right
                    const int dr[] = {-1, 1, 0, 0};
                    const int dc[] = {0, 0, -1, 1};

                    for (int i = 0; i < 9; ++i) {
                        if (!received_bits[i]) {
                            int r = i / 3;
                            int c = i % 3;

                            int total_neighbors = 0;
                            int ones_count = 0;

                            for (int d = 0; d < 4; ++d) {
                                int nr = r + dr[d];
                                int nc = c + dc[d];

                                // Check bounds
                                if (nr >= 0 && nr < 3 && nc >= 0 && nc < 3) {
                                    total_neighbors++;
                                    if (original_grid[nr][nc] == 1) {
                                        ones_count++;
                                    }
                                }
                            }

                            // If over half are 1s, or in a tie, use 1. Otherwise 0.
                            double threshold = total_neighbors / 2.0;
                            if (ones_count >= threshold) {
                                grid[r][c] = 1;
                                std::cout << "[*] Reconstructed missing index " << i << " (row " << r << ", col " << c 
                                          << ") as 1 (neighbors: " << ones_count << "/" << total_neighbors << " were 1s)\n";
                            } else {
                                grid[r][c] = 0;
                                std::cout << "[*] Reconstructed missing index " << i << " (row " << r << ", col " << c 
                                          << ") as 0 (neighbors: " << ones_count << "/" << total_neighbors << " were 1s)\n";
                            }
                        }
                    }

                    std::cout << "[+] Reconstructed 3x3 array:\n";
                    for (int i = 0; i < 3; ++i) {
                        std::cout << "    ";
                        for (int j = 0; j < 3; ++j) {
                            std::cout << grid[i][j] << "\t";
                        }
                        std::cout << "\n";
                    }
                    std::cout << "\n[*] Resetting receiver state for next transmission.\n\n";

                    // Reset state
                    std::memset(received_bits, 0, sizeof(received_bits));
                    std::memset(grid, 0, sizeof(grid));
                    active_transmission = false;
                }
            } else {
                std::cerr << "[-] Error in recvfrom()\n";
            }
            continue;
        }

        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
        int client_port = ntohs(client_addr.sin_port);

        // Verify the received packet size matches a BitPacket
        constexpr size_t expected_size = sizeof(BitPacket);
        if (static_cast<size_t>(bytes_received) != expected_size) {
            std::cerr << "[-] Received unexpected payload size of " << bytes_received 
                      << " bytes from [" << client_ip << ":" << client_port 
                      << "] (expected " << expected_size << " bytes for BitPacket)\n\n";
            continue;
        }

        // Deserialize BitPacket
        BitPacket packet;
        std::memcpy(&packet, buffer, expected_size);

        if (packet.index < 0 || packet.index >= 9) {
            std::cerr << "[-] Received invalid bit index: " << packet.index << "\n";
            continue;
        }

        // We have successfully started/continued an active transmission
        active_transmission = true;

        // Store bit and mark as received
        grid[packet.index / 3][packet.index % 3] = packet.value;
        received_bits[packet.index] = true;

        std::cout << "[+] Received bit at index " << packet.index << " -> value: " << packet.value 
                  << " from [" << client_ip << ":" << client_port << "]\n";

        // 6. Send individual acknowledgment back to client
        std::string ack_msg = "Ack bit " + std::to_string(packet.index);
        ssize_t bytes_sent = sendto(
            sockfd,
            ack_msg.c_str(),
            ack_msg.length(),
            0,
            reinterpret_cast<sockaddr*>(&client_addr),
            client_len
        );

        if (bytes_sent < 0) {
            std::cerr << "[-] Failed to send acknowledgment for bit " << packet.index << "\n";
        }

        // Check if all 9 bits have been received
        bool all_received = true;
        for (int i = 0; i < 9; ++i) {
            if (!received_bits[i]) {
                all_received = false;
                break;
            }
        }

        // Once all 9 elements are received, print the complete 3x3 array
        if (all_received) {
            std::cout << "\n[+] All 9 bits received! Complete 3x3 array:\n";
            for (int i = 0; i < 3; ++i) {
                std::cout << "    ";
                for (int j = 0; j < 3; ++j) {
                    std::cout << grid[i][j] << "\t";
                }
                std::cout << "\n";
            }
            std::cout << "\n[*] Resetting receiver state for next transmission.\n\n";

            // Reset state
            std::memset(received_bits, 0, sizeof(received_bits));
            std::memset(grid, 0, sizeof(grid));
            active_transmission = false;
        }
    }

    close(sockfd);
    return 0;
}

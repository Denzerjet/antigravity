#include <iostream>
#include <string>
#include <cstring>
#include <sstream>
#include <fstream>
#include <vector>
#include <algorithm>
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
constexpr int TIMEOUT_SEC = 2;

int main(int argc, char* argv[]) {
    // Validate command-line arguments
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <grid_file_path>\n";
        return 1;
    }

    std::string grid_file_path = argv[1];

    // 1. Create UDP Socket
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        std::cerr << "[-] Error creating socket\n";
        return 1;
    }
    std::cout << "[+] Socket created successfully\n";

    // 2. Set a receive timeout so recvfrom doesn't block indefinitely
    struct timeval tv;
    tv.tv_sec = TIMEOUT_SEC;
    tv.tv_usec = 0;
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
        std::cerr << "[-] Error setting socket timeout option\n";
        close(sockfd);
        return 1;
    }
    std::cout << "[+] Timeout set to " << TIMEOUT_SEC << " seconds\n";

    // 3. Define the destination server address structure
    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    if (inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr) <= 0) {
        std::cerr << "[-] Invalid address/Address not supported\n";
        close(sockfd);
        return 1;
    }
    server_addr.sin_port = htons(PORT);

    // 4. Open and parse the 3x3 grid and the fail indices from the text file
    std::ifstream infile(grid_file_path);
    if (!infile.is_open()) {
        std::cerr << "[-] Error: Could not open grid file: " << grid_file_path << "\n";
        close(sockfd);
        return 1;
    }

    int grid[3][3] = {};
    for (int i = 0; i < 9; ++i) {
        if (!(infile >> grid[i / 3][i % 3])) {
            std::cerr << "[-] Error: Failed to read 9 integers from grid file.\n";
            close(sockfd);
            return 1;
        }
    }

    // Read any remaining integers in the file as indices to fail
    std::vector<int> fail_indices;
    int idx;
    while (infile >> idx) {
        if (idx >= 0 && idx < 9) {
            fail_indices.push_back(idx);
        } else {
            std::cerr << "[!] Warning: Ignored invalid fail index " << idx << " (must be between 0 and 8).\n";
        }
    }
    infile.close();

    std::cout << "\nLoaded the following 3x3 grid from file \"" << grid_file_path << "\":\n";
    for (int i = 0; i < 3; ++i) {
        std::cout << "    ";
        for (int j = 0; j < 3; ++j) {
            std::cout << grid[i][j] << "\t";
        }
        std::cout << "\n";
    }

    std::cout << "\nPackets scheduled to fail transmission: ";
    if (fail_indices.empty()) {
        std::cout << "None\n";
    } else {
        for (size_t i = 0; i < fail_indices.size(); ++i) {
            std::cout << fail_indices[i] << (i + 1 < fail_indices.size() ? ", " : "");
        }
        std::cout << "\n";
    }
    std::cout << "\n[*] Starting sequential transmission of each bit...\n\n";

    // 5. Send each bit individually, waiting for its acknowledgment but not retrying
    char buffer[BUFFER_SIZE];

    for (int i = 0; i < 9; ++i) {
        BitPacket packet = { i, grid[i / 3][i % 3] };

        // Check if we need to intentionally fail/drop this packet
        bool should_fail = (std::find(fail_indices.begin(), fail_indices.end(), i) != fail_indices.end());
        if (should_fail) {
            std::cout << "[->] Intentionally dropping transmission for bit index " << i << " (simulating network packet loss)...\n";
        } else {
            std::cout << "[->] Sending bit index " << i << " (value: " << packet.value << ")... ";
            ssize_t bytes_sent = sendto(
                sockfd,
                &packet,
                sizeof(packet),
                0,
                reinterpret_cast<const sockaddr*>(&server_addr),
                sizeof(server_addr)
            );

            if (bytes_sent < 0) {
                std::cerr << "\n[-] Error: Failed to send bit index " << i << "\n";
                close(sockfd);
                return 1;
            }
        }

        // Block waiting for acknowledgment (with receive timeout)
        sockaddr_in from_addr{};
        socklen_t from_len = sizeof(from_addr);
        std::memset(buffer, 0, BUFFER_SIZE);

        ssize_t bytes_received = recvfrom(
            sockfd,
            buffer,
            BUFFER_SIZE - 1,
            0,
            reinterpret_cast<sockaddr*>(&from_addr),
            &from_len
        );

        if (bytes_received < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                std::cout << "[!] Timeout waiting for acknowledgment of bit " << i << ". Proceeding to next bit without retry.\n";
            } else {
                std::cerr << "\n[-] Error in recvfrom()\n";
                close(sockfd);
                return 1;
            }
        } else {
            buffer[bytes_received] = '\0';
            std::cout << "Success! Ack received: \"" << buffer << "\"\n";
        }
    }

    std::cout << "\n[+] Finished transmission sequence for all 9 bits.\n";

    // 6. Close the socket
    close(sockfd);
    std::cout << "[+] Socket closed. Exiting.\n";
    return 0;
}

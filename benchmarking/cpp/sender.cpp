#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include <iostream>
#include <string>
#include <vector>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstdlib>
#include <ctime>

constexpr int PORT = 8081;
constexpr int BUFFER_SIZE = 1024;
constexpr int CHUNK_SIZE = 1024;
constexpr int TIMEOUT_MS = 100; // 100 milliseconds for responsive local benchmarking

struct MetadataPacket {
    int width;
    int height;
    int channels;
    int total_chunks;
    int chunk_size;
};

struct ChunkPacket {
    int chunk_index;
    int data_length;
    char payload[CHUNK_SIZE];
};

int main(int argc, char* argv[]) {
    std::srand(std::time(nullptr));
    std::string input_path = "images/input_image.png";
    bool unreliable = false;

    // Parse command line arguments
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--unreliable" || arg == "-u") {
            unreliable = true;
        } else {
            input_path = arg;
        }
    }

    // 1. Load the PNG image using stb_image
    int width = 0, height = 0, channels = 0;
    std::cout << "[*] Loading image \"" << input_path << "\"... ";
    unsigned char* pixels = stbi_load(input_path.c_str(), &width, &height, &channels, 0);
    if (!pixels) {
        std::cerr << "Failed!\n[-] Error: Could not load image file.\n";
        return 1;
    }
    std::cout << "Success!\n"
              << "[+] Dimensions: " << width << " x " << height << " (" << channels << " channels)\n";

    size_t total_bytes = static_cast<size_t>(width) * height * channels;
    int total_chunks = (total_bytes + CHUNK_SIZE - 1) / CHUNK_SIZE;
    std::cout << "[+] Total Size: " << total_bytes << " bytes (" << total_chunks << " chunks)\n";
    if (unreliable) {
        std::cout << "[!] Mode: UNRELIABLE (UDP stream with no acknowledgments/resends)\n";
    } else {
        std::cout << "[+] Mode: RELIABLE (Stop-and-Wait ARQ with resends)\n";
    }

    // 2. Create UDP Socket
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        std::cerr << "[-] Error creating socket\n";
        stbi_image_free(pixels);
        return 1;
    }
    std::cout << "[+] Socket created successfully\n";

    // 3. Set a short receive timeout (100ms) for high-frequency retry reliability
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = TIMEOUT_MS * 1000;
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
        std::cerr << "[-] Error setting socket timeout option\n";
        close(sockfd);
        stbi_image_free(pixels);
        return 1;
    }
    std::cout << "[+] Timeout set to " << TIMEOUT_MS << " milliseconds\n";

    // 4. Define server address
    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    if (inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr) <= 0) {
        std::cerr << "[-] Invalid address/Address not supported\n";
        close(sockfd);
        stbi_image_free(pixels);
        return 1;
    }
    server_addr.sin_port = htons(PORT);

    char buffer[BUFFER_SIZE];

    // A. Phase 1: Transmit Metadata reliably (essential to set up receiver's memory buffer)
    MetadataPacket meta = { width, height, channels, total_chunks, CHUNK_SIZE };
    bool meta_acknowledged = false;

    std::cout << "\n[*] Initiating transmission of Metadata...\n";
    while (!meta_acknowledged) {
        ssize_t bytes_sent = sendto(
            sockfd,
            &meta,
            sizeof(MetadataPacket),
            0,
            reinterpret_cast<const sockaddr*>(&server_addr),
            sizeof(server_addr)
        );

        if (bytes_sent < 0) {
            std::cerr << "[-] Error sending metadata packet\n";
            close(sockfd);
            stbi_image_free(pixels);
            return 1;
        }

        // Wait for Ack Metadata
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

        if (bytes_received >= 0) {
            buffer[bytes_received] = '\0';
            if (std::string(buffer) == "Ack Metadata") {
                std::cout << "[+] Metadata acknowledged successfully!\n";
                meta_acknowledged = true;
            }
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                std::cout << "[!] Metadata acknowledgment timeout. Retransmitting...\n";
            } else {
                std::cerr << "[-] Error in recvfrom()\n";
                close(sockfd);
                stbi_image_free(pixels);
                return 1;
            }
        }
    }

    // B. Phase 2: Transmit Data Chunks
    if (unreliable) {
        std::cout << "\n[*] Initiating UNRELIABLE data chunk transmission...\n";
        for (int i = 0; i < total_chunks; ++i) {
            // Prepare ChunkPacket
            ChunkPacket chunk;
            chunk.chunk_index = i;

            size_t offset = static_cast<size_t>(i) * CHUNK_SIZE;
            size_t current_chunk_size = std::min(static_cast<size_t>(CHUNK_SIZE), total_bytes - offset);
            chunk.data_length = static_cast<int>(current_chunk_size);
            std::memcpy(chunk.payload, pixels + offset, current_chunk_size);

            ssize_t bytes_sent = sendto(
                sockfd,
                &chunk,
                sizeof(ChunkPacket),
                0,
                reinterpret_cast<const sockaddr*>(&server_addr),
                sizeof(server_addr)
            );

            if (bytes_sent < 0) {
                std::cerr << "\n[-] Error sending chunk index " << i << "\n";
                close(sockfd);
                stbi_image_free(pixels);
                return 1;
            }

            // Standard pacing delay for unreliable fire-and-forget streams
            usleep(500);

            if ((i + 1) % 100 == 0 || (i + 1) == total_chunks) {
                std::cout << "[*] Progress: " << (i + 1) << " / " << total_chunks 
                          << " chunks sent (unreliable mode) (" << ((i + 1) * 100 / total_chunks) << "%)\n";
            }
        }
    } else {
        std::cout << "\n[*] Initiating RELIABLE data chunk transmission...\n";
        for (int i = 0; i < total_chunks; ++i) {
            // Prepare ChunkPacket
            ChunkPacket chunk;
            chunk.chunk_index = i;

            size_t offset = static_cast<size_t>(i) * CHUNK_SIZE;
            size_t current_chunk_size = std::min(static_cast<size_t>(CHUNK_SIZE), total_bytes - offset);
            chunk.data_length = static_cast<int>(current_chunk_size);
            std::memcpy(chunk.payload, pixels + offset, current_chunk_size);

            bool chunk_acknowledged = false;
            std::string expected_ack = "Ack " + std::to_string(i);

            while (!chunk_acknowledged) {
                ssize_t bytes_sent = sendto(
                    sockfd,
                    &chunk,
                    sizeof(ChunkPacket),
                    0,
                    reinterpret_cast<const sockaddr*>(&server_addr),
                    sizeof(server_addr)
                );

                if (bytes_sent < 0) {
                    std::cerr << "\n[-] Error sending chunk index " << i << "\n";
                    close(sockfd);
                    stbi_image_free(pixels);
                    return 1;
                }

                // Wait for specific Ack
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

                if (bytes_received >= 0) {
                    buffer[bytes_received] = '\0';
                    if (std::string(buffer) == expected_ack) {
                        chunk_acknowledged = true;
                    }
                } else {
                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        // Packet or ack was dropped, retry
                    } else {
                        std::cerr << "\n[-] Error in recvfrom()\n";
                        close(sockfd);
                        stbi_image_free(pixels);
                        return 1;
                    }
                }
            }

            if ((i + 1) % 50 == 0 || (i + 1) == total_chunks) {
                std::cout << "[*] Progress: " << (i + 1) << " / " << total_chunks 
                          << " chunks sent and acknowledged (" << ((i + 1) * 100 / total_chunks) << "%)\n";
            }
        }
    }

    std::cout << "\n[+] Image transmission completed successfully!\n";

    // 5. Clean up
    close(sockfd);
    stbi_image_free(pixels);
    std::cout << "[+] Socket closed and memory freed. Exiting.\n";
    return 0;
}

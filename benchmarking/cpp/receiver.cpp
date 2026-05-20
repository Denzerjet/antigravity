#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
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
constexpr int BUFFER_SIZE = 2048;
constexpr int CHUNK_SIZE = 1024;

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

int main() {
    std::srand(std::time(nullptr));
    // 1. Create UDP Socket
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        std::cerr << "[-] Error creating socket\n";
        return 1;
    }
    std::cout << "[+] Socket created successfully\n";

    // 2. Allow port reuse
    int opt = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        std::cerr << "[-] Error setting socket options\n";
        close(sockfd);
        return 1;
    }

    // 3. Bind socket to port
    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    if (bind(sockfd, reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr)) < 0) {
        std::cerr << "[-] Error binding to port " << PORT << "\n";
        close(sockfd);
        return 1;
    }
    std::cout << "[+] Bound to port " << PORT << "\n";

    // 4. Set a receive timeout (2 seconds) to handle stream end in unreliable mode
    struct timeval tv;
    tv.tv_sec = 2;
    tv.tv_usec = 0;
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
        std::cerr << "[-] Error setting socket timeout option\n";
        close(sockfd);
        return 1;
    }
    std::cout << "[+] Socket receive timeout set to 2 seconds\n";
    std::cout << "[*] Awaiting image metadata...\n";

    // State variables
    bool metadata_received = false;
    int width = 0, height = 0, channels = 0, total_chunks = 0;
    std::vector<unsigned char> pixel_buffer;
    std::vector<bool> received_chunks;
    int chunks_collected = 0;

    sockaddr_in client_addr{};
    socklen_t client_len = sizeof(client_addr);
    char buffer[BUFFER_SIZE];

    // 5. Main reception loop
    while (true) {
        std::memset(buffer, 0, BUFFER_SIZE);
        ssize_t bytes_received = recvfrom(
            sockfd,
            buffer,
            BUFFER_SIZE,
            0,
            reinterpret_cast<sockaddr*>(&client_addr),
            &client_len
        );

        if (bytes_received < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                if (metadata_received) {
                    std::cout << "\n[!] Timeout waiting for remaining chunks (unreliable transmission ended).\n";
                    std::cout << "[*] Collected " << chunks_collected << " / " << total_chunks 
                              << " chunks (" << (chunks_collected * 100 / total_chunks) << "%)\n";
                    break;
                }
            } else {
                std::cerr << "[-] Error in recvfrom()\n";
            }
            continue;
        }

        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
        int client_port = ntohs(client_addr.sin_port);

        // A. Handle Metadata Phase
        if (!metadata_received) {
            if (static_cast<size_t>(bytes_received) == sizeof(MetadataPacket)) {
                MetadataPacket meta;
                std::memcpy(&meta, buffer, sizeof(MetadataPacket));

                width = meta.width;
                height = meta.height;
                channels = meta.channels;
                total_chunks = meta.total_chunks;

                std::cout << "[+] Metadata received from [" << client_ip << ":" << client_port << "]:\n"
                          << "    Dimensions: " << width << " x " << height << " (" << channels << " channels)\n"
                          << "    Total Chunks: " << total_chunks << " (" << meta.chunk_size << " bytes each)\n";

                // Allocate buffers
                pixel_buffer.assign(width * height * channels, 0);
                received_chunks.assign(total_chunks, false);
                metadata_received = true;

                // Send Ack Metadata
                std::string ack_msg = "Ack Metadata";
                sendto(sockfd, ack_msg.c_str(), ack_msg.length(), 0,
                       reinterpret_cast<sockaddr*>(&client_addr), client_len);
            }
            continue;
        }

        // B. Handle Data Chunk Phase
        if (static_cast<size_t>(bytes_received) == sizeof(ChunkPacket)) {
            ChunkPacket chunk;
            std::memcpy(&chunk, buffer, sizeof(ChunkPacket));

            // Simulating packet loss on the receiver side: drop 50% of received chunks randomly
            if (std::rand() % 100 < 50) {
                std::cout << "[!] Simulated receiver drop: chunk " << chunk.chunk_index << " ignored.\n";
                continue;
            }

            if (chunk.chunk_index < 0 || chunk.chunk_index >= total_chunks) {
                std::cerr << "[-] Received invalid chunk index: " << chunk.chunk_index << "\n";
                continue;
            }

            // Standard duplicate packet handling
            if (!received_chunks[chunk.chunk_index]) {
                received_chunks[chunk.chunk_index] = true;
                chunks_collected++;

                // Calculate offset and copy payload
                size_t offset = static_cast<size_t>(chunk.chunk_index) * CHUNK_SIZE;
                std::memcpy(pixel_buffer.data() + offset, chunk.payload, chunk.data_length);

                if (chunks_collected % 50 == 0 || chunks_collected == total_chunks) {
                    std::cout << "[*] Progress: " << chunks_collected << " / " << total_chunks 
                              << " chunks collected (" << (chunks_collected * 100 / total_chunks) << "%)\n";
                }
            }

            // Always acknowledge the chunk (so sender stops retransmitting if ack was lost)
            std::string ack_msg = "Ack " + std::to_string(chunk.chunk_index);
            sendto(sockfd, ack_msg.c_str(), ack_msg.length(), 0,
                   reinterpret_cast<sockaddr*>(&client_addr), client_len);
        } else if (static_cast<size_t>(bytes_received) == sizeof(MetadataPacket)) {
            // If sender missed our Metadata ack and retransmitted the metadata packet
            std::string ack_msg = "Ack Metadata";
            sendto(sockfd, ack_msg.c_str(), ack_msg.length(), 0,
                   reinterpret_cast<sockaddr*>(&client_addr), client_len);
        }

        // C. Break if complete
        if (chunks_collected == total_chunks) {
            std::cout << "\n[+] All chunks received successfully!\n";
            break;
        }
    }

    // 5. Interpolate/Fill in the blanks using averages of adjacent filled pixels (5 iterations)
    if (metadata_received && chunks_collected < total_chunks) {
        std::cout << "\n[*] Processing image reconstruction (interpolating missing pixels over 5 iterations)...\n";
        
        // Initialize a pixel-level filled state vector
        std::vector<bool> pixel_filled(static_cast<size_t>(width) * height, false);
        for (int r = 0; r < height; ++r) {
            for (int c = 0; c < width; ++c) {
                size_t byte_offset = (static_cast<size_t>(r) * width + c) * channels;
                int chunk_idx = static_cast<int>(byte_offset / CHUNK_SIZE);
                if (chunk_idx < total_chunks && received_chunks[chunk_idx]) {
                    pixel_filled[static_cast<size_t>(r) * width + c] = true;
                }
            }
        }

        // Run 5 iterations of boundary-propagation interpolation
        for (int iter = 1; iter <= 5; ++iter) {
            std::vector<unsigned char> interpolated_buffer = pixel_buffer;
            std::vector<bool> next_pixel_filled = pixel_filled;
            int filled_count = 0;

            for (int r = 0; r < height; ++r) {
                for (int c = 0; c < width; ++c) {
                    size_t idx = static_cast<size_t>(r) * width + c;
                    
                    // If this pixel is currently blank/unfilled, try to interpolate it
                    if (!pixel_filled[idx]) {
                        size_t byte_offset = idx * channels;

                        // Check orthogonal neighbors: Up, Down, Left, Right
                        int dr[] = {-1, 1, 0, 0};
                        int dc[] = {0, 0, -1, 1};

                        long long sum_r = 0;
                        long long sum_g = 0;
                        long long sum_b = 0;
                        int valid_neighbors = 0;

                        for (int i = 0; i < 4; ++i) {
                            int nr = r + dr[i];
                            int nc = c + dc[i];

                            if (nr >= 0 && nr < height && nc >= 0 && nc < width) {
                                size_t n_idx = static_cast<size_t>(nr) * width + nc;

                                // The neighbor is "filled" if it was received or previously interpolated
                                if (pixel_filled[n_idx]) {
                                    size_t neighbor_offset = n_idx * channels;
                                    sum_r += pixel_buffer[neighbor_offset + 0];
                                    sum_g += pixel_buffer[neighbor_offset + 1];
                                    sum_b += pixel_buffer[neighbor_offset + 2];
                                    valid_neighbors++;
                                }
                            }
                        }

                        // If we found at least one valid non-blank neighbor, fill in with the average
                        if (valid_neighbors > 0) {
                            interpolated_buffer[byte_offset + 0] = static_cast<unsigned char>(sum_r / valid_neighbors);
                            interpolated_buffer[byte_offset + 1] = static_cast<unsigned char>(sum_g / valid_neighbors);
                            interpolated_buffer[byte_offset + 2] = static_cast<unsigned char>(sum_b / valid_neighbors);
                            next_pixel_filled[idx] = true;
                            filled_count++;
                        }
                    }
                }
            }

            pixel_buffer = std::move(interpolated_buffer);
            pixel_filled = std::move(next_pixel_filled);
            std::cout << "[+] Iteration " << iter << ": successfully interpolated " << filled_count << " blank pixels.\n";

            // If we didn't fill any more pixels, we can early exit
            if (filled_count == 0) {
                std::cout << "[*] Convergence reached. No more pixels to interpolate.\n";
                break;
            }
        }
    }

    // 6. Write the pixel buffer to output_image.png
    std::string output_path = "images/output_image.png";
    std::cout << "[*] Saving reconstructed pixels to \"" << output_path << "\"... ";
    int success = stbi_write_png(
        output_path.c_str(),
        width,
        height,
        channels,
        pixel_buffer.data(),
        width * channels
    );

    if (success) {
        std::cout << "Success!\n";
    } else {
        std::cerr << "Failed!\n";
    }

    close(sockfd);
    return 0;
}

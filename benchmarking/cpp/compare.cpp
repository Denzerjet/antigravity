#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include <iostream>
#include <string>
#include <cmath>
#include <iomanip>

int main(int argc, char* argv[]) {
    std::string img1_path = "images/input_image.png";
    std::string img2_path = "images/output_image.png";

    if (argc >= 3) {
        img1_path = argv[1];
        img2_path = argv[2];
    }

    std::cout << "[*] Loading \"" << img1_path << "\"...\n";
    int w1, h1, c1;
    unsigned char* pixels1 = stbi_load(img1_path.c_str(), &w1, &h1, &c1, 0);
    if (!pixels1) {
        std::cerr << "[-] Failed to load image: " << img1_path << "\n";
        return 1;
    }

    std::cout << "[*] Loading \"" << img2_path << "\"...\n";
    int w2, h2, c2;
    unsigned char* pixels2 = stbi_load(img2_path.c_str(), &w2, &h2, &c2, 0);
    if (!pixels2) {
        std::cerr << "[-] Failed to load image: " << img2_path << "\n";
        stbi_image_free(pixels1);
        return 1;
    }

    if (w1 != w2 || h1 != h2 || c1 != c2) {
        std::cerr << "[-] Error: Image dimensions or channels do not match!\n"
                  << "    Image 1: " << w1 << "x" << h1 << " (" << c1 << " channels)\n"
                  << "    Image 2: " << w2 << "x" << h2 << " (" << c2 << " channels)\n";
        stbi_image_free(pixels1);
        stbi_image_free(pixels2);
        return 1;
    }

    std::cout << "[+] Dimensions match: " << w1 << "x" << h1 << " with " << c1 << " channels.\n\n";

    size_t total_pixels = static_cast<size_t>(w1) * h1;
    size_t total_values = total_pixels * c1;
    size_t identical_pixels = 0;
    double sum_squared_error = 0.0;
    size_t different_pixels = 0;

    for (size_t i = 0; i < total_pixels; ++i) {
        size_t offset = i * c1;
        bool pixel_match = true;

        for (int ch = 0; ch < c1; ++ch) {
            double diff = static_cast<double>(pixels1[offset + ch]) - static_cast<double>(pixels2[offset + ch]);
            sum_squared_error += diff * diff;
            if (pixels1[offset + ch] != pixels2[offset + ch]) {
                pixel_match = false;
            }
        }

        if (pixel_match) {
            identical_pixels++;
        } else {
            different_pixels++;
        }
    }

    double mse = sum_squared_error / total_values;
    double psnr = (mse > 0.0) ? (20.0 * std::log10(255.0 / std::sqrt(mse))) : INFINITY;
    double match_percentage = (static_cast<double>(identical_pixels) / total_pixels) * 100.0;

    std::cout << "==================================================\n"
              << "             IMAGE DIFFERENCE ANALYSIS            \n"
              << "==================================================\n"
              << "  Total Pixels Analyzed : " << total_pixels << "\n"
              << "  Exact Match Pixels   : " << identical_pixels << "\n"
              << "  Different Pixels     : " << different_pixels << "\n"
              << std::fixed << std::setprecision(4)
              << "  Pixel Match Rate     : " << match_percentage << " %\n"
              << "  Mean Squared Error   : " << mse << "\n"
              << "  Peak Signal-to-Noise : ";
    
    if (std::isinf(psnr)) {
        std::cout << "Perfect (0 dB error / 100% Identical)\n";
    } else {
        std::cout << psnr << " dB\n";
    }
    std::cout << "==================================================\n";

    stbi_image_free(pixels1);
    stbi_image_free(pixels2);
    return 0;
}

#include "ContinuousPeakEnvelope.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>

int main(int argc, char** argv) {
    if (argc != 4) return 2;
    std::ifstream input(argv[1], std::ios::binary | std::ios::ate);
    if (!input) return 3;
    const auto bytes = input.tellg();
    if (bytes <= 0 || bytes % sizeof(double) != 0) return 4;
    std::vector<double> x(static_cast<size_t>(bytes) / sizeof(double));
    input.seekg(0);
    input.read(reinterpret_cast<char*>(x.data()), bytes);
    if (!input || x.size() < 2) return 5;
    std::vector<double> y(x.size() - 1);
    tide::lab::ContinuousPeakEnvelope envelope;
    envelope.prepare(std::stod(argv[3]), 0.0046);
    const auto started = std::chrono::steady_clock::now();
    for (size_t i = 0; i < y.size(); ++i) y[i] = envelope.process(x[i], x[i + 1]);
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    std::ofstream output(argv[2], std::ios::binary);
    output.write(reinterpret_cast<const char*>(y.data()), y.size() * sizeof(double));
    if (!output) return 6;
    std::cout << "{\"samples\":" << y.size() << ",\"seconds\":" << seconds << "}\n";
}

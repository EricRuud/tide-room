#include "P821FeatureExtractor.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <vector>

int main(int argc, char** argv) {
    using namespace tide::p821lab;
    if (argc != 4 && argc != 5) return 2;
    FeatureExtractor::FilterBank coefficients;
    std::ifstream coeffFile(argv[1],std::ios::binary);
    if (!coeffFile.read(reinterpret_cast<char*>(coefficients.data()),sizeof(coefficients))) return 3;
    std::ifstream inputFile(argv[2],std::ios::binary|std::ios::ate);
    const auto size = inputFile.tellg(); if (size <= 0 || size % (sizeof(float)*512) != 0) return 4;
    std::vector<float> input(static_cast<size_t>(size)/sizeof(float));inputFile.seekg(0);
    if (!inputFile.read(reinterpret_cast<char*>(input.data()),size)) return 5;
    std::vector<FeatureExtractor::Frame> result(input.size()/512);
    FeatureExtractor::Coefficients ac {1.,0.,0.,0.,0.};
    if (argc==5) {std::ifstream file(argv[4],std::ios::binary);if (!file.read(reinterpret_cast<char*>(ac.data()),sizeof(ac))) return 7;}
    FeatureExtractor extractor(coefficients,argc==5,ac);
    const auto start = std::chrono::steady_clock::now();
    for (size_t j=0;j<result.size();++j) result[j] = extractor.process(input.data()+j*512);
    const auto seconds = std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
    std::ofstream output(argv[3],std::ios::binary);
    for (const auto& frame : result) {
        for (const auto& channel : frame.control) output.write(reinterpret_cast<const char*>(channel.data()),sizeof(channel));
        output.write(reinterpret_cast<const char*>(frame.width.data()),sizeof(frame.width));
    }
    if (!output) return 6;
    std::cout << "{\"frames\":" << result.size() << ",\"seconds\":" << seconds << "}\n";
}

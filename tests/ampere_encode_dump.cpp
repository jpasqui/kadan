// Writes the encoder's test cases for nvdisasm: <out>.bin (one instruction per case, each disassembled on its own)
// and <out>.txt (case number, hex words, expected text). Used to pin tests/ampere_encode_test.cpp.
#include "ampere_encode_cases.h"
#include <cstdio>
#include <fstream>

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    const auto cases = ampere_cases::All();
    std::ofstream bin(std::string(argv[1]) + ".bin", std::ios::binary);
    std::ofstream txt(std::string(argv[1]) + ".txt");
    for (std::size_t i = 0; i < cases.size(); ++i) {
        bin.write(reinterpret_cast<const char*>(cases[i].word.words.data()), 16);
        char line[96];
        std::snprintf(line, sizeof line, "%zu\t%08x %08x %08x %08x\t", i, cases[i].word.words[0], cases[i].word.words[1],
                      cases[i].word.words[2], cases[i].word.words[3]);
        txt << line << cases[i].text << "\n";
    }
    return 0;
}

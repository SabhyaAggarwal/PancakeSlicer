#include <iosfwd>
#include <vector>
#include <iostream>
#include <fstream>
#include <cstdlib>
#include <string>

unsigned char *utf8_check(unsigned char *s)
{
    while (*s) {
        if (*s < 0x80) {
            s++;
        } else if ((s[0] & 0xe0) == 0xc0) {
            if ((s[1] & 0xc0) != 0x80 ||
                (s[0] & 0xfe) == 0xc0) {
                return s;
            } else {
                s += 2;
            }
        } else if ((s[0] & 0xf0) == 0xe0) {
            if ((s[1] & 0xc0) != 0x80 ||
                (s[2] & 0xc0) != 0x80 ||
                (s[0] == 0xe0 && (s[1] & 0xe0) == 0x80) ||
                (s[0] == 0xed && (s[1] & 0xe0) == 0xa0) ||
                (s[0] == 0xef && s[1] == 0xbf &&
                (s[2] & 0xfe) == 0xbe)) {
                return s;
            } else {
                s += 3;
            }
        } else if ((s[0] & 0xf8) == 0xf0) {
            if ((s[1] & 0xc0) != 0x80 ||
                (s[2] & 0xc0) != 0x80 ||
                (s[3] & 0xc0) != 0x80 ||
                (s[0] == 0xf0 && (s[1] & 0xf0) == 0x80) ||
                (s[0] == 0xf4 && s[1] > 0x8f) || s[0] > 0xf4) {
                return s;
            } else {
                s += 4;
            }
        } else {
            return s;
        }
    }

    return NULL;
}

bool check_file(const char* target, const char* filename)
{
    std::ifstream file(filename, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        std::cerr << "\n\tError: Could not open file: " << filename
                  << "\n\tTarget: " << target << "\n" << std::endl;
        return false;
    }

    const std::streampos pos = file.tellg();

    if (pos < 0) {
        std::cerr << "\n\tError: Could not determine file size: " << filename
                  << "\n\tTarget: " << target << "\n" << std::endl;
        return false;
    }

    const auto size = static_cast<std::size_t>(pos);

    if (size == 0)
        return true;

    file.seekg(0, std::ios::beg);
    std::vector<char> buffer(size);

    if (!file.read(buffer.data(), static_cast<std::streamsize>(size))) {
        std::cerr << "\n\tError: Could not read source file: " << filename
                  << "\n\tTarget: " << target << "\n" << std::endl;
        return false;
    }

    buffer.push_back('\0');

    if (utf8_check(reinterpret_cast<unsigned char*>(buffer.data())) != nullptr) {
        std::cerr << "\n\tError: Source file does not contain (valid) UTF-8: " << filename
                  << "\n\tTarget: " << target << "\n" << std::endl;
        return false;
    }

    if (buffer.size() >= 3
        && buffer[0] == '\xef'
        && buffer[1] == '\xbb'
        && buffer[2] == '\xbf') {
        std::cerr << "\n\tError: Source file is valid UTF-8 but contains a BOM mark: " << filename
                  << "\n\tTarget: " << target << "\n" << std::endl;
        return false;
    }

    return true;
}

int main(int argc, char const *argv[])
{
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <target> <file|@responsefile> [...]" << std::endl;
        return -1;
    }

    const char* target = argv[1];
    
    std::vector<std::string> files;
    for (int i = 2; i < argc; i++) {
        if (argv[i][0] == '@') {
            std::ifstream rsp(argv[i] + 1);
            if (!rsp.is_open()) {
                std::cerr << "Could not open response file: " << (argv[i]+1) << std::endl;
                return -1;
            }
            std::string line;
            while (std::getline(rsp, line)) {
                if (!line.empty())
                    files.push_back(line);
            }
        } else {
            files.push_back(argv[i]);
        }
    }

    bool all_ok = true;
    for (const auto& f : files) {
        if (!check_file(target, f.c_str()))
            all_ok = false;
    }

    return all_ok ? 0 : -2;
}
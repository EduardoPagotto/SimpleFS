#include "sfs/disk.hpp"
#include <filesystem>
#include <format>
#include <iostream>
#include <string.h>

void Disk::open(const char* path, size_t nblocks) {

    if (std::filesystem::exists(path)) {
        file.open(path, std::ios::binary | std::ios::in | std::ios::out);
    } else {
        file.open(path, std::ios::binary | std::ios::in | std::ios::out | std::ios::trunc);
    }

    if (!file.is_open())
        throw std::runtime_error(strerror(errno));

    // get length of file:
    file.seekg(0, file.end);
    int length = file.tellg();

    std::cout << std::format("disk size: {}", length) << std::endl;

    Blocks = nblocks;
    Reads = 0;
    Writes = 0;
}

Disk::~Disk() {
    if (file.is_open()) {
        std::cout << std::format("{0} disk block reads", Reads) << std::endl;
        std::cout << std::format("{0} disk block writes", Writes) << std::endl;
        file.close();
    }
}

void Disk::sanity_check(int blocknum, char* data) {

    if (blocknum < 0)
        throw std::invalid_argument(std::format("blocknum (%d) is negative!", blocknum));

    if (blocknum >= (int)Blocks)
        throw std::invalid_argument(std::format("blocknum (%d) is too big!", blocknum));

    if (data == nullptr)
        throw std::invalid_argument("nullptr data pointer!");
}

void Disk::read(int blocknum, char* data) {
    sanity_check(blocknum, data);

    const size_t pos = blocknum * BLOCK_SIZE;

    if (!file.seekg(pos))
        throw std::runtime_error(std::format("Unable to lseek %d: %s", blocknum, strerror(errno)));

    if (!file.read(data, BLOCK_SIZE))
        throw std::runtime_error(std::format("Unable to read % d : % s ", blocknum, strerror(errno)));

    Reads++;
}

void Disk::write(int blocknum, char* data) {
    sanity_check(blocknum, data);

    const size_t pos = blocknum * BLOCK_SIZE;
    if (!file.seekp(pos))
        throw std::runtime_error(std::format("Unable to lseek %d: %s", blocknum, strerror(errno)));

    if (!file.write(data, BLOCK_SIZE))
        throw std::runtime_error(std::format("Unable to write %d: %s", blocknum, strerror(errno)));

    Writes++;
}

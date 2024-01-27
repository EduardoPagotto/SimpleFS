#include "sfs/disk.hpp"
#include <filesystem>
#include <format>
#include <iostream>
#include <string.h>

void Disk::open(const char* path, size_t nblocks) {

    if (std::filesystem::exists(path)) {
        m_file.open(path, std::ios::binary | std::ios::in | std::ios::out);
    } else {
        m_file.open(path, std::ios::binary | std::ios::in | std::ios::out | std::ios::trunc);
    }

    if (!m_file.is_open())
        throw std::runtime_error(strerror(errno));

    // get length of m_file:
    m_file.seekg(0, m_file.end);
    int length = m_file.tellg();

    std::cout << std::format("disk size: {}", length) << std::endl;

    m_nBlock = nblocks;
    m_nReads = 0;
    m_nWrites = 0;
}

Disk::~Disk() {
    if (m_file.is_open()) {
        std::cout << std::format("{0} disk block reads", m_nReads) << std::endl;
        std::cout << std::format("{0} disk block writes", m_nWrites) << std::endl;
        m_file.close();
    }
}

void Disk::sanity_check(int blocknum, char* data) {

    if (blocknum < 0)
        throw std::invalid_argument(std::format("blocknum (%d) is negative!", blocknum));

    if (blocknum >= m_nBlock)
        throw std::invalid_argument(std::format("blocknum (%d) is too big!", blocknum));

    if (data == nullptr)
        throw std::invalid_argument("nullptr data pointer!");
}

void Disk::read(int blocknum, char* data) {
    sanity_check(blocknum, data);

    const size_t pos = blocknum * DISK_BLOCK_SIZE;

    if (!m_file.seekg(pos))
        throw std::runtime_error(std::format("Unable to lseek %d: %s", blocknum, strerror(errno)));

    if (!m_file.read(data, DISK_BLOCK_SIZE))
        throw std::runtime_error(std::format("Unable to read % d : % s ", blocknum, strerror(errno)));

    m_nReads++;
}

void Disk::write(int blocknum, char* data) {
    sanity_check(blocknum, data);

    const size_t pos = blocknum * DISK_BLOCK_SIZE;
    if (!m_file.seekp(pos))
        throw std::runtime_error(std::format("Unable to lseek %d: %s", blocknum, strerror(errno)));

    if (!m_file.write(data, DISK_BLOCK_SIZE))
        throw std::runtime_error(std::format("Unable to write %d: %s", blocknum, strerror(errno)));

    m_nWrites++;
}

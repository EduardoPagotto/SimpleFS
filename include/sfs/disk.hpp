#pragma once
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <string.h>

#define DISK_BLOCK_SIZE 512 // 1024 // 4096

class Disk {
  private:
    std::fstream m_file;    // File descriptor of disk image
    uint32_t m_nBlock = 0;  // Number of blocks in disk image
    uint32_t m_nReads = 0;  // Number of reads performed
    uint32_t m_nWrites = 0; // Number of writes performed
    uint32_t m_nMounts = 0; // Number of mounts

  public:
    Disk() = default;
    ~Disk() {
        if (m_file.is_open()) {
            std::cout << std::format("{0} disk block reads", m_nReads) << std::endl;
            std::cout << std::format("{0} disk block writes", m_nWrites) << std::endl;
            m_file.close();
        }
    }

    /**
     * @brief Open file as FS disk image
     *
     * @param path Path to disk image
     * @param nblocks Number of blocks in disk image
     * @throw runtime_error exception on error.
     */
    void open(const char* path, const uint32_t& nblocks) {
        if (std::filesystem::exists(path)) {
            m_file.open(path, std::ios::binary | std::ios::in | std::ios::out);
        } else {
            m_file.open(path, std::ios::binary | std::ios::in | std::ios::out | std::ios::trunc);
        }

        if (!m_file.is_open())
            throw std::runtime_error(strerror(errno));

        // get length of m_file:
        m_file.seekg(0, m_file.end);
        const uint32_t length = m_file.tellg();
        std::cout << std::format("disk size: {}", length) << std::endl;

        m_nBlock = nblocks;
        m_nReads = 0;
        m_nWrites = 0;
    }

    /**
     * @brief Get size of disk (in terms of blocks)
     *
     * @return uint32_t
     */
    const uint32_t size() const { return m_nBlock; }

    /**
     * @brief Whether or not disk is mounted
     *
     * @return true Monted
     * @return false not monted
     */
    const bool mounted() const { return m_nMounts > 0; }

    /**
     * @brief Increment mounts
     *
     */
    void mount() { m_nMounts++; }

    /**
     * @brief Decrement mounts
     *
     */
    void unmount() {
        if (m_nMounts > 0)
            m_nMounts--;
    }

    /**
     * @brief Read block from disk
     *
     * @param blocknum Block to read from
     * @param data Buffer to read into
     */
    void read(const uint32_t& blocknum, char* data) {
        sanity_check(blocknum, data);

        const uint32_t pos = blocknum * DISK_BLOCK_SIZE;

        if (!m_file.seekg(pos))
            throw std::runtime_error(std::format("Unable to seek %d: %s", blocknum, strerror(errno)));

        if (!m_file.read(data, DISK_BLOCK_SIZE))
            throw std::runtime_error(std::format("Unable to read % d : % s ", blocknum, strerror(errno)));

        m_nReads++;
    }

    /**
     * @brief Write block to disk
     *
     * @param blocknum Block to write to
     * @param data Buffer to write from
     */
    void write(const uint32_t& blocknum, char* data) {
        sanity_check(blocknum, data);

        const uint32_t pos = blocknum * DISK_BLOCK_SIZE;
        if (!m_file.seekp(pos))
            throw std::runtime_error(std::format("Unable to seek %d: %s", blocknum, strerror(errno)));

        if (!m_file.write(data, DISK_BLOCK_SIZE))
            throw std::runtime_error(std::format("Unable to write %d: %s", blocknum, strerror(errno)));

        m_nWrites++;
    }

  private:
    /**
     * @brief Check parameters
     *
     * @param blocknum  Block to operate on
     * @param data Buffer to operate on
     * @throw invalid_argument exception on error.
     */
    void sanity_check(const uint32_t& blocknum, char* data) {
        if (blocknum < 0)
            throw std::invalid_argument(std::format("blocknum (%d) is negative!", blocknum));

        if (blocknum >= m_nBlock)
            throw std::invalid_argument(std::format("blocknum (%d) is too big!", blocknum));

        if (data == nullptr)
            throw std::invalid_argument("nullptr data pointer!");
    }
};

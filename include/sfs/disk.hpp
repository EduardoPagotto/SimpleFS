#pragma once
// #include <cstdio>
// #include <stdlib.h>
#include <fstream>

class Disk {
  private:
    std::fstream file; // File descriptor of disk image
    size_t Blocks = 0; // Number of blocks in disk image
    size_t Reads = 0;  // Number of reads performed
    size_t Writes = 0; // Number of writes performed
    size_t Mounts = 0; // Number of mounts

    /**
     * @brief Check parameters
     *
     * @param blocknum  Block to operate on
     * @param data Buffer to operate on
     * @throw invalid_argument exception on error.
     */
    void sanity_check(int blocknum, char* data);

  public:
    /**
     * @brief Number of bytes per block
     *
     */
    const static size_t BLOCK_SIZE = 512; // 1024; // 4096;

    Disk() = default;
    ~Disk();

    /**
     * @brief
     *
     * @param path Path to disk image
     * @param nblocks Number of blocks in disk image
     * @throw runtime_error exception on error.
     */
    void open(const char* path, size_t nblocks);

    /**
     * @brief Get size of disk (in terms of blocks)
     *
     * @return size_t
     */
    size_t size() const { return Blocks; }

    /**
     * @brief Whether or not disk is mounted
     *
     * @return true Monted
     * @return false not monted
     */
    bool mounted() const { return Mounts > 0; }

    /**
     * @brief Increment mounts
     *
     */
    void mount() { Mounts++; }

    /**
     * @brief Decrement mounts
     *
     */
    void unmount() {
        if (Mounts > 0)
            Mounts--;
    }

    /**
     * @brief Read block from disk
     *
     * @param blocknum Block to read from
     * @param data Buffer to read into
     */
    void read(int blocknum, char* data);

    /**
     * @brief Write block to disk
     *
     * @param blocknum Block to write to
     * @param data Buffer to write from
     */
    void write(int blocknum, char* data);
};

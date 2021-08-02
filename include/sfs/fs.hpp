#ifndef __FS_HPP
#define __FS_HPP

#include "sfs/disk.hpp"

#include <stdint.h>
#include <vector>

class FileSystem {
  public:
    const static uint32_t MAGIC_NUMBER = 0xf0f03410;
    const static uint32_t INODES_PER_BLOCK = 128;
    const static uint32_t POINTERS_PER_INODE = 5;
    const static uint32_t POINTERS_PER_BLOCK = 1024;
    // extra to dir
    const static uint32_t NAMESIZE = 16;
    const static uint32_t ENTRIES_PER_DIR = 7;
    const static uint32_t DIR_PER_BLOCK = 8;

    FileSystem() : mounted(false), fs_disk(nullptr) {}
    virtual ~FileSystem() {}

  private:
    struct SuperBlock {       // Superblock structure
        uint32_t MagicNumber; // File system magic number
        uint32_t Blocks;      // Number of blocks in file system
        uint32_t InodeBlocks; // Number of blocks reserved for inodes
        // dirs
        uint32_t DirBlocks;
        // normal
        uint32_t Inodes; // Number of inodes in file system
        // protect root
        uint32_t Protected;
        char PasswordHash[257];
    };

    struct Dirent {
        uint8_t type;
        uint8_t valid;
        uint32_t inum;
        char Name[NAMESIZE];
    };

    struct Directory {
        uint16_t Valid;
        uint32_t inum;
        char Name[NAMESIZE];
        Dirent Table[ENTRIES_PER_DIR];
    };

    struct Inode {
        uint32_t Valid;                      // Whether or not inode is valid
        uint32_t Size;                       // Size of file
        uint32_t Direct[POINTERS_PER_INODE]; // Direct pointers
        uint32_t Indirect;                   // Indirect pointer
    };

    union Block {
        SuperBlock Super;                      // Superblock
        Inode Inodes[INODES_PER_BLOCK];        // Inode block
        uint32_t Pointers[POINTERS_PER_BLOCK]; // Pointer block
        char Data[Disk::BLOCK_SIZE];           // Data block
        struct Directory Directories[FileSystem::DIR_PER_BLOCK];
    };

  public:
    static void debug(Disk* disk);
    static bool format(Disk* disk);

    bool mount(Disk* disk);

    ssize_t create();
    bool remove(size_t inumber);
    ssize_t stat(size_t inumber);

    ssize_t read(size_t inumber, char* data, size_t length, size_t offset);
    ssize_t write(size_t inumber, char* data, size_t length, size_t offset);

  private:
    bool load_inode(size_t inumber, Inode* node);

    uint32_t allocate_block();
    bool check_allocation(Inode* node, int read, int orig_offset, uint32_t& blocknum, bool write_indirect, Block indirect);
    void read_buffer(int offset, int* read, int length, char* data, uint32_t blocknum);
    ssize_t write_ret(size_t inumber, Inode* node, int ret);
    void read_helper(uint32_t blocknum, int offset, size_t* length, char** data, char** ptr);

    bool mounted;
    Disk* fs_disk;
    SuperBlock MetaData;
    std::vector<bool> free_blocks;
    std::vector<int> inode_counter;
};

#endif

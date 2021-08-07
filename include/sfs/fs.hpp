#ifndef __FS_HPP
#define __FS_HPP

#include "sfs/disk.hpp"

#include <stdint.h>
#include <vector>

class FileSystem {
  public:
    const static uint32_t MAGIC_NUMBER = 0xf0f03410;
    const static uint32_t INODES_PER_BLOCK = Disk::BLOCK_SIZE / 32; // 128 to 4k block
    const static uint32_t POINTERS_PER_INODE = 5;
    const static uint32_t POINTERS_PER_BLOCK = Disk::BLOCK_SIZE / 4; // 1024; to 4k block
    // extra to dir
    const static uint32_t NAMESIZE = 16;
    const static uint32_t ENTRIES_PER_DIR = 7;
    const static uint32_t DIR_PER_BLOCK = Disk::BLOCK_SIZE / 256; // 16 (**original 8 nao sei o motivo!!)

    FileSystem();
    virtual ~FileSystem();

  private:
    struct SuperBlock {         // Superblock structure
        uint32_t MagicNumber;   // File system magic number
        uint32_t Blocks;        // Number of blocks in file system
        uint32_t InodeBlocks;   // Number of blocks reserved for inodes
        uint32_t Inodes;        // Number of inodes in file system
        uint32_t DirBlocks;     // number of blocks to dir
        uint32_t Protected;     // ??
        char PasswordHash[257]; // root pass
    };                          // Size 281 Bytes

    struct Inode {
        uint32_t Valid;                      // Whether or not inode is valid
        uint32_t Size;                       // Size of file
        uint32_t Direct[POINTERS_PER_INODE]; // Direct pointers
        uint32_t Indirect;                   // Indirect pointer
    };                                       // size 32 Bytes

    struct Dirent {
        uint8_t type;
        uint8_t valid;
        uint32_t inum;
        char Name[NAMESIZE];
    }; // size 22 Bytes

    struct Directory {
        uint16_t Valid;
        uint32_t inum;
        char Name[NAMESIZE];
        Dirent Table[ENTRIES_PER_DIR];
        char reserv[80];
    }; // Size 256 bytes **Size 176 Bytes original

    union Block {
        SuperBlock Super;                      // Superblock
        Inode Inodes[INODES_PER_BLOCK];        // Inode block
        uint32_t Pointers[POINTERS_PER_BLOCK]; // Pointer block
        char Data[Disk::BLOCK_SIZE];           // Data block
        struct Directory Directories[FileSystem::DIR_PER_BLOCK];
    }; // Size 4096

  public:
    void debug(Disk* disk);
    bool format(Disk* disk);

    bool mount(Disk* disk);

    ssize_t create();
    bool remove(size_t inumber);
    ssize_t stat(size_t inumber);

    ssize_t read(size_t inumber, char* data, size_t length, size_t offset);
    ssize_t write(size_t inumber, char* data, size_t length, size_t offset);

    //---
    bool touch(char name[FileSystem::NAMESIZE]);

  private:
    bool load_inode(size_t inumber, Inode* node);

    uint32_t allocate_block();
    bool check_allocation(Inode* node, int read, int orig_offset, uint32_t& blocknum, bool write_indirect, Block indirect);
    void read_buffer(int offset, int* read, int length, char* data, uint32_t blocknum);
    ssize_t write_ret(size_t inumber, Inode* node, int ret);
    void read_helper(uint32_t blocknum, int offset, size_t* length, char** data, char** ptr);

    // //---
    // bool touch(char name[FileSystem::NAMESIZE]);
    FileSystem::Directory add_dir_entry(Directory dir, uint32_t inum, uint32_t type, char name[]);
    void write_dir_back(Directory dir);

    bool mounted;
    Disk* fs_disk;
    SuperBlock MetaData;
    std::vector<bool> free_blocks;

    // quantidade de inodes usados em cada block (cada posicao do array correponde a um bloco de inode)
    std::vector<int> inode_counter;

    Directory curr_dir;
    std::vector<uint32_t> dir_counter;

    unsigned int startBlockData;
    unsigned int startBlockDirectory;
};

#endif

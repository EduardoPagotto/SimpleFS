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
    const static uint32_t NAMESIZE = 28;                         // 16;
    const static uint32_t DIR_PER_BLOCK = Disk::BLOCK_SIZE / 32; // 256; // 16 (**original 8 nao sei o motivo!!)

    FileSystem();
    virtual ~FileSystem();

  private:
    struct SuperBlock {         // Superblock structure
        uint32_t MagicNumber;   // File system magic number
        uint32_t Blocks;        // Number of blocks in file system
        uint32_t InodeBlocks;   // Number of blocks reserved for inodes
        uint32_t Inodes;        // Number of inodes in file system
        uint32_t MapBlocks;     // number of blocks to dir
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
        uint32_t inum;
    }; // size 5 Bytes

    struct Directory {
        uint32_t inum;
        char Name[NAMESIZE];
    }; // 32

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

    /**
     * @brief Escreve nome de arquivo na tabela de diretorio corrente
     *
     * @param name  Nome do arquivo
     * @return true entrada no diretorio escrita com sucesso
     * @return false falha na escrita de diretorio
     */
    bool touch(char name[FileSystem::NAMESIZE]);

  private:
    /**
     * @brief Retorna iNode carregado  usando numero de iNode
     *
     * @param inumber numero do iNode
     * @param node iNode encontrado
     * @return true iNode valido
     * @return false iNode invalido
     */
    bool load_inode(size_t inumber, Inode* node);

    /**
     * @brief Retorna o numero do proximo bloco livre se existir
     *
     * @return uint32_t numero do bloco ou 0 se não existir espaco livre
     */
    uint32_t allocate_block();

    /**
     * @brief Aloca um bloco livre e retorna o mesmo por referencia em blocknum
     *
     * @param node iNode do bloco de dados a ser gravado/Lido (para no erro apenas)
     * @param read total a ser processado (para no erro apenas)
     * @param orig_offset offset a ser processado (para no erro apenas)
     * @param blocknum ponteiro de retorno do bloco livre a ser usado
     * @param write_indirect false para escrita direta (para no erro apenas)
     * @param indirect bloco de indireto (para no erro apenas)
     * @return true Bloco libre encontrado
     * @return false bloco livre não existe (para erro)
     */
    bool check_allocation(Inode* node, int read, int orig_offset, uint32_t& blocknum, bool write_indirect, Block indirect);

    /**
     * @brief Grava buffer de dados no bloco
     *
     * @param offset posicao inicial a ser gravada no bloco
     * @param read ponteiro da posicao de escrita nobloco (retorna o maximo escrito)
     * @param length posicao maxima a ser gravado no bloco
     * @param data buffer de dados a ser gravada
     * @param blocknum numero do bloco a ser gravado
     */
    void read_buffer(int offset, int* read, int length, char* data, uint32_t blocknum);

    /**
     * @brief Escreve o Inode no disco
     *
     * @param inumber numero do inode
     * @param node ponteiro do node com o conteudo do node a ser gravado
     * @param ret tamanho em bytes
     * @return ssize_t valor do "ret"
     */
    ssize_t write_ret(size_t inumber, Inode* node, int ret);

    void read_helper(uint32_t blocknum, int offset, size_t* length, char** data, char** ptr);

    //--- diretorios
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
    unsigned int startBlockMapFree;
};

#endif

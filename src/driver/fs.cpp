// fs.cpp: File System

#include "sfs/fs.hpp"
#include "sfs/sha256.hpp"

#include <algorithm>
#include <assert.h>
#include <cmath>
#include <stdio.h>
#include <string.h>

#define streq(a, b) (strcmp((a), (b)) == 0) // FIXME: solucao idiota

#define startBlockBoot 0 // FIXME: quando usar incrementar todos abaixo
#define startBlockSuper 0
#define startBlockInode 1

FileSystem::FileSystem() : mounted(false), fs_disk(nullptr) {
    startBlockIndirect = -1;
    startBlockData = -1;
    startBlockDirectory = -1;
}

FileSystem::~FileSystem() {}

void FileSystem::debug(Disk* disk) {
    SuperBlock super;

    // Read Superblock
    disk->read(startBlockSuper, (char*)&super);

    printf("SuperBlock:\n");
    printf("    %u blocks\n", super.Blocks);
    printf("    %u inode blocks\n", super.InodeBlocks);
    printf("    %u inodes\n", super.Inodes);

    if (super.MagicNumber != MAGIC_NUMBER)
        return;

    int ii = 0;

    // Read Inode blocks
    Block block;
    for (uint32_t i = startBlockInode; i <= super.InodeBlocks; i++) {
        disk->read(i, block.Data);
        for (uint32_t j = 0; j < INODES_PER_BLOCK; j++) {
            if (block.Inodes[j].Valid) {
                printf("Inode %u:\n", ii);
                printf("    size: %u bytes\n", block.Inodes[j].Size);
                printf("    direct blocks:");

                for (uint32_t k = 0; k < POINTERS_PER_INODE; k++) {
                    if (block.Inodes[j].Direct[k])
                        printf(" %u", block.Inodes[j].Direct[k]);
                }
                printf("\n");

                if (block.Inodes[j].Indirect) {
                    printf("    indirect block: %u\n    indirect data blocks:", block.Inodes[j].Indirect);
                    Block IndirectBlock;
                    disk->read(block.Inodes[j].Indirect, IndirectBlock.Data);
                    for (uint32_t k = 0; k < POINTERS_PER_BLOCK; k++) {
                        if (IndirectBlock.Pointers[k])
                            printf(" %u", IndirectBlock.Pointers[k]);
                    }
                    printf("\n");
                }
            }

            ii++;
        }
    }
}

bool FileSystem::format(Disk* disk) {

    if (disk->mounted())
        return false;

    // Cria SuperBlock
    Block block;
    memset(&block, 0, sizeof(Block));

    block.Super.MagicNumber = FileSystem::MAGIC_NUMBER;
    block.Super.Blocks = disk->size();
    block.Super.InodeBlocks = (uint32_t)std::ceil((int(block.Super.Blocks) * 1.00) / 10);
    block.Super.Inodes = block.Super.InodeBlocks * (FileSystem::INODES_PER_BLOCK);
    block.Super.DirBlocks = (uint32_t)std::ceil((int(block.Super.Blocks) * 1.00) / 100);
    block.Super.Protected = 0;                // Zera campos segurança
    memset(block.Super.PasswordHash, 0, 257); // Zera hash root
    disk->write(startBlockSuper, block.Data);

    // define inicio de cada grupo de blocos
    startBlockIndirect = startBlockInode + block.Super.InodeBlocks;
    startBlockData = startBlockIndirect + 1; // TODO: melhorar
    startBlockDirectory = block.Super.Blocks - block.Super.DirBlocks;

    // Zera Blocos de Inodes a partir do SuperBlock com 1 bloco ou 10% de uso
    for (uint32_t i = startBlockInode; i < startBlockIndirect; i++) {
        Block inodeBlock;
        for (uint32_t j = 0; j < FileSystem::INODES_PER_BLOCK; j++) {
            inodeBlock.Inodes[j].Valid = false;
            inodeBlock.Inodes[j].Size = 0;

            for (uint32_t k = 0; k < FileSystem::POINTERS_PER_INODE; k++)
                inodeBlock.Inodes[j].Direct[k] = 0;

            inodeBlock.Inodes[j].Indirect = 0;
        }

        disk->write(i, inodeBlock.Data);
    }

    // Zera Blocos de indireto
    for (uint32_t i = startBlockIndirect; i < startBlockData; i++) {
        Block DataBlock;
        for (uint32_t j = 0; j < FileSystem::POINTERS_PER_BLOCK; j++)
            DataBlock.Pointers[j] = 0;

        disk->write(i, DataBlock.Data);
    }

    // Zera Bloco de Dados depois dos blocos de inode
    for (uint32_t i = startBlockData; i < startBlockDirectory; i++) {
        Block DataBlock;
        memset(DataBlock.Data, 0, Disk::BLOCK_SIZE);
        disk->write(i, DataBlock.Data);
    }

    // Zera Bloco Diretorio nos ultimos blocos com 1 ou 1% de uso
    for (uint32_t i = startBlockDirectory; i < block.Super.Blocks; i++) {
        Block DataBlock;
        Directory dir;
        dir.inum = -1;
        dir.Valid = 0;
        memset(dir.Table, 0, sizeof(Dirent) * ENTRIES_PER_DIR);
        for (uint32_t j = 0; j < FileSystem::DIR_PER_BLOCK; j++) {
            DataBlock.Directories[j] = dir;
        }
        disk->write(i, DataBlock.Data);
    }

    // excrita do diretorio root
    struct Directory root;
    strcpy(root.Name, "/");
    root.inum = 0;
    root.Valid = 1;

    struct Dirent temp;
    memset(&temp, 0, sizeof(temp));

    temp.inum = 0;
    temp.type = 0;
    temp.valid = 1;
    char tstr1[] = ".";
    char tstr2[] = "..";

    strcpy(temp.Name, tstr1);
    memcpy(&(root.Table[0]), &temp, sizeof(Dirent));

    strcpy(temp.Name, tstr2);
    memcpy(&(root.Table[1]), &temp, sizeof(Dirent));

    Block Dirblock;
    memcpy(&(Dirblock.Directories[0]), &root, sizeof(root));
    disk->write(startBlockDirectory, Dirblock.Data);

    return true;
}

bool FileSystem::mount(Disk* disk) {

    if (disk->mounted())
        return false;

    // Le o Superblock e valida totalizadores
    Block block;
    disk->read(startBlockSuper, block.Data);

    if (block.Super.MagicNumber != MAGIC_NUMBER)
        return false;

    if (block.Super.InodeBlocks != std::ceil((block.Super.Blocks * 1.00) / 10))
        return false;

    if (block.Super.Inodes != (block.Super.InodeBlocks * INODES_PER_BLOCK))
        return false;

    if (block.Super.DirBlocks != (uint32_t)std::ceil((int(block.Super.Blocks) * 1.00) / 100))
        return false;

    // define inicio de cada grupo de blocos
    startBlockIndirect = startBlockInode + block.Super.InodeBlocks;
    startBlockData = startBlockIndirect + 1; // TODO: melhorar
    startBlockDirectory = block.Super.Blocks - block.Super.DirBlocks;

    // se fs estiver protegido
    if (block.Super.Protected) {
        char pass[BUFSIZ], line[BUFSIZ]; // FIXME : merda!!!!
        printf("Enter password: ");
        if (fgets(line, BUFSIZ, stdin) == NULL) {
            return false;
        }
        sscanf(line, "%s", pass);
        if (sha256(std::string(pass)).compare(std::string(block.Super.PasswordHash)) != 0) {
            printf("Password Failed. Exiting...\n");
            return false;
        }

        printf("Disk Unlocked\n");
    }

    disk->mount();
    this->fs_disk = disk;

    MetaData = block.Super;

    // Allocate free block bitmap
    this->free_blocks.resize(MetaData.Blocks, false);
    this->inode_counter.resize(MetaData.InodeBlocks, 0);

    // marca blocos de boot, super, inode, indirect
    for (uint32_t i = startBlockBoot; i < startBlockData; i++)
        free_blocks[i] = true;

    for (uint32_t i = startBlockInode; i < startBlockIndirect; i++) {

        uint32_t indiceBlocoData = i - startBlockInode;

        disk->read(i, block.Data);

        for (uint32_t j = 0; j < INODES_PER_BLOCK; j++) {
            if (block.Inodes[j].Valid) {
                this->inode_counter[indiceBlocoData]++;

                if (block.Inodes[j].Valid)
                    free_blocks[i] = true;

                for (uint32_t k = 0; k < POINTERS_PER_INODE; k++) {
                    if (block.Inodes[j].Direct[k]) {
                        if (block.Inodes[j].Direct[k] < MetaData.Blocks)
                            free_blocks[block.Inodes[j].Direct[k]] = true;
                        else
                            return false;
                    }
                }

                if (block.Inodes[j].Indirect) {
                    if (block.Inodes[j].Indirect < MetaData.Blocks) {
                        free_blocks[block.Inodes[j].Indirect] = true;
                        Block indirect;
                        disk->read(block.Inodes[j].Indirect, indirect.Data);
                        for (uint32_t k = 0; k < POINTERS_PER_BLOCK; k++) {
                            if (indirect.Pointers[k] < MetaData.Blocks) {
                                free_blocks[indirect.Pointers[k]] = true;
                            } else
                                return false;
                        }
                    } else
                        return false;
                }
            }
        }
    }

    // Valida diretorio de arquivos
    dir_counter.resize(MetaData.DirBlocks, 0);
    Block dirblock;
    for (uint32_t dirs = 0; dirs < MetaData.DirBlocks; dirs++) {

        uint32_t dir_block = startBlockDirectory + dirs;
        disk->read(dir_block, dirblock.Data);

        for (uint32_t offset = 0; offset < FileSystem::DIR_PER_BLOCK; offset++) {
            if (dirblock.Directories[offset].Valid == 1) {
                dir_counter[dirs]++;
            }
        }
        if (dirs == 0) {
            curr_dir = dirblock.Directories[0];
        }
    }

    this->mounted = true;
    return true;
}

ssize_t FileSystem::create() {
    if (!mounted)
        return false;

    Block block;
    for (uint32_t indexBlock = startBlockInode; indexBlock < startBlockDirectory; indexBlock++) {

        uint32_t indexBlockInode = startBlockInode - indexBlock;

        if (inode_counter[indexBlockInode] == INODES_PER_BLOCK)
            continue;
        else
            this->fs_disk->read(indexBlock, block.Data);

        for (uint32_t indexINode = 0; indexINode < INODES_PER_BLOCK; indexINode++) {
            if (!block.Inodes[indexINode].Valid) {
                block.Inodes[indexINode].Valid = true;
                block.Inodes[indexINode].Size = 0;
                block.Inodes[indexINode].Indirect = 0;
                for (int indexDirect = 0; indexDirect < 5; indexDirect++) {
                    block.Inodes[indexINode].Direct[indexDirect] = 0;
                }
                free_blocks[indexBlock] = true;
                inode_counter[indexBlockInode]++;

                this->fs_disk->write(indexBlock, block.Data);

                return (((indexBlockInode)*INODES_PER_BLOCK) + indexINode);
            }
        }
    }

    return -1;
}

bool FileSystem::load_inode(size_t inumber, Inode* node) {

    if (!mounted || (inumber > MetaData.Inodes) || (inumber < 0))
        return false;

    Block block;

    int indexBlock = (inumber / INODES_PER_BLOCK) + 1;
    int indexINode = inumber % INODES_PER_BLOCK;

    if (this->inode_counter[indexBlock - 1]) {

        fs_disk->read(indexBlock, block.Data);
        if (block.Inodes[indexINode].Valid) {
            *node = block.Inodes[indexINode];
            return true;
        }
    }

    return false;
}

// Remove inode ----------------------------------------------------------------

bool FileSystem::remove(size_t inumber) {

    if (!mounted)
        return false;

    Inode node;

    if (load_inode(inumber, &node)) {
        node.Valid = false;
        node.Size = 0;

        if (!(--inode_counter[inumber / INODES_PER_BLOCK])) {
            this->free_blocks[inumber / INODES_PER_BLOCK + 1] = false;
        }

        for (uint32_t i = 0; i < POINTERS_PER_INODE; i++) {
            this->free_blocks[node.Direct[i]] = false; // FIXME: tem merda aqui !!!!
            node.Direct[i] = 0;
        }

        if (node.Indirect) {
            Block indirect;
            fs_disk->read(node.Indirect, indirect.Data);
            this->free_blocks[node.Indirect] = false;
            node.Indirect = 0;

            for (uint32_t i = 0; i < POINTERS_PER_BLOCK; i++) {
                if (indirect.Pointers[i])
                    this->free_blocks[indirect.Pointers[i]] = false;
            }
        }

        Block block;
        fs_disk->read(inumber / INODES_PER_BLOCK + 1, block.Data);
        block.Inodes[inumber % INODES_PER_BLOCK] = node;
        fs_disk->write(inumber / INODES_PER_BLOCK + 1, block.Data);

        return true;
    }

    return false;
}

// Inode stat ------------------------------------------------------------------

ssize_t FileSystem::stat(size_t inumber) {
    if (!mounted)
        return -1;

    Inode node;

    if (load_inode(inumber, &node))
        return node.Size;

    return -1;
}

// Read from inode -------------------------------------------------------------

void FileSystem::read_helper(uint32_t blocknum, int offset, size_t* length, char** data, char** ptr) {
    fs_disk->read(blocknum, *ptr);
    *data += offset;
    *ptr += Disk::BLOCK_SIZE;
    *length -= (Disk::BLOCK_SIZE - offset);

    return;
}

ssize_t FileSystem::read(size_t inumber, char* data, size_t length, size_t offset) {
    if (!mounted)
        return -1;

    int size_inode = stat(inumber);

    if ((int)offset >= size_inode)
        return 0;
    else if (length + (int)offset > size_inode)
        length = size_inode - offset;

    Inode node;

    char* ptr = data;
    int to_read = length;

    if (load_inode(inumber, &node)) {
        if (offset < POINTERS_PER_INODE * Disk::BLOCK_SIZE) {
            uint32_t direct_node = offset / Disk::BLOCK_SIZE;
            offset %= Disk::BLOCK_SIZE;

            if (node.Direct[direct_node]) {
                read_helper(node.Direct[direct_node++], offset, &length, &data, &ptr);
                while (length > 0 && direct_node < POINTERS_PER_INODE && node.Direct[direct_node]) {
                    read_helper(node.Direct[direct_node++], 0, &length, &data, &ptr);
                }

                if (length <= 0)
                    return to_read;
                else {
                    if (direct_node == POINTERS_PER_INODE && node.Indirect) {
                        Block indirect;
                        fs_disk->read(node.Indirect, indirect.Data);

                        for (uint32_t i = 0; i < POINTERS_PER_BLOCK; i++) {
                            if (indirect.Pointers[i] && length > 0) {
                                read_helper(indirect.Pointers[i], 0, &length, &data, &ptr);
                            } else
                                break;
                        }

                        if (length <= 0)
                            return to_read;
                        else {
                            return (to_read - length);
                        }
                    } else {
                        return (to_read - length);
                    }
                }
            } else {
                return 0;
            }
        } else {
            if (node.Indirect) {
                offset -= (POINTERS_PER_INODE * Disk::BLOCK_SIZE);
                uint32_t indirect_node = offset / Disk::BLOCK_SIZE;
                offset %= Disk::BLOCK_SIZE;

                Block indirect;
                fs_disk->read(node.Indirect, indirect.Data);

                if (indirect.Pointers[indirect_node] && length > 0) {
                    read_helper(indirect.Pointers[indirect_node++], offset, &length, &data, &ptr);
                }

                for (uint32_t i = indirect_node; i < POINTERS_PER_BLOCK; i++) {
                    if (indirect.Pointers[i] && length > 0) {
                        read_helper(indirect.Pointers[i], 0, &length, &data, &ptr);
                    } else
                        break;
                }

                if (length <= 0)
                    return to_read;
                else {
                    return (to_read - length);
                }
            } else {
                return 0;
            }
        }
    }

    return -1;
}

uint32_t FileSystem::allocate_block() {
    if (!mounted)
        return 0;

    for (uint32_t i = MetaData.InodeBlocks + 1; i < MetaData.Blocks; i++) {
        if (free_blocks[i] == 0) {
            free_blocks[i] = true;
            return i;
        }
    }

    return 0;
}

bool FileSystem::check_allocation(Inode* node, int read, int orig_offset, uint32_t& blocknum, bool write_indirect, Block indirect) {
    if (!mounted)
        return false;

    if (!blocknum) {
        blocknum = allocate_block();
        if (!blocknum) {
            node->Size = read + orig_offset;
            if (write_indirect)
                fs_disk->write(node->Indirect, indirect.Data);
            return false;
        }
    }

    return true;
}

ssize_t FileSystem::write_ret(size_t inumber, Inode* node, int ret) {
    if (!mounted)
        return -1;

    int i = inumber / INODES_PER_BLOCK;
    int j = inumber % INODES_PER_BLOCK;

    Block block;
    fs_disk->read(i + 1, block.Data);
    block.Inodes[j] = *node;
    fs_disk->write(i + 1, block.Data);

    return (ssize_t)ret;
}

void FileSystem::read_buffer(int offset, int* read, int length, char* data, uint32_t blocknum) {
    if (!mounted)
        return;

    char* ptr = (char*)calloc(Disk::BLOCK_SIZE, sizeof(char));

    for (int i = offset; i < (int)Disk::BLOCK_SIZE && *read < length; i++) {
        ptr[i] = data[*read];
        *read = *read + 1;
    }
    fs_disk->write(blocknum, ptr);

    free(ptr);

    return;
}

// Write to inode --------------------------------------------------------------

ssize_t FileSystem::write(size_t inumber, char* data, size_t length, size_t offset) {
    if (!mounted)
        return -1;

    Inode node;
    Block indirect;
    int read = 0;
    int orig_offset = offset;

    // verifica se arquivo é maior do que a capacidade total maxima de armazenamento
    if (length + offset > (POINTERS_PER_BLOCK + POINTERS_PER_INODE) * Disk::BLOCK_SIZE) {
        return -1;
    }

    if (!load_inode(inumber, &node)) {
        node.Valid = true;
        node.Size = length + offset;
        for (uint32_t ii = 0; ii < POINTERS_PER_INODE; ii++) {
            node.Direct[ii] = 0;
        }
        node.Indirect = 0;
        inode_counter[inumber / INODES_PER_BLOCK]++;
        free_blocks[inumber / INODES_PER_BLOCK + 1] = true;
    } else {
        node.Size = std::max((size_t)node.Size, length + offset);
    }

    if (offset < POINTERS_PER_INODE * Disk::BLOCK_SIZE) {
        int direct_node = offset / Disk::BLOCK_SIZE;
        offset %= Disk::BLOCK_SIZE;

        if (!check_allocation(&node, read, orig_offset, node.Direct[direct_node], false, indirect)) {
            return write_ret(inumber, &node, read);
        }
        read_buffer(offset, &read, length, data, node.Direct[direct_node++]);

        if (read == length)
            return write_ret(inumber, &node, length);
        else {
            for (int i = direct_node; i < (int)POINTERS_PER_INODE; i++) {
                if (!check_allocation(&node, read, orig_offset, node.Direct[direct_node], false, indirect)) {
                    return write_ret(inumber, &node, read);
                }
                read_buffer(0, &read, length, data, node.Direct[direct_node++]);

                if (read == length)
                    return write_ret(inumber, &node, length);
            }

            if (node.Indirect)
                fs_disk->read(node.Indirect, indirect.Data);
            else {
                if (!check_allocation(&node, read, orig_offset, node.Indirect, false, indirect)) {
                    return write_ret(inumber, &node, read);
                }
                fs_disk->read(node.Indirect, indirect.Data);

                for (int i = 0; i < (int)POINTERS_PER_BLOCK; i++) {
                    indirect.Pointers[i] = 0;
                }
            }

            for (int j = 0; j < (int)POINTERS_PER_BLOCK; j++) {
                if (!check_allocation(&node, read, orig_offset, indirect.Pointers[j], true, indirect)) {
                    return write_ret(inumber, &node, read);
                }
                read_buffer(0, &read, length, data, indirect.Pointers[j]);

                if (read == length) {
                    fs_disk->write(node.Indirect, indirect.Data);
                    return write_ret(inumber, &node, length);
                }
            }

            fs_disk->write(node.Indirect, indirect.Data);
            return write_ret(inumber, &node, read);
        }
    } else {
        offset -= (Disk::BLOCK_SIZE * POINTERS_PER_INODE);
        int indirect_node = offset / Disk::BLOCK_SIZE;
        offset %= Disk::BLOCK_SIZE;

        if (node.Indirect)
            fs_disk->read(node.Indirect, indirect.Data);
        else {
            if (!check_allocation(&node, read, orig_offset, node.Indirect, false, indirect)) {
                return write_ret(inumber, &node, read);
            }
            fs_disk->read(node.Indirect, indirect.Data);

            for (int i = 0; i < (int)POINTERS_PER_BLOCK; i++) {
                indirect.Pointers[i] = 0;
            }
        }

        if (!check_allocation(&node, read, orig_offset, indirect.Pointers[indirect_node], true, indirect)) {
            return write_ret(inumber, &node, read);
        }
        read_buffer(offset, &read, length, data, indirect.Pointers[indirect_node++]);

        if (read == length) {
            fs_disk->write(node.Indirect, indirect.Data);
            return write_ret(inumber, &node, length);
        } else {
            for (int j = indirect_node; j < (int)POINTERS_PER_BLOCK; j++) {
                if (!check_allocation(&node, read, orig_offset, indirect.Pointers[j], true, indirect)) {
                    return write_ret(inumber, &node, read);
                }
                read_buffer(0, &read, length, data, indirect.Pointers[j]);

                if (read == length) {
                    fs_disk->write(node.Indirect, indirect.Data);
                    return write_ret(inumber, &node, length);
                }
            }

            fs_disk->write(node.Indirect, indirect.Data);
            return write_ret(inumber, &node, read);
        }
    }

    return -1;
}

// FIXME: abaixo sera em outra classe

// Necessario para criar arquivo

FileSystem::Directory FileSystem::add_dir_entry(Directory dir, uint32_t inum, uint32_t type, char name[]) {
    Directory tempdir = dir;

    uint32_t idx = 0;
    for (; idx < FileSystem::ENTRIES_PER_DIR; idx++) {
        if (tempdir.Table[idx].valid == 0) {
            break;
        }
    }

    if (idx == FileSystem::ENTRIES_PER_DIR) {
        printf("Directory entry limit reached..exiting\n");
        tempdir.Valid = 0;
        return tempdir;
    }

    tempdir.Table[idx].inum = inum;
    tempdir.Table[idx].type = type;
    tempdir.Table[idx].valid = 1;
    strcpy(tempdir.Table[idx].Name, name);

    return tempdir;
}

void FileSystem::write_dir_back(Directory dir) {
    uint32_t block_idx = (dir.inum / FileSystem::DIR_PER_BLOCK);
    uint32_t block_offset = (dir.inum % FileSystem::DIR_PER_BLOCK);

    Block block;
    fs_disk->read(MetaData.Blocks - 1 - block_idx, block.Data);
    block.Directories[block_offset] = dir;

    fs_disk->write(MetaData.Blocks - 1 - block_idx, block.Data);
}

bool FileSystem::touch(char name[FileSystem::NAMESIZE]) {
    if (!mounted) {
        return false;
    }

    for (uint32_t offset = 0; offset < FileSystem::ENTRIES_PER_DIR; offset++) {
        if (curr_dir.Table[offset].valid) {
            if (streq(curr_dir.Table[offset].Name, name)) {
                printf("File already exists\n");
                return false;
            }
        }
    }
    ssize_t new_node_idx = FileSystem::create();
    if (new_node_idx == -1) {
        printf("Error creating new inode\n");
        return false;
    }

    Directory temp = add_dir_entry(curr_dir, new_node_idx, 1, name);
    if (temp.Valid == 0) {
        printf("Error adding new file\n");
        return false;
    }
    curr_dir = temp;

    write_dir_back(curr_dir);

    return true;
}
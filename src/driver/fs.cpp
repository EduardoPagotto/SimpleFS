// fs.cpp: File System

#include "sfs/fs.hpp"
#include "sfs/sha256.hpp"

#include <algorithm>
#include <assert.h>
#include <cmath>
#include <stdio.h>
#include <string.h>

void FileSystem::debug(Disk* disk) {
    Block block;

    // Read Superblock
    disk->read(0, block.Data);

    printf("SuperBlock:\n");
    printf("    %u blocks\n", block.Super.Blocks);
    printf("    %u inode blocks\n", block.Super.InodeBlocks);
    printf("    %u inodes\n", block.Super.Inodes);

    if (block.Super.MagicNumber != MAGIC_NUMBER)
        return;

    int ii = 0;

    // Read Inode blocks
    for (uint32_t i = 1; i <= block.Super.InodeBlocks; i++) {
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

    // Cria SuperBlock no bloco 0
    Block block;
    memset(&block, 0, sizeof(Block));

    block.Super.MagicNumber = FileSystem::MAGIC_NUMBER;
    block.Super.Blocks = disk->size();
    block.Super.InodeBlocks = (uint32_t)std::ceil((int(block.Super.Blocks) * 1.00) / 10);
    block.Super.Inodes = block.Super.InodeBlocks * (FileSystem::INODES_PER_BLOCK);
    block.Super.DirBlocks = (uint32_t)std::ceil((int(block.Super.Blocks) * 1.00) / 100);
    block.Super.Protected = 0;                // Zera campos segurança
    memset(block.Super.PasswordHash, 0, 257); // Zera hash root
    disk->write(0, block.Data);

    // Zera Blocos de Inodes a partir do SuperBlock com 1 bloco ou 10% de uso
    uint32_t begin = 1;
    uint32_t end = block.Super.InodeBlocks + 1;
    for (uint32_t i = begin; i < end; i++) {
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

    // Zera Bloco de Dados depois dos blocos de inode
    begin = end;
    end = block.Super.Blocks - block.Super.DirBlocks;
    for (uint32_t i = begin; i < end; i++) {
        Block DataBlock;
        memset(DataBlock.Data, 0, Disk::BLOCK_SIZE);
        disk->write(i, DataBlock.Data);
    }

    // Zera Bloco Diretorio nos ultimos blocos com 1 ou 1% de uso
    begin = end;
    end = block.Super.Blocks;
    for (uint32_t i = begin; i < end; i++) {
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
    disk->write(block.Super.Blocks - 1, Dirblock.Data);

    return true;
}

bool FileSystem::mount(Disk* disk) {

    if (disk->mounted())
        return false;

    // Le o Superblock e valida totalizadores
    Block block;
    disk->read(0, block.Data);

    if (block.Super.MagicNumber != MAGIC_NUMBER)
        return false;
    if (block.Super.InodeBlocks != std::ceil((block.Super.Blocks * 1.00) / 10))
        return false;
    if (block.Super.Inodes != (block.Super.InodeBlocks * INODES_PER_BLOCK))
        return false;
    if (block.Super.DirBlocks != (uint32_t)std::ceil((int(block.Super.Blocks) * 1.00) / 100))
        return false;

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

    free_blocks[0] = true;

    for (uint32_t i = 1; i <= MetaData.InodeBlocks; i++) {
        disk->read(i, block.Data);

        for (uint32_t j = 0; j < INODES_PER_BLOCK; j++) {
            if (block.Inodes[j].Valid) {
                this->inode_counter[i - 1]++;

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
        disk->read(MetaData.Blocks - 1 - dirs, dirblock.Data);
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
    this->fs_disk->read(0, block.Data);

    for (uint32_t indexBlock = 1; indexBlock <= MetaData.InodeBlocks; indexBlock++) {
        if (inode_counter[indexBlock - 1] == INODES_PER_BLOCK)
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
                inode_counter[indexBlock - 1]++;

                this->fs_disk->write(indexBlock, block.Data);

                return (((indexBlock - 1) * INODES_PER_BLOCK) + indexINode);
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

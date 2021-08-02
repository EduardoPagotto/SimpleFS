// fs.cpp: File System

#include "sfs/fs.h"

#include <algorithm>
#include <assert.h>
#include <cmath>
#include <stdio.h>
#include <string.h>

// Debug file system -----------------------------------------------------------

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

// Format file system ----------------------------------------------------------

bool FileSystem::format(Disk* disk) {

    if (disk->mounted())
        return false;

    // Write superblock
    Block block;
    memset(&block, 0, sizeof(Block));

    block.Super.MagicNumber = FileSystem::MAGIC_NUMBER;
    block.Super.Blocks = disk->size();
    block.Super.InodeBlocks = (uint32_t)std::ceil((int(block.Super.Blocks) * 1.00) / 10);
    block.Super.Inodes = block.Super.InodeBlocks * (FileSystem::INODES_PER_BLOCK);

    disk->write(0, block.Data);

    for (uint32_t i = 1; i <= block.Super.InodeBlocks; i++) {
        Block inodeblock;
        for (uint32_t j = 0; j < FileSystem::INODES_PER_BLOCK; j++) {
            inodeblock.Inodes[j].Valid = false;
            inodeblock.Inodes[j].Size = 0;

            for (uint32_t k = 0; k < FileSystem::POINTERS_PER_INODE; k++)
                inodeblock.Inodes[j].Direct[k] = 0;

            inodeblock.Inodes[j].Indirect = 0;
        }

        disk->write(i, inodeblock.Data);
    }

    // Clear all other blocks
    return true;
}

// Mount file system -----------------------------------------------------------

bool FileSystem::mount(Disk* disk) {

    if (disk->mounted())
        return false;

    // Read superblock
    Block block;
    disk->read(0, block.Data);

    if (block.Super.MagicNumber != FileSystem::MAGIC_NUMBER)
        return false;

    if (block.Super.InodeBlocks != (block.Super.Blocks > 10 ? block.Super.Blocks / 10 : 1))
        return false;

    disk->mount();
    // Copy metadata
    MetaData = block.Super;

    // Allocate free block bitmap
    free_blocks.resize(MetaData.Blocks, false);
    inode_counter.resize(MetaData.InodeBlocks, 0);

    free_blocks[0] = true;

    for (uint32_t i = 1; i <= MetaData.InodeBlocks; i++) {
        disk->read(i, block.Data);

        for (uint32_t j = 0; j < INODES_PER_BLOCK; j++) {
            if (block.Inodes[j].Valid) {
                inode_counter[i - 1]++;

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

    this->fs_disk = disk;
    this->mounted = true;
    return true;
}

// Create inode ----------------------------------------------------------------

ssize_t FileSystem::create() {
    if (!mounted)
        return false;

    Block block;
    this->fs_disk->read(0, block.Data);

    for (uint32_t i = 1; i <= MetaData.InodeBlocks; i++) {
        if (inode_counter[i - 1] == INODES_PER_BLOCK)
            continue;
        else
            this->fs_disk->read(i, block.Data);

        for (uint32_t j = 0; j < INODES_PER_BLOCK; j++) {
            if (!block.Inodes[j].Valid) {
                block.Inodes[j].Valid = true;
                block.Inodes[j].Size = 0;
                block.Inodes[j].Indirect = 0;
                for (int ii = 0; ii < 5; ii++) {
                    block.Inodes[j].Direct[ii] = 0;
                }
                free_blocks[i] = true;
                inode_counter[i - 1]++;

                this->fs_disk->write(i, block.Data);

                return (((i - 1) * INODES_PER_BLOCK) + j);
            }
        }
    }

    return -1;
}

bool FileSystem::load_inode(size_t inumber, Inode* node) {

    if (!mounted)
        return false;

    if ((inumber > MetaData.Inodes) || (inumber < 1)) {
        return false;
    }

    Block block;

    int i = inumber / INODES_PER_BLOCK;
    int j = inumber % INODES_PER_BLOCK;

    if (this->inode_counter[i]) {
        fs_disk->read(i + 1, block.Data);
        if (block.Inodes[j].Valid) {
            *node = block.Inodes[j];
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
            this->free_blocks[node.Direct[i]] = false;
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

ssize_t FileSystem::read(size_t inumber, char* data, size_t length, size_t offset) {
    // Load inode information

    // Adjust length

    // Read block and copy to data
    return 0;
}

// Write to inode --------------------------------------------------------------

ssize_t FileSystem::write(size_t inumber, char* data, size_t length, size_t offset) {
    // Load inode

    // Write block and copy to data
    return 0;
}

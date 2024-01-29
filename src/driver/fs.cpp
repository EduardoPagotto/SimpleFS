#include "sfs/fs.hpp"
#include "sfs/sha256.hpp"
#include <algorithm>
#include <assert.h>
#include <cmath>
#include <stdio.h>
#include <string.h>

#define streq(a, b) (strcmp((a), (b)) == 0) // TODO: solucao idiota

#define startBlockBoot 0 // TODO: quando usar incrementar todos abaixo
#define startBlockSuper 0
#define startBlockInode 1

FileSystem::FileSystem() : mounted(false), fs_disk(nullptr) {
    startBlockData = -1;
    startBlockMapFree = -1;
}

FileSystem::~FileSystem() {}

void FileSystem::debug(Disk& disk) {
    SuperBlock super;

    // Read Superblock
    disk.read(startBlockSuper, (char*)&super);

    printf("SuperBlock:\n");
    printf("    %u blocks\n", super.nBlocks);
    printf("    %u inode blocks\n", super.nInodeBlocks);
    printf("    %u inodes\n", super.nInodes);

    if (super.nMagic != FS_MAGIC_NUMBER)
        return;

    int ii = 0;

    // Read Inode blocks
    Block block;
    for (uint32_t i = startBlockInode; i <= super.nInodeBlocks; i++) {
        disk.read(i, block.data);
        for (uint32_t j = 0; j < FS_INODES_PER_BLOCK; j++) {
            if (block.inodes[j].bonds > 0) {
                printf("Inode %u:\n", ii);
                printf("    size: %u bytes\n", block.inodes[j].size);
                printf("    direct blocks:");

                for (uint32_t k = 0; k < FS_POINTERS_PER_INODE; k++) {
                    if (block.inodes[j].direct[k])
                        printf(" %u", block.inodes[j].direct[k]);
                }
                printf("\n");

                if (block.inodes[j].indirect) {
                    printf("    indirect block: %u\n    indirect data blocks:", block.inodes[j].indirect);
                    Block IndirectBlock;
                    disk.read(block.inodes[j].indirect, IndirectBlock.data);
                    for (uint32_t k = 0; k < FS_POINTERS_PER_BLOCK; k++) {
                        if (IndirectBlock.pointers[k])
                            printf(" %u", IndirectBlock.pointers[k]);
                    }
                    printf("\n");
                }
            }

            ii++;
        }
    }
}

bool FileSystem::format(Disk& disk) {

    if (disk.mounted())
        return false;

    // Cria SuperBlock
    Block block;
    memset(&block, 0, sizeof(Block));

    // Define totalizadores dos grupos de blocos (inode, data, directory)
    block.super.nMagic = FS_MAGIC_NUMBER;
    block.super.nBlocks = disk.tot_blocks();
    block.super.nInodeBlocks = (uint32_t)std::ceil((block.super.nBlocks * 1.00) / 10);
    block.super.nInodes = block.super.nInodeBlocks * (FS_INODES_PER_BLOCK);
    block.super.nMapBlocks = (uint32_t)std::ceil((int(block.super.nBlocks) * 1.00) / 100);

    // Define parametros de segurança
    block.super.Protected = 0;              // Zera campos segurança
    memset(block.super.szPassHash, 0, 257); // Zera hash root
    disk.write(startBlockSuper, block.data);

    // Define inicio de blocos de dados e diretorio
    startBlockData = startBlockInode + block.super.nInodeBlocks;
    startBlockMapFree = block.super.nBlocks - block.super.nMapBlocks;

    // Zera Blocos de Inode
    for (uint32_t i = startBlockInode; i < startBlockData; i++) {
        Block inodeBlock;
        for (uint32_t j = 0; j < FS_INODES_PER_BLOCK; j++) {
            inodeBlock.inodes[j].bonds = 0;
            inodeBlock.inodes[j].mode = 0; // 0x01ff; tttt 000r wxrw xrwx
            inodeBlock.inodes[j].size = 0;

            for (uint32_t k = 0; k < FS_POINTERS_PER_INODE; k++)
                inodeBlock.inodes[j].direct[k] = 0;

            inodeBlock.inodes[j].indirect = 0;
        }

        disk.write(i, inodeBlock.data);
    }

    // Zera Bloco de Dados depois dos blocos de inode
    for (uint32_t i = startBlockData; i < startBlockMapFree; i++) {
        Block DataBlock;
        memset(DataBlock.data, 0, DISK_BLOCK_SIZE);
        disk.write(i, DataBlock.data);
    }

    // Zera Bloco Mapa Free
    for (uint32_t i = startBlockMapFree; i < block.super.nBlocks; i++) {
        Block FreeBlock;
        memset(FreeBlock.data, 0, DISK_BLOCK_SIZE);
        disk.write(i, FreeBlock.data);
    }

    // Cria entrada diretorio root
    Block blockINode;
    disk.read(startBlockInode, blockINode.data); // Le bloco 0 de iNode
    Inode* node = &blockINode.inodes[0];         // pega Inode
    (node->bonds)++;                             // Marca coo valido
    node->mode = 0b0000000100100100;             // 0000 000r--r--r-- Diretorio
    node->direct[0] = startBlockData;            // Aloca primeiro inode data valido

    // inicializa Diretorio root
    Block Dirblock;
    memset(&Dirblock, 0, sizeof(Block));

    if (add_dir_entry(0, (char*)".", Dirblock) == true) {
        if (add_dir_entry(0, (char*)"..", Dirblock) == true) {
            disk.write(node->direct[0], Dirblock.data);
            disk.write(startBlockInode, blockINode.data);
            return true;
        }
    }

    return false;
}

bool FileSystem::mount(Disk& disk) {

    if (disk.mounted())
        return false;

    // Le o Superblock e valida totalizadores
    Block block;
    disk.read(startBlockSuper, block.data);

    if (block.super.nMagic != FS_MAGIC_NUMBER)
        return false;

    if (block.super.nInodeBlocks != std::ceil((block.super.nBlocks * 1.00) / 10))
        return false;

    if (block.super.nInodes != (block.super.nInodeBlocks * FS_INODES_PER_BLOCK))
        return false;

    if (block.super.nMapBlocks != (uint32_t)std::ceil((int(block.super.nBlocks) * 1.00) / 100))
        return false;

    // define inicio de cada grupo de blocos
    startBlockData = startBlockInode + block.super.nInodeBlocks;
    startBlockMapFree = block.super.nBlocks - block.super.nMapBlocks;

    // se fs estiver protegido
    if (block.super.Protected) {
        char pass[BUFSIZ], line[BUFSIZ]; // FIXME : merda!!!!
        printf("Enter password: ");
        if (fgets(line, BUFSIZ, stdin) == nullptr) {
            return false;
        }
        sscanf(line, "%s", pass);
        if (sha256(std::string(pass)).compare(std::string(block.super.szPassHash)) != 0) {
            printf("Password Failed. Exiting...\n");
            return false;
        }

        printf("Disk Unlocked\n");
    }

    disk.mount();
    this->fs_disk = &disk;

    MetaData = block.super;

    // Allocate free block bitmap
    this->free_blocks.resize(MetaData.nBlocks, false);
    this->inode_counter.resize(MetaData.nInodeBlocks, 0);

    // Marca blocos de boot, super e inode como ocupados
    for (uint32_t i = startBlockBoot; i < startBlockInode; i++)
        free_blocks[i] = true;

    // Percorre Blocos de inodes para marcar blocos de inode e dados em uso
    for (uint32_t i = startBlockInode; i < startBlockData; i++) {

        // Indice do numero de Inode
        uint32_t indiceBlocoInode = i - startBlockInode;

        // Le bloco inteiro de Inode
        disk.read(i, block.data);

        for (uint32_t j = 0; j < FS_INODES_PER_BLOCK; j++) {
            if (block.inodes[j].bonds > 0) {
                this->inode_counter[indiceBlocoInode]++;

                free_blocks[i] = true;

                for (uint32_t k = 0; k < FS_POINTERS_PER_INODE; k++) {
                    if (block.inodes[j].direct[k]) {
                        if (block.inodes[j].direct[k] < MetaData.nBlocks)
                            free_blocks[block.inodes[j].direct[k]] = true;
                        else
                            return false;
                    }
                }

                if (block.inodes[j].indirect) {
                    if (block.inodes[j].indirect < MetaData.nBlocks) {
                        free_blocks[block.inodes[j].indirect] = true;
                        Block indirect;
                        disk.read(block.inodes[j].indirect, indirect.data);
                        for (uint32_t k = 0; k < FS_POINTERS_PER_BLOCK; k++) {
                            if (indirect.pointers[k] < MetaData.nBlocks) {
                                if (indirect.pointers[k] != 0)
                                    free_blocks[indirect.pointers[k]] = true;
                            } else
                                return false;
                        }
                    } else
                        return false;
                }
            }
        }
    }

    // Carrega Diretorio Root
    Block blockINode;
    disk.read(startBlockInode, blockINode.data); // Le bloco 0 de iNode
    Inode* node = &blockINode.inodes[0];         // pega Inode
    uint8_t tipo = node->mode >> 12;
    if ((node->bonds > 0) && (tipo == 0)) {

        curr_dir = node->direct[0];
        this->mounted = true;
        return true;
    }

    return false;
}

ssize_t FileSystem::create() {
    if (!mounted)
        return false;

    Block block;
    for (uint32_t i = startBlockInode; i < startBlockData; i++) {

        uint32_t indexBlockInode = i - startBlockInode;

        if (inode_counter[indexBlockInode] == FS_INODES_PER_BLOCK)
            continue;
        else
            this->fs_disk->read(i, block.data);

        for (uint32_t indexINode = 0; indexINode < FS_INODES_PER_BLOCK; indexINode++) {
            if (block.inodes[indexINode].bonds == 0) {
                block.inodes[indexINode].bonds++;
                block.inodes[indexINode].mode = 0b0001000110110110;
                block.inodes[indexINode].size = 0;
                block.inodes[indexINode].indirect = 0;
                for (int indexDirect = 0; indexDirect < 5; indexDirect++) {
                    block.inodes[indexINode].direct[indexDirect] = 0;
                }
                free_blocks[i] = true;
                inode_counter[indexBlockInode]++;

                this->fs_disk->write(i, block.data);

                return (((indexBlockInode)*FS_INODES_PER_BLOCK) + indexINode);
            }
        }
    }

    return -1;
}

bool FileSystem::load_inode(size_t inumber, Inode* node) {

    // valida range
    if (!mounted || (inumber > MetaData.nInodes) || (inumber < 0))
        return false;

    // encontra o indice do iNode no vetor de inodes
    uint32_t indiceInodeLocal = inumber / FS_INODES_PER_BLOCK;

    // Valida se bloco de iNode nao esta vazio no indice de Inodes
    if (this->inode_counter[indiceInodeLocal]) {

        Block block;

        // Encontra o Bloco de iNode Correto
        uint32_t iBlock = indiceInodeLocal + startBlockInode;

        // Encontra o indice de Inode dentro do Bloco encontrado
        int indexINode = inumber % FS_INODES_PER_BLOCK;

        // Le o bloco de iNode Inteiro
        fs_disk->read(iBlock, block.data);

        // Se iNode estiver valido para uso carregar na variavel de retorno por ref
        if (block.inodes[indexINode].bonds > 0) {
            *node = block.inodes[indexINode];
            return true;
        }
    }

    // iNode não é Valido
    return false;
}

// Remove inode ----------------------------------------------------------------

bool FileSystem::remove(size_t inumber) {

    if (!mounted)
        return false;

    Inode node;

    if (load_inode(inumber, &node)) {
        node.bonds--;
        node.size = 0;

        uint32_t indiceInodeLocal = inumber / FS_INODES_PER_BLOCK;
        uint32_t iBlock = indiceInodeLocal + startBlockInode;

        if (!(--inode_counter[indiceInodeLocal])) {
            this->free_blocks[iBlock] = false;
        }

        for (uint32_t i = 0; i < FS_POINTERS_PER_INODE; i++) {
            this->free_blocks[node.direct[i]] = false;
            node.direct[i] = 0;
        }

        if (node.indirect) {
            Block indirect;
            fs_disk->read(node.indirect, indirect.data);
            this->free_blocks[node.indirect] = false;
            node.indirect = 0;

            for (uint32_t i = 0; i < FS_POINTERS_PER_BLOCK; i++) {
                if (indirect.pointers[i])
                    this->free_blocks[indirect.pointers[i]] = false;
            }
        }

        Block block;
        fs_disk->read(iBlock, block.data);
        block.inodes[inumber % FS_INODES_PER_BLOCK] = node;
        fs_disk->write(iBlock, block.data);

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
        return node.size;

    return -1;
}

// Read from inode -------------------------------------------------------------

void FileSystem::read_helper(uint32_t blocknum, int offset, size_t* length, char** data, char** ptr) {
    fs_disk->read(blocknum, *ptr);
    *data += offset;
    *ptr += DISK_BLOCK_SIZE;
    *length -= (DISK_BLOCK_SIZE - offset);

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
        if (offset < FS_POINTERS_PER_INODE * DISK_BLOCK_SIZE) {
            uint32_t direct_node = offset / DISK_BLOCK_SIZE;
            offset %= DISK_BLOCK_SIZE;

            if (node.direct[direct_node]) {
                read_helper(node.direct[direct_node++], offset, &length, &data, &ptr);
                while (length > 0 && direct_node < FS_POINTERS_PER_INODE && node.direct[direct_node]) {
                    read_helper(node.direct[direct_node++], 0, &length, &data, &ptr);
                }

                if (length <= 0)
                    return to_read;
                else {
                    if (direct_node == FS_POINTERS_PER_INODE && node.indirect) {
                        Block indirect;
                        fs_disk->read(node.indirect, indirect.data);

                        for (uint32_t i = 0; i < FS_POINTERS_PER_BLOCK; i++) {
                            if (indirect.pointers[i] && length > 0) {
                                read_helper(indirect.pointers[i], 0, &length, &data, &ptr);
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
            if (node.indirect) {
                offset -= (FS_POINTERS_PER_INODE * DISK_BLOCK_SIZE);
                uint32_t indirect_node = offset / DISK_BLOCK_SIZE;
                offset %= DISK_BLOCK_SIZE;

                Block indirect;
                fs_disk->read(node.indirect, indirect.data);

                if (indirect.pointers[indirect_node] && length > 0) {
                    read_helper(indirect.pointers[indirect_node++], offset, &length, &data, &ptr);
                }

                for (uint32_t i = indirect_node; i < FS_POINTERS_PER_BLOCK; i++) {
                    if (indirect.pointers[i] && length > 0) {
                        read_helper(indirect.pointers[i], 0, &length, &data, &ptr);
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

    // Procura bloco livre
    for (uint32_t i = startBlockData; i < startBlockMapFree; i++) {
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
            node->size = read + orig_offset;
            if (write_indirect)
                fs_disk->write(node->indirect, indirect.data);
            return false;
        }
    }

    return true;
}

ssize_t FileSystem::write_ret(size_t inumber, Inode* node, int ret) {
    if (!mounted)
        return -1;

    // encocntra bloco de posicao do inode correspondente
    int i = (inumber / FS_INODES_PER_BLOCK) + startBlockInode;
    int j = inumber % FS_INODES_PER_BLOCK;

    // Le o bloco inteiro e grava os novos dados do inode em sua posicao
    Block block;
    fs_disk->read(i, block.data);
    block.inodes[j] = *node;
    fs_disk->write(i, block.data);

    // TODO: melhorar
    return (ssize_t)ret;
}

void FileSystem::read_buffer(int offset, int* read, int length, char* data, uint32_t blocknum) {
    if (!mounted)
        return;

    char* ptr = (char*)calloc(DISK_BLOCK_SIZE, sizeof(char));

    for (int i = offset; i < (int)DISK_BLOCK_SIZE && *read < length; i++) {
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
    if (length + offset > (FS_POINTERS_PER_BLOCK + FS_POINTERS_PER_INODE) * DISK_BLOCK_SIZE) {
        return -1;
    }

    if (!load_inode(inumber, &node)) {
        // entradas consecutivas com offset != 0 inode sera atualizado
        node.bonds++;
        node.size = length + offset;
        for (uint32_t ii = 0; ii < FS_POINTERS_PER_INODE; ii++) {
            node.direct[ii] = 0;
        }
        node.indirect = 0;
        inode_counter[inumber / FS_INODES_PER_BLOCK]++;
        free_blocks[inumber / FS_INODES_PER_BLOCK + 1] = true;
    } else {
        // primeira entrada com offset 0, inode sera preenchido pela primeira vez
        node.size = std::max((size_t)node.size, length + offset);
    }

    if (offset < FS_POINTERS_PER_INODE * DISK_BLOCK_SIZE) {
        // arquivo cabe em 1 a 5 blocos (sem indireção)
        int direct_node = offset / DISK_BLOCK_SIZE;
        offset %= DISK_BLOCK_SIZE;

        if (!check_allocation(&node, read, orig_offset, node.direct[direct_node], false, indirect)) {
            // falhou em alocar todo o espaço grava apenas o que conseguiu
            return write_ret(inumber, &node, read);
        }
        // copia dados do buffer de entrada para o bloco no fs (direct_node)
        read_buffer(offset, &read, length, data, node.direct[direct_node++]);

        if (read == length)
            // atualiza inode com dados gravados e sai da funcao
            return write_ret(inumber, &node, length);
        else {

            // continua a escrever nos blocos diretos
            for (int i = direct_node; i < FS_POINTERS_PER_INODE; i++) {
                if (!check_allocation(&node, read, orig_offset, node.direct[direct_node], false, indirect)) {
                    return write_ret(inumber, &node, read);
                }
                // copia dados do buffer de entrada para o bloco no fs (direct_node)
                read_buffer(0, &read, length, data, node.direct[direct_node++]);

                // dados copiados em todas as passadas, atualiza o inode
                if (read == length)
                    return write_ret(inumber, &node, length);
            }

            // se indirect ja foi instanciado
            if (node.indirect)
                fs_disk->read(node.indirect, indirect.data);
            else {

                // Aloca e formata bloco de indirecao
                if (!check_allocation(&node, read, orig_offset, node.indirect, false, indirect)) {
                    return write_ret(inumber, &node, read);
                }
                fs_disk->read(node.indirect, indirect.data);

                for (int i = 0; i < FS_POINTERS_PER_BLOCK; i++) {
                    indirect.pointers[i] = 0;
                }
            }

            for (int j = 0; j < FS_POINTERS_PER_BLOCK; j++) {
                if (!check_allocation(&node, read, orig_offset, indirect.pointers[j], true, indirect)) {
                    return write_ret(inumber, &node, read);
                }
                read_buffer(0, &read, length, data, indirect.pointers[j]);

                if (read == length) {
                    fs_disk->write(node.indirect, indirect.data);
                    return write_ret(inumber, &node, length);
                }
            }

            fs_disk->write(node.indirect, indirect.data);
            return write_ret(inumber, &node, read);
        }
    } else {
        offset -= (DISK_BLOCK_SIZE * FS_POINTERS_PER_INODE);
        int indirect_node = offset / DISK_BLOCK_SIZE;
        offset %= DISK_BLOCK_SIZE;

        // Se Node indirect ja esta instanciado ler bloco de dados do mesmo
        if (node.indirect)
            fs_disk->read(node.indirect, indirect.data);
        else {
            // primeira entrado do node indirect alocar bloco para indirect
            if (!check_allocation(&node, read, orig_offset, node.indirect, false, indirect)) {
                return write_ret(inumber, &node, read);
            }

            // le bloco com dados do indirect
            fs_disk->read(node.indirect, indirect.data);

            // Limpa ponteiros dentro de indirect
            for (int i = 0; i < FS_POINTERS_PER_BLOCK; i++) {
                indirect.pointers[i] = 0;
            }
        }

        // Aloca novo bloco para dados e coloca indice do iNode no vetor de indirect
        if (!check_allocation(&node, read, orig_offset, indirect.pointers[indirect_node], true, indirect)) {
            return write_ret(inumber, &node, read);
        }

        // escreve dados no bloco
        read_buffer(offset, &read, length, data, indirect.pointers[indirect_node++]);

        if (read == length) {
            // se dados cabem escreve bloco do indirect e atualiza iNode do arquivo
            fs_disk->write(node.indirect, indirect.data);
            return write_ret(inumber, &node, length);
        } else {
            for (int j = indirect_node; j < FS_POINTERS_PER_BLOCK; j++) {
                // Aloca novo bloco
                if (!check_allocation(&node, read, orig_offset, indirect.pointers[j], true, indirect)) {
                    return write_ret(inumber, &node, read);
                }
                // escreve dados no bloco
                read_buffer(0, &read, length, data, indirect.pointers[j]);

                if (read == length) {
                    fs_disk->write(node.indirect, indirect.data);
                    return write_ret(inumber, &node, length);
                }
            }

            fs_disk->write(node.indirect, indirect.data);
            return write_ret(inumber, &node, read);
        }
    }

    return -1;
}

bool FileSystem::touch(char name[FS_NAMESIZE]) {
    if (!mounted) {
        return false;
    }

    Block dirBlock;
    fs_disk->read(curr_dir, dirBlock.data);

    // Aloca um inode para os dados do arquivo
    ssize_t new_node_idx = this->create();
    if (new_node_idx == -1) {
        printf("Error creating new Dir inode\n");
        return false;
    }

    if (add_dir_entry(new_node_idx, name, dirBlock) == false) {
        remove(new_node_idx);
        return false;
    }

    fs_disk->write(curr_dir, dirBlock.data);

    return true;
}

// FIXME: abaixo sera em outra classe

// Necessario para criar arquivo

bool add_dir_entry(const uint32_t& nodeId, char name[], Block& dirBlock) {

    uint32_t last = 0;
    for (; last < FS_DIR_PER_BLOCK; last++) {
        if ((last < 2) && (strlen(dirBlock.dirs[last].szName) == 0))
            break;

        if (streq(dirBlock.dirs[last].szName, name)) {
            printf("File already exists\n");
            return false;
        }

        if ((last >= 2) && (dirBlock.dirs[last].inum == 0)) // vazio a partir daqui
            break;
    }

    DirEntry entry;
    memset(&entry, 0, sizeof(DirEntry));
    strcpy(entry.szName, name);
    entry.inum = nodeId;
    memcpy(&(dirBlock.dirs[last]), &entry, sizeof(DirEntry)); // FIXME: esta certo ????

    return true;
}
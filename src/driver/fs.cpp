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
            if (block.Inodes[j].bonds > 0) {
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

    // Define totalizadores dos grupos de blocos (inode, data, directory)
    block.Super.MagicNumber = FileSystem::MAGIC_NUMBER;
    block.Super.Blocks = disk->size();
    block.Super.InodeBlocks = (uint32_t)std::ceil((int(block.Super.Blocks) * 1.00) / 10);
    block.Super.Inodes = block.Super.InodeBlocks * (FileSystem::INODES_PER_BLOCK);
    block.Super.MapBlocks = (uint32_t)std::ceil((int(block.Super.Blocks) * 1.00) / 100);

    // Define parametros de segurança
    block.Super.Protected = 0;                // Zera campos segurança
    memset(block.Super.PasswordHash, 0, 257); // Zera hash root
    disk->write(startBlockSuper, block.Data);

    // Define inicio de blocos de dados e diretorio
    startBlockData = startBlockInode + block.Super.InodeBlocks;
    startBlockMapFree = block.Super.Blocks - block.Super.MapBlocks;

    // Zera Blocos de Inode
    for (uint32_t i = startBlockInode; i < startBlockData; i++) {
        Block inodeBlock;
        for (uint32_t j = 0; j < FileSystem::INODES_PER_BLOCK; j++) {
            inodeBlock.Inodes[j].bonds = 0;
            inodeBlock.Inodes[j].mode = 0; // 0x01ff; tttt 000r wxrw xrwx
            inodeBlock.Inodes[j].Size = 0;

            for (uint32_t k = 0; k < FileSystem::POINTERS_PER_INODE; k++)
                inodeBlock.Inodes[j].Direct[k] = 0;

            inodeBlock.Inodes[j].Indirect = 0;
        }

        disk->write(i, inodeBlock.Data);
    }

    // Zera Bloco de Dados depois dos blocos de inode
    for (uint32_t i = startBlockData; i < startBlockMapFree; i++) {
        Block DataBlock;
        memset(DataBlock.Data, 0, Disk::BLOCK_SIZE);
        disk->write(i, DataBlock.Data);
    }

    // Zera Bloco Mapa Free
    for (uint32_t i = startBlockMapFree; i < block.Super.Blocks; i++) {
        Block FreeBlock;
        memset(FreeBlock.Data, 0, Disk::BLOCK_SIZE);
        disk->write(i, FreeBlock.Data);
    }

    // Cria entrada diretorio root
    Block blockINode;
    disk->read(startBlockInode, blockINode.Data); // Le bloco 0 de iNode
    Inode* node = &blockINode.Inodes[0];          // pega Inode
    (node->bonds)++;                              // Marca coo valido
    node->mode = 0b0000000100100100;              // 0000 000r--r--r-- Diretorio
    node->Direct[0] = startBlockData;             // Aloca primeiro inode data valido

    // inicializa Diretorio root
    Block Dirblock;
    memset(&Dirblock, 0, sizeof(Block));

    if (this->add_dir_entry(0, (char*)".", &Dirblock) == true) {
        if (this->add_dir_entry(0, (char*)"..", &Dirblock) == true) {
            disk->write(node->Direct[0], Dirblock.Data);
            disk->write(startBlockInode, blockINode.Data);
            return true;
        }
    }

    return false;
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

    if (block.Super.MapBlocks != (uint32_t)std::ceil((int(block.Super.Blocks) * 1.00) / 100))
        return false;

    // define inicio de cada grupo de blocos
    startBlockData = startBlockInode + block.Super.InodeBlocks;
    startBlockMapFree = block.Super.Blocks - block.Super.MapBlocks;

    // se fs estiver protegido
    if (block.Super.Protected) {
        char pass[BUFSIZ], line[BUFSIZ]; // FIXME : merda!!!!
        printf("Enter password: ");
        if (fgets(line, BUFSIZ, stdin) == nullptr) {
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

    // Marca blocos de boot, super e inode como ocupados
    for (uint32_t i = startBlockBoot; i < startBlockInode; i++)
        free_blocks[i] = true;

    // Percorre Blocos de inodes para marcar blocos de inode e dados em uso
    for (uint32_t i = startBlockInode; i < startBlockData; i++) {

        // Indice do numero de Inode
        uint32_t indiceBlocoInode = i - startBlockInode;

        // Le bloco inteiro de Inode
        disk->read(i, block.Data);

        for (uint32_t j = 0; j < INODES_PER_BLOCK; j++) {
            if (block.Inodes[j].bonds > 0) {
                this->inode_counter[indiceBlocoInode]++;

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
                                if (indirect.Pointers[k] != 0)
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

    // Carrega Diretorio Root
    Block blockINode;
    disk->read(startBlockInode, blockINode.Data); // Le bloco 0 de iNode
    Inode* node = &blockINode.Inodes[0];          // pega Inode
    uint8_t tipo = node->mode >> 12;
    if ((node->bonds > 0) && (tipo == 0)) {

        curr_dir = node->Direct[0];
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

        if (inode_counter[indexBlockInode] == INODES_PER_BLOCK)
            continue;
        else
            this->fs_disk->read(i, block.Data);

        for (uint32_t indexINode = 0; indexINode < INODES_PER_BLOCK; indexINode++) {
            if (block.Inodes[indexINode].bonds == 0) {
                block.Inodes[indexINode].bonds++;
                block.Inodes[indexINode].mode = 0b0001000110110110;
                block.Inodes[indexINode].Size = 0;
                block.Inodes[indexINode].Indirect = 0;
                for (int indexDirect = 0; indexDirect < 5; indexDirect++) {
                    block.Inodes[indexINode].Direct[indexDirect] = 0;
                }
                free_blocks[i] = true;
                inode_counter[indexBlockInode]++;

                this->fs_disk->write(i, block.Data);

                return (((indexBlockInode)*INODES_PER_BLOCK) + indexINode);
            }
        }
    }

    return -1;
}

bool FileSystem::load_inode(size_t inumber, Inode* node) {

    // valida range
    if (!mounted || (inumber > MetaData.Inodes) || (inumber < 0))
        return false;

    // encontra o indice do iNode no vetor de inodes
    uint32_t indiceInodeLocal = inumber / INODES_PER_BLOCK;

    // Valida se bloco de iNode nao esta vazio no indice de Inodes
    if (this->inode_counter[indiceInodeLocal]) {

        Block block;

        // Encontra o Bloco de iNode Correto
        uint32_t iBlock = indiceInodeLocal + startBlockInode;

        // Encontra o indice de Inode dentro do Bloco encontrado
        int indexINode = inumber % INODES_PER_BLOCK;

        // Le o bloco de iNode Inteiro
        fs_disk->read(iBlock, block.Data);

        // Se iNode estiver valido para uso carregar na variavel de retorno por ref
        if (block.Inodes[indexINode].bonds > 0) {
            *node = block.Inodes[indexINode];
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
        node.Size = 0;

        uint32_t indiceInodeLocal = inumber / INODES_PER_BLOCK;
        uint32_t iBlock = indiceInodeLocal + startBlockInode;

        if (!(--inode_counter[indiceInodeLocal])) {
            this->free_blocks[iBlock] = false;
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
        fs_disk->read(iBlock, block.Data);
        block.Inodes[inumber % INODES_PER_BLOCK] = node;
        fs_disk->write(iBlock, block.Data);

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

    // encocntra bloco de posicao do inode correspondente
    int i = (inumber / INODES_PER_BLOCK) + startBlockInode;
    int j = inumber % INODES_PER_BLOCK;

    // Le o bloco inteiro e grava os novos dados do inode em sua posicao
    Block block;
    fs_disk->read(i, block.Data);
    block.Inodes[j] = *node;
    fs_disk->write(i, block.Data);

    // TODO: melhorar
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
        // entradas consecutivas com offset != 0 inode sera atualizado
        node.bonds++;
        node.Size = length + offset;
        for (uint32_t ii = 0; ii < POINTERS_PER_INODE; ii++) {
            node.Direct[ii] = 0;
        }
        node.Indirect = 0;
        inode_counter[inumber / INODES_PER_BLOCK]++;
        free_blocks[inumber / INODES_PER_BLOCK + 1] = true;
    } else {
        // primeira entrada com offset 0, inode sera preenchido pela primeira vez
        node.Size = std::max((size_t)node.Size, length + offset);
    }

    if (offset < POINTERS_PER_INODE * Disk::BLOCK_SIZE) {
        // arquivo cabe em 1 a 5 blocos (sem indireção)
        int direct_node = offset / Disk::BLOCK_SIZE;
        offset %= Disk::BLOCK_SIZE;

        if (!check_allocation(&node, read, orig_offset, node.Direct[direct_node], false, indirect)) {
            // falhou em alocar todo o espaço grava apenas o que conseguiu
            return write_ret(inumber, &node, read);
        }
        // copia dados do buffer de entrada para o bloco no fs (direct_node)
        read_buffer(offset, &read, length, data, node.Direct[direct_node++]);

        if (read == length)
            // atualiza inode com dados gravados e sai da funcao
            return write_ret(inumber, &node, length);
        else {

            // continua a escrever nos blocos diretos
            for (int i = direct_node; i < (int)POINTERS_PER_INODE; i++) {
                if (!check_allocation(&node, read, orig_offset, node.Direct[direct_node], false, indirect)) {
                    return write_ret(inumber, &node, read);
                }
                // copia dados do buffer de entrada para o bloco no fs (direct_node)
                read_buffer(0, &read, length, data, node.Direct[direct_node++]);

                // dados copiados em todas as passadas, atualiza o inode
                if (read == length)
                    return write_ret(inumber, &node, length);
            }

            // se indirect ja foi instanciado
            if (node.Indirect)
                fs_disk->read(node.Indirect, indirect.Data);
            else {

                // Aloca e formata bloco de indirecao
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

        // Se Node indirect ja esta instanciado ler bloco de dados do mesmo
        if (node.Indirect)
            fs_disk->read(node.Indirect, indirect.Data);
        else {
            // primeira entrado do node indirect alocar bloco para indirect
            if (!check_allocation(&node, read, orig_offset, node.Indirect, false, indirect)) {
                return write_ret(inumber, &node, read);
            }

            // le bloco com dados do indirect
            fs_disk->read(node.Indirect, indirect.Data);

            // Limpa ponteiros dentro de indirect
            for (int i = 0; i < (int)POINTERS_PER_BLOCK; i++) {
                indirect.Pointers[i] = 0;
            }
        }

        // Aloca novo bloco para dados e coloca indice do iNode no vetor de indirect
        if (!check_allocation(&node, read, orig_offset, indirect.Pointers[indirect_node], true, indirect)) {
            return write_ret(inumber, &node, read);
        }

        // escreve dados no bloco
        read_buffer(offset, &read, length, data, indirect.Pointers[indirect_node++]);

        if (read == length) {
            // se dados cabem escreve bloco do indirect e atualiza iNode do arquivo
            fs_disk->write(node.Indirect, indirect.Data);
            return write_ret(inumber, &node, length);
        } else {
            for (int j = indirect_node; j < (int)POINTERS_PER_BLOCK; j++) {
                // Aloca novo bloco
                if (!check_allocation(&node, read, orig_offset, indirect.Pointers[j], true, indirect)) {
                    return write_ret(inumber, &node, read);
                }
                // escreve dados no bloco
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

bool FileSystem::add_dir_entry(const uint32_t& nodeId, char name[], Block* dirBlock) {

    uint32_t last = 0;
    for (; last < FileSystem::DIR_PER_BLOCK; last++) {
        if ((last < 2) && (strlen(dirBlock->Directories[last].Name) == 0))
            break;

        if (streq(dirBlock->Directories[last].Name, name)) {
            printf("File already exists\n");
            return false;
        }

        if ((last >= 2) && (dirBlock->Directories[last].inum == 0)) // vazio a partir daqui
            break;
    }

    DirEntry entry;
    memset(&entry, 0, sizeof(DirEntry));
    strcpy(entry.Name, name);
    entry.inum = nodeId;
    memcpy(&(dirBlock->Directories[last]), &entry, sizeof(DirEntry));

    return true;
}

bool FileSystem::touch(char name[FileSystem::NAMESIZE]) {
    if (!mounted) {
        return false;
    }

    Block dirBlock;
    fs_disk->read(curr_dir, dirBlock.Data);

    // Aloca um inode para os dados do arquivo
    ssize_t new_node_idx = this->create();
    if (new_node_idx == -1) {
        printf("Error creating new Dir inode\n");
        return false;
    }

    if (this->add_dir_entry(new_node_idx, name, &dirBlock) == false) {
        this->remove(new_node_idx);
        return false;
    }

    fs_disk->write(curr_dir, dirBlock.Data);

    return true;
}
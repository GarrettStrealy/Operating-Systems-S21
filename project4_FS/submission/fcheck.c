#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include "fs.h"

#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
uint dirsize = sizeof(struct dirent);
#define DIRSIZE dirsize

#define PERROR(msg...) fprintf(stderr, msg)

int fsfd;
int inode_blocks;
int datablocks_start;
int datablocks_end;
int bitmap_start;

char *mem_map_image;
int *bitmap_references;
int *indirect_pointers;
int *reference_count;
int *directory_reference_count;
int *inodes_allocated;

struct superblock superblock;
struct stat file_stat;

// helps get a requested inode
void get_inode(int inode_number, struct dinode *inode)
{
    struct dinode *temp;
    char buf[BSIZE];
    int bn = (inode_number / IPB) + 2;

    lseek(fsfd, bn * BSIZE, 0);
    read(fsfd, buf, BSIZE);

    temp = ((struct dinode *)buf) + (inode_number % IPB);
    *inode = *temp;
}

// helper for check_directory
// make sure the given inode exists and update its number of references
void check_type(int inode_number, char *name)
{
    struct dinode inode;
    get_inode(inode_number, &inode);

    if (inode.type <= 0)
    {
        PERROR("ERROR: inode referred to in directory but marked free.\n");
        exit(EXIT_FAILURE);
    }
    else 
    {
        if (inode.type == T_FILE)
        {
            reference_count[inode_number]--;
        }
        else if (inode.type == T_DIR)
        {   
            if (strcmp(name, ".") != 0 && strcmp(name, "..") != 0) 
            {
                directory_reference_count[inode_number]--;
            }
        }
    }
}

// helper for check_direct_pointers and check_indirect_pointers
// make sure the given block's bit is set correctly
void check_block(int block)
{
    char buf[BSIZE];
    int addr = bitmap_start + (block / (BSIZE * 8));

    lseek(fsfd, addr * BSIZE, 0);
    read(fsfd, buf, BSIZE);

    if ((buf[block / 8] & MASK[block % 8]) == 0)
    {
        PERROR("ERROR: address used by inode but marked free in bitmap.\n");
        exit(EXIT_FAILURE);
    }
}

// helper for check_direct_pointers and check_indirect_pointers
// make sure the given directory is properly formatted
void check_directory(int inode_number, int addr, int root_directory)
{
    // read the data block for the directory into buffer
    char buf[BSIZE];
    lseek(fsfd, addr * BSIZE, 0);
    read(fsfd, buf, BSIZE);

    if (root_directory == 1)
    {
        struct dirent *current_dir = (struct dirent *)buf;
        struct dirent *parent_dir = (struct dirent *)(buf + sizeof(struct dirent));

        ushort current_dir_inum = current_dir->inum;
        ushort parent_dir_inum = parent_dir->inum;

        // Checking for root directory
        if (inode_number == 1)
        {   
            if (current_dir_inum != 1 || parent_dir_inum != 1)
            {
                PERROR("ERROR: root directory does not exist.\n");
                exit(EXIT_FAILURE);
            }
        }
        // Checking that current directory refers to itself
        else if (inode_number != current_dir_inum)
        {
            PERROR("ERROR: directory not properly formatted.\n");
            exit(EXIT_FAILURE);
        }

        check_type(current_dir_inum, current_dir->name);
        if (current_dir_inum != 0)
        {
            inodes_allocated[current_dir_inum]--;
        }

        check_type(parent_dir_inum, parent_dir->name);
        if (parent_dir_inum != 0)
        {
            inodes_allocated[parent_dir_inum]--;
        }
    }

    int start = 0;
    // Updating the inodes used
    if (root_directory)
    {
        start = 2;
    }

    int max_directories = BSIZE / DIRSIZE;
    struct dirent *entry;

    int dir;
    for (dir = start; dir < max_directories; dir++)
    {
        entry = (struct dirent *)((dir * DIRSIZE) + buf);

        switch (entry->inum) {
            case 0: 
                continue;
            default: 
                check_type(entry->inum, entry->name);
                inodes_allocated[entry->inum]--;
        }
    }
}

// helper for check_inodes
// check the given inode's direct pointers
void check_direct_pointers(struct dinode inode, int inode_number)
{
    int i, block_addr, root_directory;

    for (i = 0; i < NDIRECT; i++)
    {   
        block_addr = inode.addrs[i];

        if (block_addr == 0)
        {
            continue;
        }
        else if (block_addr < datablocks_start || block_addr > datablocks_end)
        {
            PERROR("ERROR: bad direct address in inode.\n");
            exit(EXIT_FAILURE);
        }
        else
        {
            // make sure block is marked used in bitmap
            check_block(block_addr);

            // update number of bitmap references
            bitmap_references[block_addr - datablocks_start]--;

            // if a directory, make sure it is properly formatted
            if (inode.type == T_DIR)
            {
                switch (i)
                {
                    case 0: 
                        root_directory = 1;
                        break;
                    default: 
                        root_directory = 0;
                }
                check_directory(inode_number, block_addr, root_directory);
            }
        }
    }
}

// helper for check_inodes
// check the given inode's indirect pointers
void check_indirect_pointers(struct dinode inode, int inode_number)
{
    int indirect = inode.addrs[NDIRECT];
    int block_number;

    if (indirect == 0) {
        // do nothing
    }
    else if (indirect < datablocks_start || indirect > datablocks_end)
    {
        PERROR("ERROR: bad indirect address in inode.\n");
        exit(EXIT_FAILURE);
    }
    else
    {
        check_block(indirect);

        block_number = indirect - datablocks_start;

        indirect_pointers[block_number]++;

        bitmap_references[block_number]--;

        char buf[BSIZE];

        lseek(fsfd, indirect * BSIZE, 0);
        read(fsfd, buf, BSIZE);

        int i, block_addr;
        for (i = 0; i < 128; i++)
        {
            block_addr = *((int *)(buf + (i * 4)));
            block_number = block_addr - datablocks_start;
            if (block_addr != 0)
            {
                if (block_addr < datablocks_start || block_addr > datablocks_end)
                {
                    PERROR("ERROR: bad indirect address in inode.\n");
                    exit(EXIT_FAILURE);
                }
                else
                {
                    check_block(block_addr);

                    indirect_pointers[block_number]++;

                    bitmap_references[block_number]--;

                    if (inode.type == T_DIR)
                    {
                        check_directory(inode_number, block_addr, 0);
                    }
                }
            }
        }
    }
}

// helper for main
// get the details of the bitmap
void get_bitmap_info()
{
    char buf[BSIZE];

    lseek(fsfd, bitmap_start * BSIZE, 0);
    read(fsfd, buf, BSIZE);

    int i;
    int j = 0;
    for (i = datablocks_start; i < datablocks_end; i++, j++)
    {
        bitmap_references[j] = ((buf[i / 8] & MASK[i % 8]) != 0);
    }
}

// helper for main
// get the details of each inode
void get_inodes_info()
{
    struct dinode inode;

    int i;
    for (i = 1; i < superblock.ninodes; i++)
    {
        get_inode(i, &inode);

        switch(inode.type) {
            case T_FILE:
                reference_count[i] = inode.nlink;
                inodes_allocated[i] = 1;
                break;
            case T_DIR:
                directory_reference_count[i] = 1;
                inodes_allocated[i] = 1;
                break;
            case T_DEV:
                inodes_allocated[i] = 1;
            default:
                break;
        }
    }
}

// helper for main
// make sure inodes are correct
void check_inodes()
{
    struct dinode inode;

    int i;
    for (i = 1; i < superblock.ninodes; i++)
    {
        get_inode(i, &inode);

        if (inode.type < 0 || inode.type > 3)
        {
            PERROR("ERROR: bad inode.\n");
            exit(EXIT_FAILURE);
        }
        else if (i == 1) // should be root directory
        {
            if (inode.type != T_DIR)
            {
                PERROR("ERROR: root directory does not exist.\n");
                exit(EXIT_FAILURE);
            }
        }

        check_direct_pointers(inode, i);

        check_indirect_pointers(inode, i);
    }
}

// helper for main
// make sure blocks are only used once
void check_addresses(int i)
{
    if (indirect_pointers[i] > 0)
    {
        PERROR("ERROR: indirect address used more than once.\n");
        exit(EXIT_FAILURE);
    }
    else
    {
        PERROR("ERROR: direct address used more than once.\n");
        exit(EXIT_FAILURE);
    }
}

// helper for main
// make sure bitmap is correct
void check_bitmap()
{
    int i;
    for (i = 0; i < superblock.nblocks; i++)
    {
        if (bitmap_references[i] == 1)
        {
            PERROR("ERROR: bitmap marks block in use but it is not in use.\n");
            exit(EXIT_FAILURE);
        }
        else if (bitmap_references[i] < 0)
        {
            check_addresses(i);
        }
    }
}

// helper for main
// make sure all inodes are referred to in some directory
void check_directories()
{
    int i;
    for (i = 0; i < superblock.ninodes; i++)
    {
        if (directory_reference_count[i] < 0)
        {
            PERROR("ERROR: directory appears more than once in file system.\n");
            exit(EXIT_FAILURE);
        }
        else if (inodes_allocated[i] == 1)
        {
            PERROR("ERROR: inode marked use but not found in a directory.\n");
            exit(EXIT_FAILURE);
        }
        else if (reference_count[i] >= 1 || reference_count[i] < 0)
        {
            PERROR("ERROR: bad reference count for file.\n");
            exit(EXIT_FAILURE);
        }
    }
}

// helper for main
// set global variables and allocate memory
void init()
{
    // get file stat
    if (fstat(fsfd, &file_stat) < 0)
    {
        exit(EXIT_FAILURE);
    }

    // Map memory
    char *mem_map_image = mmap(NULL, file_stat.st_size, PROT_READ, MAP_PRIVATE, fsfd, 0);

    superblock = *((struct superblock *)(mem_map_image + BSIZE));

    directory_reference_count = malloc(sizeof(int) * superblock.ninodes);
    reference_count = malloc(sizeof(int) * superblock.ninodes);
    indirect_pointers = malloc(sizeof(int) * superblock.nblocks);
    bitmap_references = malloc(sizeof(int) * superblock.nblocks);
    inodes_allocated = malloc(sizeof(int) * superblock.ninodes);

    // First bitmap block number
    bitmap_start = 3 + (superblock.ninodes / (BSIZE / sizeof(struct dinode)));
    // First data block number
    datablocks_start = bitmap_start + (superblock.nblocks / (BSIZE * 8)) + 1;
    // Last data block number
    datablocks_end = datablocks_start + superblock.nblocks;
}

// helper for main
// close file and free memory
void cleanup()
{
    close(fsfd);
    free(bitmap_references);
    free(indirect_pointers);
    free(inodes_allocated);
    free(reference_count);
    free(directory_reference_count);
}

int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        PERROR("Usage: xcheck <file_system_image>\n");
        exit(EXIT_FAILURE);
    }

    // open file system image for reading
    if ((fsfd = open(argv[1], O_RDONLY)) < 0)
    {
        PERROR("image not found.\n");
        exit(EXIT_FAILURE);
    }

    init();

    get_inodes_info();
    get_bitmap_info();
    check_inodes();
    check_bitmap();
    check_directories();

    cleanup();

    exit(EXIT_SUCCESS);
}
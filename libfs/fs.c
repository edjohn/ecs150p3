#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "disk.h"
#include "fs.h"

#define SIGNATURE "ECS150FS"
#define SIGNATURE_LENGTH 8
#define ROOT_PADDING 10
#define SUPERBLOCK_PADDING 4079
#define MAX_ROOT_ENTRIES 128
#define FAT_ENTRIES 8192
#define BLOCK_SIZE 4096 
#define MAX_FILENAME 16 
#define FAT_EOC 0xFFFF 


//superblock 
typedef struct __attribute__((packed)) {
	uint8_t signature[SIGNATURE_LENGTH]; 
	uint16_t total_block_amount;
	uint16_t root_block_index;
	uint16_t data_block_index;
	uint16_t data_block_amount;
	uint8_t fat_block_amount;
	uint8_t padding[SUPERBLOCK_PADDING];
} SuperBlock;

//single block of FAT
typedef struct __attribute__((packed)) {
	uint16_t entries[BLOCK_SIZE/2];
} FAT;


typedef struct __attribute__((packed)) {
	uint8_t file_name[MAX_FILENAME];
	uint32_t file_size;
	uint16_t first_data_block_index;
	uint8_t padding[ROOT_PADDING];
} RootEntry;

typedef struct __attribute__((packed)) {
	uint16_t offset;
	uint8_t index;
	int in_use; 
} FileDescriptor;


static  SuperBlock *super_block;
static  FAT *fat_entries;
static  RootEntry *RootEntryArray;
static struct FileDescriptor *fd_table[FS_OPEN_MAX_COUNT];


// make fs on disk accessible on diskname (OS)
int fs_mount(const char *diskname)
{
	// Open virtual disk
	if (block_disk_open(diskname) != 0) {
		return -1;
	}
	// Read super_block at beginning of virtual disk
	super_block = malloc(sizeof(SuperBlock));
	if (!super_block || block_read(0, super_block) == -1) {
        fprintf(stderr, "Error: unable to read the superblock from disk.\n");
        free(super_block);
        block_disk_close();
        return -1;
    }
	// Verify signature has correct signature
	if (memcmp(super_block->signature, SIGNATURE, SIGNATURE_LENGTH) != 0) {
		fprintf(stderr, "Error: disk signature doesn't match.\n");
        free(super_block);
        block_disk_close();
        return -1;
	}

	// Verify super_block has correct block amount
	if (super_block->total_block_amount != block_disk_count()) {
		fprintf(stderr, "Error: super_block has wrong block amount.\n");
		free(super_block);
    	block_disk_close();
		return -1;
	}
	//printf("made it past initial checks");


	// Read blocks into a FAT array
	fat_entries = malloc(sizeof(FAT) * super_block->fat_block_amount);
	if (!fat_entries) {
        fprintf(stderr, "Error: unable to allocate memory for the FAT.\n");
        free(super_block);
        block_disk_close();
        return -1;
    }

	// Read the FAT blocks from disk
	for (int i = 0; i < super_block->fat_block_amount; i++) {
		if (block_read(1 + i, &fat_entries[i]) == -1) {
			free(fat_entries);
			free(super_block);
			return -1; // Handle read failure
		}
	}

	// Allocate memory for the root directory entries
    RootEntryArray = malloc(MAX_ROOT_ENTRIES * sizeof(RootEntry));
    if (RootEntryArray == NULL) {
        free(fat_entries);
        free(super_block);
        return -1; // Handle memory allocation failure
    }	

	// Read the root directory block from disk
    if (block_read(super_block->root_block_index, RootEntryArray) == -1) {
        free(RootEntryArray);
        free(fat_entries);
        free(super_block);
        return -1;
    }

    // Initialize the file descriptor table
    for (int i = 0; i < FS_OPEN_MAX_COUNT; i++) {
        fd_table[i] = NULL; // Set each file descriptor to NULL, indicating it's not in use
    }

    //printf("made it past initial checks");


	return 0;
}

int fs_umount(void)
{
	if (!super_block || !fat_entries || !RootEntryArray) {
        fprintf(stderr, "Error: Filesystem is not mounted.\n");
        return -1;
    }

	// Free the dynamically allocated memory
    if (super_block) {
        free(super_block);
        super_block = NULL;
    }

    if (fat_entries) {
        free(fat_entries);
        fat_entries = NULL;
    }

    if (RootEntryArray) {
        free(RootEntryArray);
        RootEntryArray = NULL;
    }

    // Close the virtual disk
    if (block_disk_close() == -1) {
        fprintf(stderr, "Error: Unable to close virtual disk.\n");
		return -1; // Closing the virtual disk failed
    }


	//reset fd table?

	// memset(&super_block, 0, sizeof(SuperBlock));
	// memset(&root_directory, 0, sizeof(RootEntry));
	// free(fat_entries);

	return 0;
}

int fs_info(void)
{
	if (!super_block || !fat_entries || !RootEntryArray) {
        fprintf(stderr, "Error: Filesystem is not mounted.\n");
        return -1;
    }

    // Print basic filesystem information
    printf("FS Info:\n");
    printf("total_blk_count=%d\n", super_block->total_block_amount);
    printf("fat_blk_count=%d\n", super_block->fat_block_amount);
    printf("rdir_blk=%d\n", super_block->root_block_index);
    printf("data_blk=%d\n", super_block->data_block_index);
    printf("data_blk_count=%d\n", super_block->data_block_amount);

    // Count free blocks in the FAT
    int free_fat_blocks = 0;
    for (int i = 0; i < super_block->data_block_amount; i++) {
        // Assuming each FAT struct represents a block of FAT entries
		if (fat_entries->entries[i] == 0){
			free_fat_blocks++;
		}
	}

    // Count free root directory entries
    int free_root_entries = 0;
    for (int i = 0; i < MAX_ROOT_ENTRIES; i++) {
        if (RootEntryArray[i].file_name[0] == '\0') { // Assuming an empty filename indicates a free entry
            free_root_entries++;
        }
    }

    // Print the ratios of free FAT blocks to total data blocks, and free root directory entries to maximum root entries
    printf("fat_free_ratio=%d/%d\n", free_fat_blocks, super_block->data_block_amount);
    printf("rdir_free_ratio=%d/%d\n", free_root_entries, MAX_ROOT_ENTRIES);

	return 0;
}

int fs_create(const char *filename)
{
	/* TODO: Phase 2 */
	if (!super_block || !fat_entries || !RootEntryArray) {
        fprintf(stderr, "Error: No filesystem is currently mounted.\n");
        return -1;
    }

	if (!filename || strlen(filename) >= MAX_FILENAME) {
        fprintf(stderr, "Error: Filename is invalid or too long.\n");
        return -1;
    }

	// Check for existing file with the same name
    for (int i = 0; i < MAX_ROOT_ENTRIES; i++) {
        if (strcmp((char *)RootEntryArray[i].file_name, filename) == 0) {
            fprintf(stderr, "Error: File already exists.\n");
            return -1;
        }
    }

	// Look for an empty entry in RootEntryArray
    int emptyEntry = -1;
    for (int i = 0; i < MAX_ROOT_ENTRIES; i++) {
        if (RootEntryArray[i].file_name[0] == '\0') { // Empty entry found
            emptyEntry = i;
            break;
        }
    }

	// Check if root directory is full
    if (emptyEntry == -1) {
        fprintf(stderr, "Error: Root directory is full.\n");
        return -1;
    }

	// Create the file by initializing RootEntry
    strcpy((char *)RootEntryArray[emptyEntry].file_name, filename);
    RootEntryArray[emptyEntry].file_size = 0;
    RootEntryArray[emptyEntry].first_data_block_index = FAT_EOC; // Indicating no data blocks are allocated yet

    // Write updated RootEntryArray back to disk
    if (block_write(super_block->root_block_index, RootEntryArray) == -1) {
        fprintf(stderr, "Error: Unable to write RootEntryArray to disk.\n");
        return -1;
    }

    return 0;

}

int fs_delete(const char *filename)
{
	/* TODO: Phase 2 */
    if (!super_block || !fat_entries || !RootEntryArray) {
        fprintf(stderr, "Error: No filesystem is currently mounted.\n");
        return -1;
    }

    if (!filename || strlen(filename) == 0 || strlen(filename) >= MAX_FILENAME) {
        fprintf(stderr, "Error: Invalid filename.\n");
        return -1;
    }
    // Check if file exists
    int fileIndex = -1;
    for (int i = 0; i < MAX_ROOT_ENTRIES; i++) {
        if (strcmp((char *)RootEntryArray[i].file_name, filename) == 0) {
            fileIndex = i;
            break;
        }
    }
    // if file not found 
    if (fileIndex == -1) {
        fprintf(stderr, "Error: File not found.\n");
        return -1;
    }

    // Free allocated FAT entries
    uint16_t currentBlock = RootEntryArray[fileIndex].first_data_block_index;
    while (currentBlock != 0xFFFF) {
        int fatBlockIndex = currentBlock / (BLOCK_SIZE / 2);
        uint16_t nextBlockIndex = fat_entries[fatBlockIndex].entries[currentBlock % (BLOCK_SIZE / 2)];
        fat_entries[fatBlockIndex].entries[currentBlock % (BLOCK_SIZE / 2)] = 0;  // Mark the block as free
        
        // Write the modified FAT block back to disk
        // Assuming FAT starts immediately after the superblock, at block 1
        int diskBlockNum = 1 + fatBlockIndex; // Add 1 to account for the superblock at block 0
        if (block_write(diskBlockNum, &fat_entries[fatBlockIndex]) == -1) {
            fprintf(stderr, "Error: Unable to write FAT block to disk.\n");
            return -1;
        }

        currentBlock = nextBlockIndex;
    }

    memset(&RootEntryArray[fileIndex], 0, sizeof(RootEntry));

    // Write the updated root directory back to disk
    if (block_write(super_block->root_block_index, RootEntryArray) == -1) {
        fprintf(stderr, "Error: Unable to write RootEntryArray to disk.\n");
        return -1;
    }

    return 0;
    
}

int fs_ls(void)
{
	/* TODO: Phase 2 */
    if (!super_block || !fat_entries || !RootEntryArray) {
        fprintf(stderr, "Error: No filesystem is currently mounted.\n");
        return -1;
    }

    printf("FS Ls:\n");

    // Step 2: Iterate through the Root Directory
    for (int i = 0; i < MAX_ROOT_ENTRIES; i++) {
        // Check if the entry is valid (non-empty)
        if (RootEntryArray[i].file_name[0] != '\0') {
            // Step 3: Print File Information
            // Assuming 'file_name' is a null-terminated string
            printf("file: %s, size: %d, data_blk: %d\n",
                   RootEntryArray[i].file_name,
                   RootEntryArray[i].file_size,
                   RootEntryArray[i].first_data_block_index);
        }
    }

    return 0;  // Indicate success
}

int fs_open(const char *filename)
{
	/* TODO: Phase 3 */
}

int fs_close(int fd)
{
	/* TODO: Phase 3 */
}

int fs_stat(int fd)
{
	/* TODO: Phase 3 */
}

int fs_lseek(int fd, size_t offset)
{
	/* TODO: Phase 3 */
}

int fs_write(int fd, void *buf, size_t count)
{
	/* TODO: Phase 4 */
}

int fs_read(int fd, void *buf, size_t count)
{
	/* TODO: Phase 4 */
}


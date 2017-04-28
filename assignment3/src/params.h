/*
  Copyright (C) 2012 Joseph J. Pfeiffer, Jr., Ph.D. <pfeiffer@cs.nmsu.edu>
  This program can be distributed under the terms of the GNU GPLv3.
  See the file COPYING.
  There are a couple of symbols that need to be #defined before
  #including all the headers.
*/

#ifndef _PARAMS_H_
#define _PARAMS_H_

// The FUSE API has been changed a number of times.  So, our code
// needs to define the version of the API that we assume.  As of this
// writing, the most current API version is 26
#define FUSE_USE_VERSION 26

// need this to get pwrite().  I have to use setvbuf() instead of
// setlinebuf() later in consequence.
#define _XOPEN_SOURCE 500

// maintain bbfs state in here
#include <math.h>
#include <limits.h>
#include <stdio.h>
#include <math.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <time.h>
#include <fuse.h>


#define BLOCK_SIZE 512
#define BITS_PER_BLOCK 4096
#define BITS_PER_BYTE 8
#define INODES_PER_BLOCK 8
#define ZERO_INDEX_BITS 7
#define VALUE 513
#ifdef NAME_MAX
#undef NAME_MAX
#endif
#define NAME_MAX 252

typedef struct{

	int size;
	int direct_ptrs[12];
	int indirect_ptr;
	int test;
	short flags;
	

}inode;


typedef struct{

	inode list[8];
	

}inode_block;


typedef struct{
	
	int dataregion_bitmap_blocks; //How many block needed for dataregion bitmap
	int dataregion_bitmap_start; // Which block does the dataregion bitmap start
	int inode_bitmap_blocks;	//How many blocks needed for inode bitmap
	int inode_bitmap_start;	//which block does the inode bitmap start
	int inode_blocks;	//How many blocks needed for inodes
	int inode_blocks_start;	//which block does the inodes start
	int total_inodes;
	int dataregion_blocks;	//How many data region blocks are needed
	int dataregion_blocks_start; //what block does the data region start
	int disksize;
	int random[6];

}metadata_info;



typedef struct{
	
	metadata_info list[8];

}super_block;

typedef struct{

	char filepath[508];
	int inode;

}filepath_block;

typedef struct{

	struct dirent list[2];

}directory_block;

int get_metadata_info(int total_size, metadata_info * info);
int check_inode_status(int inode_number);
int set_inode_status(int inode_number, int status);
int check_dataregion_status(int datablock_number);
int set_dataregion_status(int datablock_number, int status);
inode get_inode(int inode_number);
void set_inode(int inode_number, inode node);




struct sfs_state {
    FILE *logfile;
    char *diskfile;
};
#define SFS_DATA ((struct sfs_state *) fuse_get_context()->private_data)

#endif

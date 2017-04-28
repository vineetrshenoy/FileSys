/*
  Simple File System
  This code is derived from function prototypes found /usr/include/fuse/fuse.h
  Copyright (C) 2001-2007  Miklos Szeredi <miklos@szeredi.hu>
  His code is licensed under the LGPLv2.
*/

#include "params.h"
#include "block.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fuse.h>
#include <libgen.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>

#ifdef HAVE_SYS_XATTR_H
#include <sys/xattr.h>
#endif

#include "log.h"


char buffer[BLOCK_SIZE];
inode_block entry_buffer;
struct stat s;
metadata_info info; 
char * filepath;

extern int diskfile;
// CURRENTLY WORKS ONLY FOR TOTAL SIZE, MULTIPLES OF 4 MB (8MB, 16MB,32MB, ETC)
/*
  This function initializes all the structure: 25% is for metadata, 75% for data
  
  INPUT: The total size of the file, metadata_info pointer to store metadata value
  OUTPUT: 0 on success
*/
int get_metadata_info(int total_size, metadata_info * info){

  // ----------------------------------------------
  //This gets just the information for the data regions 

  int data_size = 0.75 * total_size; //75% of the total space will be used for the data region
  int data_blocks = data_size / BLOCK_SIZE;  //Gets the actual number of data blocks
  // See how many bitmap blocks are needed to address all the data blocks.
  //Each bitmap block can address BITS_PER_BLOCK blcoks
  int data_bitmap_blocks = data_blocks / BITS_PER_BLOCK; 
  info->dataregion_blocks = data_blocks;
  info->dataregion_bitmap_blocks = data_bitmap_blocks;

  //----------------------------------------------------

  int metadata_size = total_size - data_size; //25% of th total space for metadata
  int num_metadata_blocks = metadata_size / (BLOCK_SIZE); //Number of blocks based on size
  num_metadata_blocks = num_metadata_blocks - data_bitmap_blocks - 1; // Total blocks minus data_bitmap_blocks minus superblock

  //int inode_bitmap = (num_metadata_blocks / VALUE) + 1;
  int inode_bitmap =  num_metadata_blocks / VALUE; //gets the number of inodes. See documentation
  if (inode_bitmap % VALUE != 0){
    inode_bitmap++;
  }
  num_metadata_blocks = num_metadata_blocks - inode_bitmap;



  info->disksize = total_size;
  info->inode_blocks = num_metadata_blocks;
  info->inode_bitmap_blocks = inode_bitmap;
  
  info->total_inodes = info->inode_blocks * INODES_PER_BLOCK;

  info->dataregion_bitmap_start = 1;
  info->inode_bitmap_start = 1 + info->dataregion_bitmap_blocks;

  info->inode_blocks_start = 1 + info->dataregion_bitmap_blocks + info->inode_bitmap_blocks;
  info->dataregion_blocks_start = 1 + info->dataregion_bitmap_blocks + info->inode_bitmap_blocks + info->inode_blocks;


  return 0;

}




/*
  Checks the status of a specific inode. Returns this value
  
  INPUT: The inode number to check
  OUTPUT: The status (1 allocated, 0 unallocated)
*/
int check_inode_status(int inode_number){

  int blk_number = inode_number/(BITS_PER_BLOCK); //Finds out in which block bit is
  block_read(info.inode_bitmap_start + blk_number, buffer); //reads the block

  int byte_offset = (inode_number - (BITS_PER_BLOCK * blk_number)) / BITS_PER_BYTE; //Finds how many bytes from begining that specific bit is
  char * ptr = buffer + byte_offset; 
  char data_bits = *ptr; //Obtains 8 bits in which desired bit is contained

  int bit_offset = inode_number - (BITS_PER_BLOCK * blk_number) - (byte_offset * BITS_PER_BYTE); //where in the 8 bits the desired is lcoated
  int bit = ZERO_INDEX_BITS - bit_offset; //counting offset from the MSB
  

  bit = (data_bits & ( 1 << bit )) >> bit; //Gets the required bit
    
  return bit;
}


/*
  Sets the status of a specific inode. Returns this value
  
  INPUT: The inode number to set, the value to set it to
  OUTPUT: The status (1 allocated, 0 unallocated)
*/
int set_inode_status(int inode_number, int status){

  int blk_number = inode_number/(BITS_PER_BLOCK); //Finds out in which block bit is
  block_read(info.inode_bitmap_start + blk_number, buffer); //reads the block

  int byte_offset = (inode_number - (BITS_PER_BLOCK * blk_number)) / BITS_PER_BYTE;//Finds how many bytes from begining that specific bit is
  char * ptr = buffer + byte_offset;
  char data_bits = *ptr; //Obtains 8 bits in which desired bit is contained

  int bit_offset = inode_number - (BITS_PER_BLOCK * blk_number) - (byte_offset * BITS_PER_BYTE); //where in the 8 bits the desired is lcoated
  int bit = ZERO_INDEX_BITS - bit_offset;

  data_bits ^= (-status ^ data_bits) & (1 << bit); //sets the required bit
  *ptr = data_bits; //puts set of 8 bits back into buffer
  block_write(info.inode_bitmap_start + blk_number, buffer); //writes block back to file
  return 0;

}



/*
  Checks the status of a specific inode. Returns this value
  
  INPUT: The inode number to check
  OUTPUT: The status (1 allocated, 0 unallocated)
*/
int check_dataregion_status(int datablock_number){

  int blk_number = datablock_number/(BITS_PER_BLOCK); //Finds out in which block bit is
  block_read(info.dataregion_bitmap_start + blk_number, buffer);  //reads the block

  int byte_offset = (datablock_number - (BITS_PER_BLOCK * blk_number)) / BITS_PER_BYTE; //Finds how many bytes from begining that specific bit is
  char * ptr = buffer + byte_offset; 
  char data_bits = *ptr; //Obtains 8 bits in which desired bit is contained

  int bit_offset = datablock_number - (BITS_PER_BLOCK * blk_number) - (byte_offset * BITS_PER_BYTE); //where in the 8 bits the desired is lcoated
  int bit = ZERO_INDEX_BITS - bit_offset; //counting offset from the MSB
  

  bit = (data_bits & ( 1 << bit )) >> bit; //Gets the required bit
    
  return bit;
}


/*
  Sets the status of a specific inode. Returns this value
  
  INPUT: The inode number to set, the value to set it to
  OUTPUT: The status (1 allocated, 0 unallocated)
*/
int set_dataregion_status(int datablock_number, int status){

  int blk_number = datablock_number/(BITS_PER_BLOCK); //Finds out in which block bit is
  block_read(info.dataregion_bitmap_start + blk_number, buffer);  //reads the block

  int byte_offset = (datablock_number - (BITS_PER_BLOCK * blk_number)) / BITS_PER_BYTE;//Finds how many bytes from begining that specific bit is
  char * ptr = buffer + byte_offset;
  char data_bits = *ptr; //Obtains 8 bits in which desired bit is contained

  int bit_offset = datablock_number - (BITS_PER_BLOCK * blk_number) - (byte_offset * BITS_PER_BYTE); //where in the 8 bits the desired is lcoated
  int bit = ZERO_INDEX_BITS - bit_offset;

  data_bits ^= (-status ^ data_bits) & (1 << bit); //sets the required bit
  *ptr = data_bits; //puts set of 8 bits back into buffer
  block_write(info.dataregion_bitmap_start + blk_number, buffer); //writes block back to file
  return 0;

}

/*
  Gets a copy of the specified inode at returns it to the user
  INPUT: The inode number that is requested
  OUTPUT: A struct containing the inode
*/

inode get_inode(int inode_number){
    
  inode node;
  int blk_number = inode_number / INODES_PER_BLOCK; // Finds which block to read
  block_read(info.inode_blocks_start + blk_number, &entry_buffer); // Reads the block
  
  
  int offset = inode_number - (INODES_PER_BLOCK * blk_number);
  node = (entry_buffer.list[offset]);
  return node;
  
}

/*
  Sets a certain inode in the metadata region
  INPUT: The inode number to write to, the inode itself
  OUTPUT: none
*/
void set_inode(int inode_number, inode node){

  int blk_number = inode_number / INODES_PER_BLOCK; // Finds which block to read
  block_read(info.inode_blocks_start + blk_number, &entry_buffer); // Reads the block
  
  
  int offset = inode_number - (INODES_PER_BLOCK * blk_number);
  entry_buffer.list[offset] = node;
  block_write(info.inode_blocks_start + blk_number, &entry_buffer);
  
}

/*
  Gets the number of directories in a file path
  INPUT: The file path
  OUTPUT: the number of different portions of the filepath
*/

int get_num_dirs(const char * filepath){
  int length = strlen(filepath);
  char slash = 47;
  int i, count;
  count = 0;
  for (i = 0; i < length; i++){
    if (filepath[i] == slash)
      count++;
  }
  return count;
}

/*
  Get each file path returned as a char *
  INPUT: The file path
  OUTPUT: A char ** that points to each string in the filepath
*/


char ** parsePath(const char * filepath){

  int length = strlen(filepath);
  int i, count;
  char slash = 47;
  count = 0;

  //Figures out how many "/" are present -- stores in count
  for (i = 0; i < length; i++){
    if (filepath[i] == slash)
      count++;
  }
  

  int * indices = (int * ) malloc ((count + 1) * sizeof(int)); //stores the index of each slash
  int j = 0;
  for (i = 0; i < length; i++){

    if (filepath[i] == slash){
      indices[j] = i;
      j++;
    }

    
  }
  indices[count] = length; 

  char ** strings = (char **) malloc(count * sizeof(char *)); //MUST FREE THIS LATER

  for (i = 0; i < count; i++){
    int size = (indices[i + 1] - indices[i]);
    strings[i] = (char *) malloc(sizeof(char) * size);
    strncpy(strings[i], (filepath + indices[i] + 1), (size - 1));
    
  }
  free(indices);
  
  return strings;

}

/*
  Finds the inode based on a filepath
  INPUT: The file path
  OUTPUT: An integer representing a inode
*/
int findInode(const char *path) {

  super_block sblock;
  block_read(0, &sblock);
  int dataRegionOffset = sblock.list[0].dataregion_blocks_start;
  filepath_block rblock;
  block_read(dataRegionOffset, &rblock);
  int inodeNum = rblock.inode;
  int inodeBlock = inodeNum/8;
  if (inodeNum%8 != 0) {
    inodeBlock++;
  }
  int inodeOffset = inodeNum - (inodeBlock - 1)*8;
  int numOfDirs = get_num_dirs(path);
  char ** fldrs = parsePath(filepath);
  inode_block iblock;
  filepath_block fblock;
  int i, j, gotem;
  inode node;
  // go through each inode of each folder in the path to find the inode for the filepath
  for (i = 0; i < numOfDirs; i++) {
    block_read(inodeBlock, &iblock);
    node = iblock.list[inodeOffset];
    gotem = 0;
    // check each direct_ptr in inode until the correct folder is found
    for (j = 0; j < 12; j++) {
      block_read(node.direct_ptrs[j], &fblock);
      int fcheck = 0;
      int l = 0;
      while (fldrs[i][l]) {
        if (fblock.filepath[l] != fldrs[i][l]) {
          fcheck = 1;
        }
        l++;
      }
      if (fcheck == 0) {
        gotem = 1;
        break;
      }
    }
    if (gotem == 1) {
      inodeNum = fblock.inode;
      if (i == numOfDirs - 1) {
        break;
      }
      inodeBlock = inodeNum/8;
      if (inodeNum%8 != 0) {
        inodeBlock++;
      }
      inodeOffset = inodeNum - (inodeBlock - 1)*8;
    }
    else {
      // do indirect ptr stuff
      int k;
      for (k = 0; k < numOfDirs; k++) {
        free(fldrs[k]);
      }
      return -1;
    }
  }
  for (i = 0; i < numOfDirs; i++) {
    free(fldrs[i]);
  }
  return inodeNum;
}

/*
  Finds the filepath block based on a path
  INPUT: The file path
  OUTPUT: An integer representing a inode
*/
int findFilepathBlock(const char *path) {

  super_block sblock;
  block_read(0, &sblock);
  int dataRegionOffset = sblock.list[0].dataregion_blocks_start;
  filepath_block rblock;
  block_read(dataRegionOffset, &rblock);
  int inodeNum = rblock.inode;
  int inodeBlock = inodeNum/8;
  if (inodeNum%8 != 0) {
    inodeBlock++;
  }
  int inodeOffset = inodeNum - (inodeBlock - 1)*8;
  int numOfDirs = get_num_dirs(path);
  char ** fldrs = parsePath(filepath);
  inode_block iblock;
  filepath_block fblock;
  int i, j, gotem, fblockNum;
  inode node;
  // go through each inode of each folder in the path to find the inode for the filepath
  for (i = 0; i < numOfDirs; i++) {
    block_read(inodeBlock, &iblock);
    node = iblock.list[inodeOffset];
    gotem = 0;
    // check each direct_ptr in inode until the correct folder is found
    for (j = 0; j < 12; j++) {
      block_read(node.direct_ptrs[j], &fblock);
      int fcheck = 0;
      int l = 0;
      while (fldrs[i][l]) {
        if (fblock.filepath[l] != fldrs[i][l]) {
          fcheck = 1;
        }
        l++;
      }
      if (fcheck == 0) {
        gotem = 1;
        break;
      }
    }
    if (gotem == 1) {
      inodeNum = fblock.inode;
      if (i == numOfDirs - 1) {
        break;
      }
      inodeBlock = inodeNum/8;
      if (inodeNum%8 != 0) {
        inodeBlock++;
      }
      inodeOffset = inodeNum - (inodeBlock - 1)*8;
    }
    else {
      // do indirect ptr stuff
    }
  }
  for (i = 0; i < numOfDirs; i++) {
    free(fldrs[i]);
  }
  return fblockNum;
}

int find_free_datablock(){
  int i;
  int totalDatablocks = info.dataregion_blocks;
  for (i = 0; i < totalDatablocks; i++){
    if (check_dataregion_status(i) == 0)
      return i;
    
  }

  return -1;
}

int find_free_inode(){
  int i;
  int totalInodes = info.total_inodes;
  for (i = 0; i < totalInodes; i++){
    if (check_inode_status(i) == 0)
      return i;
    
  }

  return -1;
}

///////////////////////////////////////////////////////////
//
// Prototypes for all these functions, and the C-style comments,
// come indirectly from /usr/include/fuse.h
//

/**
 * Initialize filesystem
 *
 * The return value will passed in the private_data field of
 * fuse_context to all file operations and as a parameter to the
 * destroy() method.
 *
 * Introduced in version 2.3
 * Changed in version 2.6
 */
void *sfs_init(struct fuse_conn_info *conn)
{

    inode node;
    inode_block block;
    super_block sblock;
    int count;

    filepath = SFS_DATA->diskfile;
    disk_open(filepath); //opens the disk
    count = 0;


    //clearing all fields for the node
    int i;  
    node.size = 0;
    node.indirect_ptr = 0;
    node.test = 0;
    node.flags = 0;

    for(i = 0; i < 12; i++){

      node.direct_ptrs[i] = 0;
    }

  //clearing all fields for the inode_entry
    for(i = 0; i < 8; i++){
      
      block.list[i] = node;

    }
    
    fstat(diskfile, &s); //get file information
    get_metadata_info(s.st_size, &info); //gets all the metadata info

    sblock.list[0] = info; //setting the superblock

    printf("Writing the superblock\n");
    block_write(count, &sblock);
    count++;

    printf("Writing the data bitmap\n");
    for (i = 0; i < info.dataregion_bitmap_blocks; i++){
      block_write(count, buffer);
      count++;
    }

    printf("Writing the inode bitmap\n");
    for (i = 0; i < info.inode_bitmap_blocks; i++){
      block_write(count, buffer);
      count++;
    } 

    printf("Writing the inode blocks\n");
    for (i = 0; i < info.inode_blocks; i++){
      block_write(count, &block);
      count++;
    }

    

    fprintf(stderr, "in bb-init\n");
    log_msg("\nsfs_init()\n");
    
    log_conn(conn);
    log_fuse_context(fuse_get_context());

    log_msg("\n # dataregion_bitmap_blocks = %d", info.dataregion_bitmap_blocks);
    log_msg("\n # dataregion_bitmap_start = %d", info.dataregion_bitmap_start);
    log_msg("\n # inode_bitmap_blocks = %d", info.inode_bitmap_blocks);
    log_msg("\n # inodeblocs = %d", info.inode_blocks);
    log_msg("\n inode blocks start %d", info.inode_blocks_start);
    log_msg("\n dataregion_blocks = %d", info.dataregion_blocks);
    //sfs_create("/.Trash", S_IRWXU, NULL);

    struct fuse_file_info fi;
    sfs_opendir("/", &fi);
    struct stat statbuf;
    sfs_getattr("/.xdg-volume-info", &statbuf);
    char buff[BLOCK_SIZE];
    fuse_fill_dir_t filler;
    sfs_readdir("/", &buff, filler, 0, &fi);
    sfs_releasedir("/", &fi);

    
    return SFS_DATA;
}

/**
 * Clean up filesystem
 *
 * Called on filesystem exit.
 *
 * Introduced in version 2.3
 */
void sfs_destroy(void *userdata)
{
    log_msg("\nsfs_destroy(userdata=0x%08x)\n", userdata);
    disk_close();
}

/** Get file attributes.
 *
 * Similar to stat().  The 'st_dev' and 'st_blksize' fields are
 * ignored.  The 'st_ino' field is ignored except if the 'use_ino'
 * mount option is given.
 */
int sfs_getattr(const char *path, struct stat *statbuf)
{
    int retstat = 0;
    char slash = 47;
    char fpath[PATH_MAX];
    
    log_msg("\nsfs_getattr(path=\"%s\", statbuf=0x%08x)\n",
    path, statbuf);

    memset(statbuf, 0, sizeof(struct stat)); // initialize buffer
    statbuf->st_dev = 0;
    statbuf->st_blksize = 0;
    statbuf->st_ino = 0;
    memset(&fpath, '?', PATH_MAX);  // initialize fpath before copying path into it so the final character can be found
    int i = 0;
    while (path[i]) {
      fpath[i] = path[i];
      i++;
    }
    char finalChar;
    for (i = 0; i < PATH_MAX; i++) {
      if (fpath[i] == '?') {
        finalChar = fpath[i - 1];
        break;
      }
    }

    if (finalChar == slash) {
      statbuf->st_mode = S_IFDIR | S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH;
      statbuf->st_nlink = 2;
    }
    else {
      statbuf->st_mode = S_IFREG | S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
      statbuf->st_nlink = 1;
    }

    statbuf->st_uid = getuid();
    statbuf->st_gid = getgid();
    statbuf->st_rdev = 0;
    super_block sblock;
    block_read(0, &sblock);
    int inodeNum = findInode(path);
    int inodeBlockOffset = sblock.list[0].inode_blocks_start;
    int inodeBlock = inodeNum/8;
    int inodeOffset = inodeNum - inodeBlock*8;
    inode_block iblock;
    block_read(inodeBlockOffset + inodeBlock, &iblock);
    statbuf->st_size = iblock.list[inodeOffset].size;
    statbuf->st_blocks = statbuf->st_size/BLOCK_SIZE;
    if (statbuf->st_size%8 != 0) {
      statbuf->st_blocks += 1;
    }
    /*
    statbuf->st_atime = time(NULL);
    statbuf->st_mtime = time(NULL);
    statbuf->st_ctime = time(NULL);
    */
    return retstat;
}

/**
 * Create and open a file
 *
 * If the file does not exist, first create it with the specified
 * mode, and then open it.
 *
 * If this method is not implemented or under Linux kernel
 * versions earlier than 2.6.15, the mknod() and open() methods
 * will be called instead.
 *
 * Introduced in version 2.5
 */
int sfs_create(const char *path, mode_t mode, struct fuse_file_info *fi)
{
    int retstat = 0;
    log_msg("\nsfs_create(path=\"%s\", mode=0%03o, fi=0x%08x)\n",
      path, mode, fi);
    
    super_block sblock;
    block_read(0, &sblock);
    int totalInodes = sblock.list[0].total_inodes;
    int totalDatablocks = sblock.list[0].dataregion_blocks;
    int inodeNum = findInode(path);
    // file does not exist
    if (inodeNum == -1) {
      int i, inodeStatus, datablockStatus;
      inode node;
      filepath_block fblock;
      // find available inode
      for (i = 0; i < totalInodes; i++) {
        inodeStatus = check_inode_status(i);
        // inode is available
        if (!inodeStatus) {
          set_inode_status(i, 1);
          node.size = 0;
          set_inode(i, node);
          inodeNum = i;
          break;
        }
      }
      // find available datablock
      for (i = 0; i < totalDatablocks; i++) {
        datablockStatus = check_dataregion_status(i);
        if(!datablockStatus) {
          int datablockOffset = sblock.list[0].dataregion_blocks_start + i;
          int numOfDirs = get_num_dirs(path);
          char ** fldrs = parsePath(path);
          int j = 0;
          while (fldrs[numOfDirs - 1][j]) {
            fblock.filepath[j] = fldrs[numOfDirs - 1][j];
            j++;
          }
          fblock.inode = inodeNum;
          block_write(datablockOffset + i, &fblock);
          break;
        }
      }
    }
    sfs_open(path, fi);
    
    return retstat;
}

/** Remove a file */
int sfs_unlink(const char *path)
{
    int retstat = 0;
    log_msg("sfs_unlink(path=\"%s\")\n", path);

    int inodeNum = findInode(path);
    int fblockNum = findFilepathBlock(path);
    inode node = get_inode(inodeNum);
    int i, direct_ptr;
    for (i = 0; i < 12; i++) {
      if (node.direct_ptrs[i] != 0) {
        set_dataregion_status(node.direct_ptrs[i], 0);
      }
    }
    set_dataregion_status(fblockNum, 0);
    set_inode_status(inodeNum, 0);
    
    return retstat;
}

/** File open operation
 *
 * No creation, or truncation flags (O_CREAT, O_EXCL, O_TRUNC)
 * will be passed to open().  Open should check if the operation
 * is permitted for the given flags.  Optionally open may also
 * return an arbitrary filehandle in the fuse_file_info structure,
 * which will be passed to all file operations.
 *
 * Changed in version 2.2
 */
int sfs_open(const char *path, struct fuse_file_info *fi)
{
    int retstat = 0;
    log_msg("\nsfs_open(path\"%s\", fi=0x%08x)\n",
      path, fi);

    int inodeNum = findInode(path);
    log_msg("\n The inode number is %d", inodeNum);
    if (inodeNum == -1) {
      //fi->flags = fi->flags | O_CREAT;
      sfs_create(path, S_IFREG, fi);
      return ENOENT;
    }
    
    return retstat;
}

/** Release an open file
 *
 * Release is called when there are no more references to an open
 * file: all file descriptors are closed and all memory mappings
 * are unmapped.
 *
 * For every open() call there will be exactly one release() call
 * with the same flags and file descriptor.  It is possible to
 * have a file opened more than once, in which case only the last
 * release will mean, that no more reads/writes will happen on the
 * file.  The return value of release is ignored.
 *
 * Changed in version 2.2
 */
int sfs_release(const char *path, struct fuse_file_info *fi)
{
    int retstat = 0;
    log_msg("\nsfs_release(path=\"%s\", fi=0x%08x)\n",
	  path, fi);
    

    return retstat;
}

/** Read data from an open file
 *
 * Read should return exactly the number of bytes requested except
 * on EOF or error, otherwise the rest of the data will be
 * substituted with zeroes.  An exception to this is when the
 * 'direct_io' mount option is specified, in which case the return
 * value of the read system call will reflect the return value of
 * this operation.
 *
 * Changed in version 2.2
 */
int sfs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
    int retstat = 0;
    log_msg("\nsfs_read(path=\"%s\", buf=0x%08x, size=%d, offset=%lld, fi=0x%08x)\n",
      path, buf, size, offset, fi);

    int inodeNum = findInode(path);
    inode node = get_inode(inodeNum);
    int fileSize = node.size;
    if (offset >= fileSize) {
      return 0;
    }
    if (offset + size > fileSize) {
      size = fileSize - offset;
    }
    int readBlockNum = offset/BLOCK_SIZE;
    if (offset%BLOCK_SIZE != 0) {
      readBlockNum++;
    }
    int readOffset = offset%BLOCK_SIZE;
    int numOfBlocksNeeded = 1;
    if (size > BLOCK_SIZE - readOffset) {
      numOfBlocksNeeded = (size + readOffset)/BLOCK_SIZE;
      if ((size + readOffset)%BLOCK_SIZE != 0) {
        numOfBlocksNeeded++;
      }
    }
    char buffer[BLOCK_SIZE];
    int unreadSize = size;
    int i;
    for (i = 0; i < numOfBlocksNeeded; i++) {
      block_read(node.direct_ptrs[readBlockNum + i], &buffer);
      if (size <= BLOCK_SIZE - readOffset) {
        int j;
        for (j = readOffset; j < readOffset + unreadSize; j++) {
          buf[j - readOffset] = buffer[j];
        }
        unreadSize = 0;
      }
      else {
        int j;
        for (j = readOffset; j < BLOCK_SIZE; j++) {
          buf[size - unreadSize + j - readOffset] = buffer[j];
        }
        unreadSize -= (BLOCK_SIZE - readOffset);
      }
    }
    retstat = size;

   
    return retstat;
}

/** Write data to an open file
 *
 * Write should return exactly the number of bytes requested
 * except on error.  An exception to this is when the 'direct_io'
 * mount option is specified (see read operation).
 *
 * Changed in version 2.2
 */
int sfs_write(const char *path, const char *buf, size_t size, off_t offset,
	     struct fuse_file_info *fi)
{
    int retstat = 0;
    log_msg("\nsfs_write(path=\"%s\", buf=0x%08x, size=%d, offset=%lld, fi=0x%08x)\n",
      path, buf, size, offset, fi);
    
    if (offset >= 12*BLOCK_SIZE) {
      return 0;
    }
    if (offset + size > 12*BLOCK_SIZE) {
      size = 12*BLOCK_SIZE - offset;
    }
    int inodeNum = findInode(path);
    inode node = get_inode(inodeNum);
    int writeBlockNum = offset/BLOCK_SIZE;
    if (offset%BLOCK_SIZE != 0) {
      writeBlockNum++;
    }
    int writeOffset = offset%BLOCK_SIZE;
    int currentNumOfBlocks = 0;
    int i;
    int numOfBlocksNeeded = 1;
    for (i = 0; i < 12; i++) {
      if (node.direct_ptrs[i] != 0) {
        currentNumOfBlocks++;
      }
    }
    if (currentNumOfBlocks < writeBlockNum) {
      while (currentNumOfBlocks != writeBlockNum) {
        currentNumOfBlocks++;
        node.direct_ptrs[currentNumOfBlocks] = find_free_datablock();
        set_dataregion_status(node.direct_ptrs[currentNumOfBlocks], 1);
      }
    }
    if (size > BLOCK_SIZE - writeOffset) {
      numOfBlocksNeeded = (size + writeOffset)/BLOCK_SIZE;
      if ((size + writeOffset)%BLOCK_SIZE != 0) {
        numOfBlocksNeeded++;
      }
      for (i = 0; i < numOfBlocksNeeded; i++) {
        currentNumOfBlocks++;
        node.direct_ptrs[currentNumOfBlocks] = find_free_datablock();
        set_dataregion_status(node.direct_ptrs[currentNumOfBlocks], 1);
      }
    }
    node.size = (currentNumOfBlocks - 1)*BLOCK_SIZE;
    if (size%BLOCK_SIZE != 0) {
      node.size += size%BLOCK_SIZE;
    }
    else {
      node.size += BLOCK_SIZE;
    }
    char buffer[BLOCK_SIZE];
    int unwrittenSize = size;
    for (i = 0; i < numOfBlocksNeeded; i++) {
      block_read(node.direct_ptrs[writeBlockNum + i], &buffer);
      if (size <= BLOCK_SIZE - writeOffset) {
        int j;
        for (j = writeOffset; j < writeOffset + unwrittenSize; j++) {
          buffer[j] = buf[j - writeOffset];
        }
        unwrittenSize = 0;
      }
      else {
        int j;
        for (j = writeOffset; j < BLOCK_SIZE; j++) {
          buffer[j] = buf[size - unwrittenSize + j - writeOffset];
        }
        unwrittenSize -= (BLOCK_SIZE - writeOffset);
      }
      block_write(node.direct_ptrs[writeBlockNum + i], &buffer);
    }
    retstat = size;
    
    return retstat;
}


/** Create a directory */
int sfs_mkdir(const char *path, mode_t mode)
{
    int retstat = 0;
    log_msg("\nsfs_mkdir(path=\"%s\", mode=0%3o)\n",
	    path, mode);
   
    
    return retstat;
}


/** Remove a directory */
int sfs_rmdir(const char *path)
{
    int retstat = 0;
    log_msg("sfs_rmdir(path=\"%s\")\n",
	    path);
    
    
    return retstat;
}


/** Open directory
 *
 * This method should check if the open operation is permitted for
 * this  directory
 *
 * Introduced in version 2.3
 */
int sfs_opendir(const char *path, struct fuse_file_info *fi)
{
    int retstat = 0;
    log_msg("\nsfs_opendir(path=\"%s\", fi=0x%08x)\n",
	  path, fi);
    
    
    return retstat;
}

/** Read directory
 *
 * This supersedes the old getdir() interface.  New applications
 * should use this.
 *
 * The filesystem may choose between two modes of operation:
 *
 * 1) The readdir implementation ignores the offset parameter, and
 * passes zero to the filler function's offset.  The filler
 * function will not return '1' (unless an error happens), so the
 * whole directory is read in a single readdir operation.  This
 * works just like the old getdir() method.
 *
 * 2) The readdir implementation keeps track of the offsets of the
 * directory entries.  It uses the offset parameter and always
 * passes non-zero offset to the filler function.  When the buffer
 * is full (or an error happens) the filler function will return
 * '1'.
 *
 * Introduced in version 2.3
 */
int sfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset,
	       struct fuse_file_info *fi)
{
    int retstat = 0;
    log_msg("\n entering readdir \n");
    int pathInodeNum = findInode(path);
    inode pathInode = get_inode(pathInodeNum);
    int i = 0;
    directory_block dblock;
    int fillerReturn;
    while (pathInode.direct_ptrs[i]) {
      block_read(pathInode.direct_ptrs[i], &dblock);
      fillerReturn = filler(buf, dblock.list[0].d_name, NULL, sizeof(struct dirent));
      if (fillerReturn != 0) {
        return retstat;
      }
      i++;
    }
    
    return retstat;
}

/** Release directory
 *
 * Introduced in version 2.3
 */
int sfs_releasedir(const char *path, struct fuse_file_info *fi)
{
    int retstat = 0;

    
    return retstat;
}

struct fuse_operations sfs_oper = {
  .init = sfs_init,
  .destroy = sfs_destroy,

  .getattr = sfs_getattr,
  .create = sfs_create,
  .unlink = sfs_unlink,
  .open = sfs_open,
  .release = sfs_release,
  .read = sfs_read,
  .write = sfs_write,

  .rmdir = sfs_rmdir,
  .mkdir = sfs_mkdir,

  .opendir = sfs_opendir,
  .readdir = sfs_readdir,
  .releasedir = sfs_releasedir
};

void sfs_usage()
{
    fprintf(stderr, "usage:  sfs [FUSE and mount options] diskFile mountPoint\n");
    abort();
}

int main(int argc, char *argv[])
{
    int fuse_stat;
    struct sfs_state *sfs_data;
    
    // sanity checking on the command line
    if ((argc < 3) || (argv[argc-2][0] == '-') || (argv[argc-1][0] == '-'))
	sfs_usage();

    sfs_data = malloc(sizeof(struct sfs_state));
    if (sfs_data == NULL) {
	perror("main calloc");
	abort();
    }

    // Pull the diskfile and save it in internal data
    sfs_data->diskfile = argv[argc-2];
    argv[argc-2] = argv[argc-1];
    argv[argc-1] = NULL;
    argc--;
    
    sfs_data->logfile = log_open();
    
    // turn over control to fuse
    fprintf(stderr, "about to call fuse_main, %s \n", sfs_data->diskfile);
    fuse_stat = fuse_main(argc, argv, &sfs_oper, sfs_data);
    fprintf(stderr, "fuse_main returned %d\n", fuse_stat);
    
    return fuse_stat;
}

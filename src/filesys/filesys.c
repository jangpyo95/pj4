#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "threads/thread.h"
#include "threads/malloc.h"
#include "filesys/buffer_cache.h"

/* Partition that contains the file system. */
struct block *fs_device;

static void do_format (void);

/* Initializes the file system module.
   If FORMAT is true, reformats the file system. */
void
filesys_init (bool format) 
{
  fs_device = block_get_role (BLOCK_FILESYS);
  if (fs_device == NULL)
    PANIC ("No file system device found, can't initialize file system.");

  inode_init ();
  free_map_init ();
  buffer_cache_init();


  if (format) 
    do_format ();

  free_map_open ();

  //added4 3-2
  //set current directory by root directory
  thread_current()->cur_dir = dir_open_root();
}

/* Shuts down the file system module, writing any unwritten data
   to disk. */
void
filesys_done (void) 
{
  free_map_close ();
}

/* Creates a file named NAME with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */
bool
filesys_create (const char *name, off_t initial_size) 
{
  block_sector_t inode_sector = 0;

  //added4 3-3 file create 
  char file_name[PATH_MAX]; 
  struct dir *dir = parsing_path(name,file_name);

  //struct dir *dir = dir_open_root ();
  bool success = (dir != NULL
                  && free_map_allocate (1, &inode_sector)
                  //added4 3-1
                  //set flag 0
                  && inode_create (inode_sector, initial_size,0)
                  && dir_add (dir, file_name, inode_sector));
  if (!success && inode_sector != 0) 
    free_map_release (inode_sector, 1);
  dir_close (dir);

  return success;
}

/* Opens the file with the given NAME.
   Returns the new file if successful or a null pointer
   otherwise.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
struct file *
filesys_open (const char *name)
{
  //added4 3-4
  //distinguish abolute path and relative path
  char file_name[PATH_MAX];
  struct dir *dir = parsing_path(name,file_name);
  //if(!strcmp(file_name,".") && !strcmp(file_name,".."))
  //  return NULL;

  struct inode *inode = NULL;

  if (dir == NULL)
     return NULL;
 
  dir_lookup (dir, file_name, &inode);
  dir_close (dir);

  return file_open (inode);
}

/* Deletes the file named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
bool
filesys_remove (const char *name) 
{
  //added4 3-5
  //remove file from specified directory
  //parsing the path, and get target's name
  char file_name[PATH_MAX];
  struct dir *dir = parsing_path(name,file_name);
 // printf("in remove");
  //check if target is file or directory
  struct inode *inode;
  dir_lookup(dir,file_name,&inode); 
  struct dir *file_dir=NULL;
  char read_name[PATH_MAX];

  //check if traget is file
  //if target were directory, check if file exist in target directory
  bool success = false;
  if(!is_directory(inode) || 
    ((file_dir = dir_open(inode)) && !dir_readdir(file_dir,read_name)))
    if( (dir != NULL) && (dir_remove (dir,file_name)))
      success = true;

  dir_close (dir);

  if(file_dir)
    dir_close(file_dir);

  return success;
}

/* Formats the file system. */
static void
do_format (void)
{
  printf ("Formatting file system...");
  free_map_create ();
  if (!dir_create (ROOT_DIR_SECTOR, 16))
    PANIC ("root directory creation failed");

  //added4
  struct dir *dir =dir_open_root();
  dir_add(dir,".",ROOT_DIR_SECTOR);
  dir_add(dir,"..",ROOT_DIR_SECTOR);
  dir_close(dir);
  
  free_map_close ();
  printf ("done.\n");
}

//added4 3-3
//parsing path and save file name at char file
struct dir * parsing_path(const char *path, char *file){
  //validity check of path and file string  
  if(path==NULL|| file==NULL)
    return NULL;
  if(strlen(path)==0)
    return NULL;
  
  struct dir *dir;
  char path_tmp[PATH_MAX];
  strlcpy(path_tmp,path,PATH_MAX);

  //if abolute path
  if(path_tmp[0] == '/')
    dir = dir_open_root();
  else{
    dir = dir_reopen(thread_current()->cur_dir);
    //check if directory is removed(for dir-rm-cwd)
    struct inode *inode = dir_get_inode(dir);
    if(!is_directory(inode))
      return NULL;
    }

  char *token,*token2, *save_ptr;
  token = strtok_r(path_tmp,"/",&save_ptr);
  token2 = strtok_r(NULL,"/",&save_ptr);
  
  // case like cd / 
  if(token==NULL){
    strlcpy(file,".",2);
    return dir;
  }
  
  //after this loop token will be file name.
  while(token2){
    struct inode *inode; 
    //get directory
    //get inode & check if it is file
    if(!dir_lookup(dir,token,&inode)){
       dir_close(dir);
       return NULL;
    }
    else if(!is_directory(inode)){
       dir_close(dir);
       return NULL;
    } 

    dir_close(dir);
    dir = dir_open(inode);

    token = token2;
    token2 = strtok_r(NULL, "/", &save_ptr);
  } 
// //when file name is too long
//  if(strlen(token)+1>NAME_MAX){
//    dir_close(dir);
//    return NULL;
//  }
  strlcpy(file,token,PATH_MAX);
  return dir;
}



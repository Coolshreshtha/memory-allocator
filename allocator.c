#define _GNU_SOURCE

#include <assert.h>
#include <malloc.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

// The minimum size returned by malloc
#define MIN_MALLOC_SIZE 16

// Round a value x up to the next multiple of y
#define ROUND_UP(x, y) ((x) % (y) == 0 ? (x) : (x) + ((y) - (x) % (y)))

// The size of a single page of memory, in bytes
#define PAGE_SIZE 0x1000

#define MAGIC_NUMBER 0xA991E

size_t xxmalloc_usable_size(void* ptr);

// basic node
typedef struct node {
  struct node* next;
} node_t;

// node for the page headers
typedef struct header_node {
  int magic_number;
  size_t block_size;
} header_node_t;

// a singular free list for a specific size
typedef struct freelist {
  node_t* next;
} freelist_t;

// all of our freelists [0] = 16, [1] = 32, ..., [7] = 2048;
freelist_t* freelists[8] = {NULL};

// rounds to next power of 2
int roundPowers(int size) {
  if (size <= 16)
    return 16;
  else if (size <= 32)
    return 32;
  else if (size <= 64)
    return 64;
  else if (size <= 128)
    return 128;
  else if (size <= 256)
    return 256;
  else if (size <= 512)
    return 512;
  else if (size <= 1024)
    return 1024;
  else if (size <= 2048)
    return 2048;
  else {
    return -1;
  }
}

// returns appopriate free list index
int roundPowersList(int size) {
  return ((__builtin_ctzl(size))-4);
}

// initialize freelist
// returns pointer to that new list
// assume size is rounded
freelist_t* allocate_blocks(size_t size) {

  // returns base pointer for list
  void* blocked_page =
      mmap(NULL, PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED, -1, 0);

  // error checking
  if(blocked_page == (void*) -1) {
    perror("failed to allocate space");
    exit(EXIT_FAILURE);
  }

  // save first block as header and initalize
  header_node_t* page_header = (header_node_t*) blocked_page;
  page_header->magic_number = MAGIC_NUMBER;
  page_header->block_size =  size;

  // loop through rest of memory and assign pointers to freelist
  size_t offset = size;
  while(offset < PAGE_SIZE) {
    // make new node
    node_t* node = (node_t*) (blocked_page + offset);
    offset += size;
    node->next = (node_t*) (blocked_page + offset);
  }

  // set last to null
  node_t* node = (node_t*) (blocked_page + offset - size);
  node->next = NULL;

  return (freelist_t*) (blocked_page + size);

}

// "remove" a node from the free list of a given size
// return a pointer to the new list
freelist_t* removeFirst(freelist_t* lst) {
  return (freelist_t*) lst->next;
}


// A utility logging function that definitely does not call malloc or free
void log_message(char* message);

/**
 * Allocate space on the heap.
 * \param size  The minimium number of bytes that must be allocated
 * \returns     A pointer to the beginning of the allocated space.
 *              This function may return NULL when an error occurs.
 */
void* xxmalloc(size_t size) {

  // if size > 2048, request memory directly from mmap and do not
  // seperate into blocks
  // cannot be freed and results in memory leak
  if (size > 2048) {
    // Round the size up to the next multiple of the page size
    size = ROUND_UP(size, PAGE_SIZE);

    // Request memory from the operating system in page-sized chunks
    void* p = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);

    // Check for errors
    if (p == MAP_FAILED) {
      log_message("mmap failed! Giving up.\n");
      exit(2);
    }

    return p;
  } else // allocate using our freelists
  {
    // round size
    size = roundPowers(size);

    // find list
    int index = roundPowersList(size);

    // if lst is null, allocate new blocks
    if(freelists[index] == NULL)
      freelists[index] = allocate_blocks(size);

    // find and return first block
    freelist_t* first = freelists[index];
    freelists[index] = removeFirst(freelists[index]);
    return (void*) first;
  }
}

// "add" a node from the free list of a given size
// return a pointer to the new list
freelist_t* addFirst(freelist_t* lst, void* freeNode) {
  node_t* first = (node_t*) lst;

  freelist_t* new = (freelist_t*) freeNode;

  //freeNode.next point to the beginning of the list
  new->next=first;

  return new;
}

/**
 * Free space occupied by a heap object.
 * \param ptr   A pointer somewhere inside the object that is being freed
 */
void xxfree(void* ptr) {
  // Don't free NULL!
  if (ptr == NULL) return;

  // find out which list the pointer belongs to
  size_t size = xxmalloc_usable_size(ptr);

  // check that the magic number is valid
  if(size == -1) {
    perror("pointer not allocated using custom malloc");
    return;
  }

  // locate list
  int index = roundPowersList(size);

  // add ptr back to list
  freelists[index] = addFirst(freelists[index], ptr);

  return;

}

/**
 * Get the available size of an allocated object. This function should return the amount of space
 * that was actually allocated by malloc, not the amount that was requested.
 * \param ptr   A pointer somewhere inside the allocated object
 * \returns     The number of bytes available for use in this object
 */
size_t xxmalloc_usable_size(void* ptr) {
  // If ptr is NULL always return zero
  if (ptr == NULL) {
    return 0;
  }

  // Treat the freed pointer as an integer
  intptr_t free_address = (intptr_t) ptr;

  // Round down to the beginning of a page
  intptr_t page_start = free_address - (free_address % PAGE_SIZE);

  // Cast the page start address to a header struct
  header_node_t* header = (header_node_t*) page_start;

  if (header->magic_number != MAGIC_NUMBER) {
    return - 1;
  }

  return header->block_size;
}

/**
 * Print a message directly to standard error without invoking malloc or free.
 * \param message   A null-terminated string that contains the message to be printed
 */
void log_message(char* message) {
  // Get the message length
  size_t len = 0;
  while (message[len] != '\0') {
    len++;
  }

  // Write the message
  if (write(STDERR_FILENO, message, len) != len) {
    // Write failed. Try to write an error message, then exit
    char fail_msg[] = "logging failed\n";
    write(STDERR_FILENO, fail_msg, sizeof(fail_msg));
    exit(2);
  }
}

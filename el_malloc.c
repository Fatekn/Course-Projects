// el_malloc.c: implementation of explicit list malloc functions.

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include "el_malloc.h"

////////////////////////////////////////////////////////////////////////////////
// Global control functions

// Global control variable for the allocator. Must be initialized in
// el_init().
el_ctl_t *el_ctl = NULL;

// Create an initial block of memory for the heap using
// mmap(). Initialize the el_ctl data structure to point at this
// block. The initializ size/position of the heap for the memory map
// are given in the symbols EL_HEAP_INITIAL_SIZE and
// EL_HEAP_START_ADDRESS.  Initialize the lists in el_ctl to contain a
// single large block of available memory and no used blocks of
// memory.
int el_init(){
  el_ctl =
    mmap(EL_CTL_START_ADDRESS,
         EL_PAGE_BYTES,
         PROT_READ | PROT_WRITE,
         MAP_PRIVATE | MAP_ANONYMOUS,
         -1, 0);
  assert(el_ctl == EL_CTL_START_ADDRESS);

  void *heap = 
    mmap(EL_HEAP_START_ADDRESS,
         EL_HEAP_INITIAL_SIZE,
         PROT_READ | PROT_WRITE,
         MAP_PRIVATE | MAP_ANONYMOUS,
         -1, 0);
  assert(heap == EL_HEAP_START_ADDRESS);

  el_ctl->heap_bytes = EL_HEAP_INITIAL_SIZE; // make the heap as big as possible to begin with
  el_ctl->heap_start = heap;                 // set addresses of start and end of heap
  el_ctl->heap_end   = PTR_PLUS_BYTES(heap,el_ctl->heap_bytes);

  if(el_ctl->heap_bytes < EL_BLOCK_OVERHEAD){
    fprintf(stderr,"el_init: heap size %ld to small for a block overhead %ld\n",
            el_ctl->heap_bytes,EL_BLOCK_OVERHEAD);
    return 1;
  }
 
  el_init_blocklist(&el_ctl->avail_actual);
  el_init_blocklist(&el_ctl->used_actual);
  el_ctl->avail = &el_ctl->avail_actual;
  el_ctl->used  = &el_ctl->used_actual;

  // establish the first available block by filling in size in
  // block/foot and null links in head
  size_t size = el_ctl->heap_bytes - EL_BLOCK_OVERHEAD;
  el_blockhead_t *ablock = el_ctl->heap_start;
  ablock->size = size;
  ablock->state = EL_AVAILABLE;
  el_blockfoot_t *afoot = el_get_footer(ablock);
  afoot->size = size;

  // Add initial block to availble list; avoid use of list add
  // functions in case those are buggy which will screw up the heap
  // initialization
  ablock->prev = el_ctl->avail->beg;
  ablock->next = el_ctl->avail->beg->next;
  ablock->prev->next = ablock;
  ablock->next->prev = ablock;
  el_ctl->avail->length++;
  el_ctl->avail->bytes += (ablock->size + EL_BLOCK_OVERHEAD);

  return 0;
}

// Clean up the heap area associated with the system which unmaps all
// pages associated with the heap.
void el_cleanup(){
  munmap(el_ctl->heap_start, el_ctl->heap_bytes);
  munmap(el_ctl, EL_PAGE_BYTES);
}

////////////////////////////////////////////////////////////////////////////////
// Pointer arithmetic functions to access adjacent headers/footers

// Compute the address of the foot for the given head which is at a
// higher address than the head.
el_blockfoot_t *el_get_footer(el_blockhead_t *head){
  size_t size = head->size;
  el_blockfoot_t *foot = PTR_PLUS_BYTES(head, sizeof(el_blockhead_t) + size);
  return foot;
}

// REQUIRED
// Compute the address of the head for the given foot which is at a
// lower address than the foot.
el_blockhead_t *el_get_header(el_blockfoot_t *foot){
  size_t size = foot->size;
  el_blockhead_t *head = PTR_MINUS_BYTES(foot, size + sizeof(el_blockhead_t));
  return head;
}

// Return a pointer to the block that is one block higher in memory
// from the given block.  This should be the size of the block plus
// the EL_BLOCK_OVERHEAD which is the space occupied by the header and
// footer. Returns NULL if the block above would be off the heap.
// DOES NOT follow next pointer, looks in adjacent memory.
el_blockhead_t *el_block_above(el_blockhead_t *block){
  el_blockhead_t *higher =
    PTR_PLUS_BYTES(block, block->size + EL_BLOCK_OVERHEAD);
  if((void *) higher >= (void*) el_ctl->heap_end){
    return NULL;
  }
  else{
    return higher;
  }
}

// REQUIRED
// Return a pointer to the block that is one block lower in memory
// from the given block.  Uses the size of the preceding block found
// in its foot. DOES NOT follow block->next pointer, looks in adjacent
// memory. Returns NULL if the block below would be outside the heap.
// 
// WARNING: This function must perform slightly different arithmetic
// than el_block_above(). Take care when implementing it.
el_blockhead_t *el_block_below(el_blockhead_t *block){
  el_blockfoot_t *temp = PTR_MINUS_BYTES(block, sizeof(el_blockfoot_t));

  if((void*) temp <= (void*) el_ctl->heap_start) {
    return NULL;
  } else {
    el_blockhead_t *lower = PTR_MINUS_BYTES(temp, temp->size + sizeof(el_blockhead_t));
    return lower;
  }
}

////////////////////////////////////////////////////////////////////////////////
// Block list operations

// Print an entire blocklist. The format appears as follows.
//
// {length:   2  bytes:  3400}
//   [  0] head @ 0x600000000000 {state: a  size:   128}
//   [  1] head @ 0x600000000360 {state: a  size:  3192}
//
// Note that the '@' column uses the actual address of items which
// relies on a consistent mmap() starting point for the heap.
void el_print_blocklist(el_blocklist_t *list){
  printf("{length: %3lu  bytes: %5lu}\n", list->length,list->bytes);
  el_blockhead_t *block = list->beg;
  for(int i=0; i<list->length; i++){
    printf("  ");
    block = block->next;
    printf("[%3d] head @ %p ", i, block);
    printf("{state: %c  size: %5lu}\n", block->state,block->size);
  }
}


// Print a single block during a sequential walk through the heap
void el_print_block(el_blockhead_t *block){
  el_blockfoot_t *foot = el_get_footer(block);
  printf("%p\n", block);
  printf("  state:      %c\n", block->state);
  printf("  size:       %lu (total: 0x%lx)\n", block->size, block->size+EL_BLOCK_OVERHEAD);
  printf("  prev:       %p\n", block->prev);
  printf("  next:       %p\n", block->next);
  printf("  user:       %p\n", PTR_PLUS_BYTES(block,sizeof(el_blockhead_t)));
  printf("  foot:       %p\n", foot);
  printf("  foot->size: %lu\n", foot->size);
}

// Print out stats on the heap for use in debugging. Shows the
// available and used list along with a linear walk through the heap
// blocks.
void el_print_stats(){
  printf("HEAP STATS (overhead per node: %lu)\n",EL_BLOCK_OVERHEAD);
  printf("heap_start:  %p\n",el_ctl->heap_start); 
  printf("heap_end:    %p\n",el_ctl->heap_end); 
  printf("total_bytes: %lu\n",el_ctl->heap_bytes);
  printf("AVAILABLE LIST: ");
  el_print_blocklist(el_ctl->avail);
  printf("USED LIST: ");
  el_print_blocklist(el_ctl->used);
  printf("HEAP BLOCKS:\n");
  int i = 0;
  el_blockhead_t *cur = el_ctl->heap_start;
  while(cur != NULL){
    printf("[%3d] @ ",i);
    el_print_block(cur);
    cur = el_block_above(cur);
    i++;
  }
}

// Initialize the specified list to be empty. Sets the beg/end
// pointers to the actual space and initializes those data to be the
// ends of the list.  Initializes length and size to 0.
void el_init_blocklist(el_blocklist_t *list){
  list->beg        = &(list->beg_actual); 
  list->beg->state = EL_BEGIN_BLOCK;
  list->beg->size  = EL_UNINITIALIZED;
  list->end        = &(list->end_actual); 
  list->end->state = EL_END_BLOCK;
  list->end->size  = EL_UNINITIALIZED;
  list->beg->next  = list->end;
  list->beg->prev  = NULL;
  list->end->next  = NULL;
  list->end->prev  = list->beg;
  list->length     = 0;
  list->bytes      = 0;
}  

// REQUIRED
// Add to the front of list; links for block are adjusted as are links
// within list.  Length is incremented and the bytes for the list are
// updated to include the new block's size and its overhead.
void el_add_block_front(el_blocklist_t *list, el_blockhead_t *block){
  block->prev = list->beg;
  block->next = list->beg->next;

  block->prev->next = block;
  block->next->prev = block;

  list->length++;
  list->bytes += block->size + EL_BLOCK_OVERHEAD;
  return;
}

// REQUIRED
// Unlink block from the list it is in which should be the list
// parameter.  Updates the length and bytes for that list including
// the EL_BLOCK_OVERHEAD bytes associated with header/footer.
void el_remove_block(el_blocklist_t *list, el_blockhead_t *block){
  block->prev->next = block->next;
  block->next->prev = block->prev;

  list->length--;
  list->bytes -= (block->size + EL_BLOCK_OVERHEAD);
  return;
}

////////////////////////////////////////////////////////////////////////////////
// Allocation-related functions

// REQUIRED
// Find the first block in the available list with block size of at
// least `size`.  Returns a pointer to the found block or NULL if no
// block of sufficient size is available.
el_blockhead_t *el_find_first_avail(size_t size){
  el_blockhead_t *curr = el_ctl->avail->beg;
  while(curr->state != EL_END_BLOCK) {
    if(curr->size >= size) {
      return curr;
    }
    curr = curr->next;
  }
  return NULL;
}

// REQUIRED
// Set the pointed to block to the given size and add a footer to
// it. Creates another block above it by creating a new header and
// assigning it the remaining space. Ensures that the new block has a
// footer with the correct size. Returns a pointer to the newly
// created block while the parameter block has its size altered to
// parameter size. Does not do any linking of blocks.  If the
// parameter block does not have sufficient size for a split (at least
// new_size + EL_BLOCK_OVERHEAD for the new header/footer) makes no
// changes tot the block and returns NULL indicating no new block was
// created.
el_blockhead_t *el_split_block(el_blockhead_t *block, size_t new_size){
  if(block->size < new_size + EL_BLOCK_OVERHEAD) {
    return NULL;
  }

  el_blockfoot_t *oldfoot = el_get_footer(block);
  size_t oldsize = block->size;

  block->size = new_size;
  el_blockfoot_t *newfoot = el_get_footer(block);			//create new footer for original block
  newfoot->size = new_size;

  el_blockhead_t* new_block = el_block_above(block);			//create new header for new block
  new_block->size = oldsize - new_size - EL_BLOCK_OVERHEAD;
  
  oldfoot->size = new_block->size;
  
  return new_block;
}

// REQUIRED
// Return pointer to a block of memory with at least the given size
// for use by the user.  The pointer returned is to the usable space,
// not the block header. Makes use of find_first_avail() to find a
// suitable block and el_split_block() to split it.  Returns NULL if
// no space is available.
void *el_malloc(size_t nbytes){
  el_blockhead_t* block = el_find_first_avail(nbytes);

  if(block == NULL) {						//check if block was found
    return NULL;
  }
  
  el_remove_block(el_ctl->avail, block);			//found block, remove from available

  el_blockhead_t* newblock = el_split_block(block, nbytes);	//split block
  
  if(newblock != NULL) {					//if newblock was created, make it available
    el_add_block_front(el_ctl->avail, newblock);
    newblock->state = EL_AVAILABLE;
  }

  el_add_block_front(el_ctl->used, block);
  block->state = EL_USED;
  
  return PTR_PLUS_BYTES(block, sizeof(el_blockhead_t));		//return pointer to memory of block
}

////////////////////////////////////////////////////////////////////////////////
// De-allocation/free() related functions

// REQUIRED
// Attempt to merge the block lower with the next block in
// memory. Does nothing if lower is null or not EL_AVAILABLE and does
// nothing if the next higher block is null (because lower is the last
// block) or not EL_AVAILABLE.  Otherwise, locates the next block with
// el_block_above() and merges these two into a single block. Adjusts
// the fields of lower to incorporate the size of higher block and the
// reclaimed overhead. Adjusts footer of higher to indicate the two
// blocks are merged.  Removes both lower and higher from the
// available list and re-adds lower to the front of the available
// list.
void el_merge_block_with_above(el_blockhead_t *lower){
  if(lower == NULL || lower->state != EL_AVAILABLE) {
    return;
  }

  el_blockhead_t* above = el_block_above(lower);
  if(above == NULL || above->state != EL_AVAILABLE) {
    return;
  }

  el_remove_block(el_ctl->avail, lower);			//remove both from available before updating
  el_remove_block(el_ctl->avail, above);

  lower->size += above->size + EL_BLOCK_OVERHEAD;		//update sizes
  el_get_footer(lower)->size = lower->size;
  el_add_block_front(el_ctl->avail, lower);			//add combined block back to avail
  return;
}

// REQUIRED
// Free the block pointed to by the give ptr.  The area immediately
// preceding the pointer should contain an el_blockhead_t with information
// on the block size. Attempts to merge the free'd block with adjacent
// blocks using el_merge_block_with_above().
void el_free(void *ptr){
  el_blockhead_t* block = PTR_MINUS_BYTES(ptr, sizeof(el_blockhead_t));

  if(block->state != EL_USED) {
    return;
  }
  el_remove_block(el_ctl->used, block);
  block->state = EL_AVAILABLE;
  el_add_block_front(el_ctl->avail, block);
  
  el_merge_block_with_above(block);
  el_merge_block_with_above(el_block_below(block));
  
  return;
}

////////////////////////////////////////////////////////////////////////////////
// HEAP EXPANSION FUNCTIONS

// REQUIRED
// Attempts to append pages of memory to the heap with mmap(). npages
// is how many pages are to be appended with total bytes to be
// appended as npages * EL_PAGE_BYTES. Calls mmap() with similar
// arguments to those used in el_init() however requests the address
// of the pages to be at heap_end so that the heap grows
// contiguously. If this fails, prints the message
// 
//  ERROR: Unable to mmap() additional 3 pages
// 
// and returns 1. Note that mmap() returns the constant MAP_FAILED on
// errors and the returned address will not match the requested
// virtual address on failures.
//
// Otherwise, adjusts heap size and end for the expanded heap. Creates
// a new block for the freshly allocated pages that is added to the
// available list. Also attempts to merge this block with the block
// below it. Returns 0 on success.
int el_append_pages_to_heap(int npages){
  void *newheap =
    mmap(el_ctl->heap_end,
         npages * EL_PAGE_BYTES,
         PROT_READ | PROT_WRITE,
         MAP_PRIVATE | MAP_ANONYMOUS,
         -1, 0);

  if(newheap == MAP_FAILED || newheap != el_ctl->heap_end) {			//check if map failed or different address used, error
    printf("ERROR: Unable to mmap() additional %d pages\n", npages);
    return 1;
  }

  el_ctl->heap_bytes += npages * EL_PAGE_BYTES;

  el_blockhead_t *newblock = el_ctl->heap_end;					//create new head at old heap end before updating

  el_ctl->heap_end = PTR_PLUS_BYTES(el_ctl->heap_start,el_ctl->heap_bytes);

  size_t size = (npages * EL_PAGE_BYTES) - EL_BLOCK_OVERHEAD;			//initialize size, state, and footer size for new block
  newblock->size = size;
  newblock->state = EL_AVAILABLE;
  el_blockfoot_t *newfoot = el_get_footer(newblock);
  newfoot->size = size;

  // Add initial block to available list
  el_add_block_front(el_ctl->avail, newblock);

  el_merge_block_with_above(el_block_below(newblock));				//try merging
  return 0;  
}

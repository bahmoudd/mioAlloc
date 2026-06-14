# mioMalloc

I chose the name `mioMalloc` because `mio` stands for `My Implementation Of`.

`mmap()` is used over `brk()` and `sbrk()` because the latter functions are naive and has some issues which can be seen [here](https://stackoverflow.com/questions/68123943/advantages-of-mmap-over-sbrk)

I used a doubly-linked list over a singly-linked list simply to make writing some functions easier.

## An explanation of each function

mioMalloc works by first initialising a free list to `NULL`.

It then uses two structs: `Block` and `HeapMetadata`. `Block` contains metadata for each initialised block so that they can be worked with. `HeapMetadata` contains metadata for the entire heap, so that each function knows where the first block is and how many blocks there are within the heap.

### The structs

This isn't really a function but it's important to explain the structs so that you have an idea of how each function work.

The `Block` structs takes the following attributes:
- `heapMetadata` points to the heap metadata so it is accessible once blocks are returned from functions.
- `prev` and `next` point to the previous and next blocks respectively, creating a doubly-linked list. This makes allocating and freeing memory a lot easier later on.
- `start` is the start of the block.
- `end` is the end of the block. I included a `start` and `end` to make splitting blocks much easier. There is no `size` attribute because size is just `end` - `start`. I felt that including `size` would be redundant.
- `isLarge` is a boolean which shows if a memory block exceeds the page size or not. The necessity for this will be explained later on in the readme.

The `HeapMetadata` takes the following attributes:
- `blockCount` is the amount of blocks in the heap.
- `firstBlock` is a pointer to the first block in the heap.

### `initHeap(void)`

Like the name suggests, this function initialises the heap and free list so that memory can be dynamically allocated.

It works as follows:
1) It requests memory from the OS. This depends on the page size, which depends on your CPU architecture and OS. If this fails, `initHeap()` returns `NULL`.
2) The first few bytes of the raw memory become heap metadata.
3) The first block is placed after the heap metadata, and takes up the rest of the raw memory.
4) The block is initialised with all the necessary values, and so is the heap metadata.
5) Then, the free list is initialised to the first block in the heap.
6) The start of the first block is then returned from initHeap().

### `void* mioMalloc(size_t requestedSize)`

This function allocates memory from the heap and returns it to the programmer. It works as follows:

1) It checks if the free list has been initialised. If not, it returns `NULL`.
2) It aligns the requested size to a multiple of 16 to avoid edge cases
3) If the rounded requested size (RRS) + size of the heap metadata exceeds the page size, it calls `largeMalloc()` which returns memory requested directly from the OS. This is slow but hardly happens anyway so overhead here is acceptable. `free()` needs some way to distinguish between whether a memory block was created from `mioMalloc()` or `largeMalloc()` hence the need for the `isLarge` attribute for the `Block` struct.
4) It fetches the heap metadata and stores it within a variable to be accessed later.
5) Then, it traverses the free list for any free blocks which have a size that is greater than or equal to the RRS.
6) If no such free block exists, it returns NULL. If not, the blocks is split into two - one which has a size of the RRS, and the remainder.
7) The remainder is initialised, marked free and the start of the carved-out memory block is returned from the function.

### `void mioFree(void* pointer)`

1) Like glibc's `free()`, if a `NULL` pointer is passed in, the function simply returns.
2) The pointer is converted back into a `Block` pointer and if `isLarge` is set to `true`, it returns the memory back to the OS using `munmap()`
3) The block is marked as free and coalesced with its neighbours if they are free
4) The block is then added to the free list

### `void* mioCalloc(size_t requestedSize)`

Implementing this function is trivial once you have `malloc` and `free` implemented. You simply `malloc` the requested size, initialise it to all zeroes and return the initialised memory.

### `void* mioRealloc(void* pointer, size_t requestedSize)`

1) If the pointer is `NULL`, it simply calls `malloc` with the requested size
2) If `requestedSize` is 0, it frees the pointer
3) The requested size is aligned to a multiple of 16 and converts the `void*` pointer back into a `Block` pointer
4) If the RRS is less than the size of the memory block, it shrinks it in-place
5) If the RRS is greater than the size of the memory block, it attempts to grow it in-place
6) Otherwise, it calls `mioMalloc`, creating a new memory block and copies the contents of the old pointer to the new one
7) The processed pointer is returned from the function to the programmer.

### `size_t getMioPointerSize(void* pointer)`

This converts the `void*` pointer to a `Block*` pointer and returns the size by subtracting the end of the pointer to the start of it.

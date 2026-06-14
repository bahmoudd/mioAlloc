#ifndef MIOALLOC_H_
#define MIOALLOC_H_

#include <stddef.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stddef.h>

#define PAGE_SIZE sysconf(_SC_PAGESIZE)
#define roundToMul16(number) (number + 15) & ~15
#define MIN_BLOCK_SIZE 16

typedef struct Block {
    struct HeapMetadata *heapMetadata;
    struct Block *prev;
    struct Block *next;
    size_t size;
    void *start;
    void *end;
    bool inUse;
    bool isLarge;
} Block;

typedef struct HeapMetadata {
    size_t blockCount;
    Block* firstBlock;
} HeapMetadata;

Block* initHeap();
static Block *splitBlock(Block *blockToResize, size_t newSize); 
static void mergeWithNext(Block* block);
static void *largeMalloc(size_t requestedSize);
static void insertIntoFreeList(Block* block);
static void largeFree(void *pointer);
void *mioMalloc(size_t requestedSize);
void mioFree(void *pointer);
void *mioCalloc(size_t num, size_t requestedSize);
void *mioRealloc(void *pointer, size_t requestedSize);
size_t getMioPointerSize(void *pointer);
void mioPrintInfo(void *pointer);

#endif
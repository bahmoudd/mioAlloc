#include <sys/mman.h>
#include <unistd.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stddef.h>
#include <stdio.h>
#include "./mioAlloc.h"

static Block *freeList = NULL;

Block *initHeap() {
    void *rawMemory = mmap(NULL,
                           PAGE_SIZE,
                           PROT_READ | PROT_WRITE,
                           MAP_PRIVATE | MAP_ANONYMOUS,
                           -1, 0);

    if (rawMemory == MAP_FAILED)   
        return NULL;

    HeapMetadata *heapMetadata = (HeapMetadata*) rawMemory;
    Block *firstBlock = (Block*) ((uint8_t*) rawMemory + sizeof(HeapMetadata));
    void *heapEnd = (uint8_t*) rawMemory + PAGE_SIZE;

    firstBlock->start = (uint8_t*)firstBlock + sizeof(Block);
    firstBlock->end = heapEnd;
    firstBlock->size = heapEnd - firstBlock->start;
    firstBlock->inUse = false;
    firstBlock->heapMetadata = heapMetadata;
    firstBlock->next = NULL;
    firstBlock->prev = NULL;
    firstBlock->isLarge = false;

    heapMetadata->firstBlock = firstBlock;
    heapMetadata->blockCount = 1;

    freeList = firstBlock;

    return firstBlock;
}

static Block *splitBlock(Block *blockToResize, size_t newSize) {
    if (blockToResize == NULL)
        return NULL;

    size_t blockSize = (uint8_t*) blockToResize->end - (uint8_t*) blockToResize->start;
    size_t remaining = blockSize - newSize;

    if (remaining < sizeof(Block) + MIN_BLOCK_SIZE) 
        return blockToResize;

    void *allocatedEnd = (uint8_t*) blockToResize->start + newSize;
    Block *newBlock = (Block*) allocatedEnd;

    newBlock->start = (uint8_t*)newBlock + sizeof(Block);
    newBlock->end = allocatedEnd;
    newBlock->inUse = false;
    newBlock->prev = blockToResize;
    newBlock->next = blockToResize->next;
    newBlock->size = newBlock->end - newBlock->start;
    newBlock->isLarge = false;

    if (blockToResize->next)
        blockToResize->next->prev = newBlock;

    blockToResize->end = newBlock;
    blockToResize->inUse = false;
    blockToResize->size = blockToResize->end - blockToResize->start;

    return newBlock;
}

static void mergeWithNext(Block* block) {
    if (!block || !block->next)
        return;

    Block *next = block->next;

    if (block->inUse || next->inUse) {
        return;
    }

    block->end = next->end;
    block->next = next->next;

    if (next->next)
        next->next->prev = block;
}

static void *largeMalloc(size_t requestedSize) {
    size_t roundedSize = roundToMul16(requestedSize);
    void *rawMemory = mmap(NULL,
                           requestedSize + sizeof(Block),
                           PROT_WRITE | PROT_READ,
                           MAP_PRIVATE | MAP_ANONYMOUS,
                           -1, 0);
    
    Block *block = (Block*)rawMemory;
    
    block->start = (uint8_t*)block + sizeof(Block);
    block->end = (uint8_t*)rawMemory + requestedSize + sizeof(Block);
    block->inUse = true;
    block->isLarge = true;
    block->size = block->end - block->start;
    
    return block->start;
}

static void insertIntoFreeList(Block* block) {
    block->inUse = false;

    Block* cur = freeList;
    Block* prev = NULL;

    while (cur && cur < block) {
        prev = cur;
        cur = cur->next;
    }

    block->next = cur;
    block->prev = prev;

    if (cur)
        cur->prev = block;

    if (prev)
        prev->next = block;
    else
        freeList = block;
}

static void largeFree(void *pointer) {
    Block *block = (Block*) ((uint8_t*) pointer - sizeof(Block));
    if (!block->isLarge)
        return;
    
    munmap(block, block->size);
}

void *mioMalloc(size_t requestedSize) {
    Block *currentBlock = freeList;
    
    if (currentBlock == NULL)
        return NULL;
    
    size_t roundedSize = roundToMul16(requestedSize);

    if (requestedSize + sizeof(Block) > PAGE_SIZE)
        return largeMalloc(requestedSize);

    HeapMetadata *heapMetadata = currentBlock->heapMetadata;
    while (currentBlock) {
        if (currentBlock->size < roundedSize) {
            currentBlock = currentBlock->next;
        } else {
            break;
        }
    }
    
    if (currentBlock == NULL)
        return NULL;
        
    Block* remainder = splitBlock(currentBlock, roundedSize);
    
    if (currentBlock->prev)
        currentBlock->prev->next = currentBlock->next;
    else
        freeList = currentBlock->next;

    if (currentBlock->next)
        currentBlock->next->prev = currentBlock->prev;

    if (remainder) {
        remainder->next = freeList;
        if (freeList)
            freeList->prev = remainder;
        freeList = remainder;
    }

    return currentBlock->start;
}

void mioFree(void *pointer) {
    if (pointer == NULL)
        return;

    Block *block = (Block*)((uint8_t*)pointer - sizeof(Block));

    if (block->isLarge) {
        largeFree(pointer);
        return;
    }

    block->inUse = false;

    if (block->next && block->next->inUse == false) {
        Block *next = block->next;
        block->end = next->end;
        block->next = next->next;

        if (next->next)
            next->next->prev = block;
    }

    if (block->prev && block->prev->inUse == false) {
        Block *prev = block->prev;
        block->start = prev->start;
        block->prev = prev->prev;

        if (prev->prev)
            prev->prev->next = block;
    }

    block->next = freeList;
    if (freeList)
        freeList->prev = block;

    freeList = block;
}

void *mioCalloc(size_t num, size_t requestedSize) {
    if (num != 0 && requestedSize > SIZE_MAX / num)
        return NULL;

    void *ptr = mioMalloc(num * requestedSize);
    if (ptr == NULL)
        return NULL;

    memset(ptr, 0, num * requestedSize);

    return ptr;
}

void *mioRealloc(void *pointer, size_t requestedSize) {
    if (pointer == NULL)
        return mioMalloc(requestedSize);

    if (requestedSize == 0) {
        mioFree(pointer);
        return NULL;
    }

    Block *block = (Block*)((uint8_t*)pointer - sizeof(Block));
    size_t oldSize = block->size;

    size_t newSize = roundToMul16(requestedSize);

    if (newSize <= oldSize) {
        Block *remainder = splitBlock(block, newSize);

        if (remainder) {
            remainder->inUse = false;
            insertIntoFreeList(remainder);
        }

        return pointer;
    }

    if (block->next && !block->next->inUse) {
        size_t combined = oldSize + block->next->size;
        
        if (combined >= newSize) {
            mergeWithNext(block);
            splitBlock(block, newSize);

            block->inUse = true;
            return block->start;
        }
    }

    void *newPointer = mioMalloc(requestedSize);
    if (!newPointer) return NULL;

    memcpy(newPointer, pointer, oldSize);
    mioFree(pointer);

    return newPointer;
}

size_t getMioPointerSize(void *pointer) {
    Block *block = (Block*) ((uint8_t*)pointer - sizeof(Block));

    return block->size;
}

size_t getMioBlockCount(void *pointer) {
    Block *block = (Block*) ((uint8_t*)pointer - sizeof(Block));

    return block->heapMetadata->blockCount;
}
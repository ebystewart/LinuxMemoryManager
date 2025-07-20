#ifndef MM_H_
#define MM_H_

#include <stdint.h>
#include "glueThread/glthread.h"

#define MM_MAX_STRUCT_NAME  32U
#define MIN_BLOCK_DATA_SIZE 20U

typedef enum{
    MM_FALSE,
    MM_TRUE
}vm_bool_t;

typedef struct block_meta_data_{
    vm_bool_t is_free;
    uint32_t block_size;
    uint32_t offset; /* offset from the strt of the page */
    struct block_meta_data_ *prev_block;
    struct block_meta_data_ *next_block;
    glthread_t priority_list_glue;
}block_meta_data_t;

GLTHREAD_TO_STRUCT(glue_to_block_metadata, block_meta_data_t, priority_list_glue);

/* Forward declaration */
struct vm_page_family_;

typedef struct vm_page_{
    struct vm_page_ *next;
    struct vm_page_ *prev;
    struct vm_page_family_ *page_family; /* Back pointer */
    block_meta_data_t block_meta_data;
    char page_memory[0];
}vm_page_t;

typedef struct vm_page_family_{
    char struct_name[MM_MAX_STRUCT_NAME];
    uint32_t struct_size;
    vm_page_t *first_page;
    glthread_t free_block_priority_list_head;
}vm_page_family_t;

typedef struct vm_page_for_families_{
    struct vm_page_for_families_ *next;
    vm_page_family_t vm_page_family[0];
}vm_page_for_families_t;

#define MAX_FAMILIES_PER_VM_PAGE \
        ((SYSTEM_PAGE_SIZE - sizeof(vm_page_for_families_t *))/sizeof(vm_page_family_t))

#define SYSTEM_USABLE_PAGE_SIZE \
        (SYSTEM_PAGE_SIZE - sizeof(vm_page_for_families_t *))

#define offset_of(container_structure, field_name) \
    ((size_t)&(((container_structure *)0)->field_name))

#define MM_GET_PAGE_FROM_META_BLOCK(block_meta_data_ptr) \
    ((void *)((char *)block_meta_data_ptr - block_meta_data__ptr->offset))

#define NEXT_META_BLOCK(block_meta_data_ptr) \
    (block_meta_data_ptr->next_block)

#define NEXT_META_BLOCK_BY_SIZE(block_meta_data_ptr) \
    (block_meta_data_t *)((((char *)block_meta_data_ptr) + \
     (block_meta_data_ptr->block_size + sizeof(block_meta_data_t))))
    //(block_meta_data_ptr + 1 + (block_meta_data_t *)(block_meta_data_ptr->block_size))

#define PREV_META_BLOCK(block_meta_data_ptr) \
    (block_meta_data_ptr->prev_block)

#define mm_bind_split_blocks_after_allocation(allocated_meta_block, free_meta_block)    \
    free_meta_block->next_block = allocated_meta_block->next_block;                     \
    free_meta_block->prev_block = allocated_meta_block;                                 \
    allocated_meta_block->next_block = free_meta_block;                                 \
    if(free_meta_block->next_block)                                                     \
        free_meta_block->next_block->prev_block = free_meta_block;
        
#define MARK_VM_PAGE_EMPTY(vm_page_ptr)                 \
    vm_page_ptr->block_meta_data.prev_block = NULL;     \
    vm_page_ptr->block_meta_data.next_block = NULL;     \
    vm_page_ptr->block_meta_data.is_free = MM_TRUE;

/* size excluding the vm_page_t structure, including the first meta block */
#define VM_PAGE_USABLE_SIZE \
    (SYSTEM_PAGE_SIZE - sizeof(vm_page_t) + sizeof(block_meta_data_t))

#define ITERATE_PAGE_FAMILIES_BEGIN(vm_page_for_families_ptr, curr)                     \
{                                                                                       \
    uint32_t idx = 0U;                                                                  \
    for (curr = (vm_page_family_t *)&vm_page_for_families_ptr->vm_page_family[idx];     \
        ((idx < MAX_FAMILIES_PER_VM_PAGE) && (curr->struct_size > 0U));                 \
        idx++,curr++)                                                                   \
    {                                                                                   \

#define ITERATE_PAGE_FAMILIES_END(vm_page_for_families_ptr, curr) }}

#define ITERATE_META_BLOCKS_BEGIN(first_meta_block, curr_meta_block)                    \
{                                                                                       \
    for(curr_meta_block = first_meta_block;                                             \
        curr_meta_block < (first_meta_block + (block_meta_data_t *)VM_PAGE_USABLE_SIZE);   \
        curr_meta_block += (block_meta_data_t *)(curr_meta_block->block_size + sizeof(block_meta_data_t)))   \
        {                                                                               \

#define ITERATE_META_BLOCKS_END(first_meta_block, curr_meta_block) }}


#define ITERATE_VM_PAGE_BEGIN(vm_page_family_ptr, curr_vm_page)     \
{                                                                   \
    curr_vm_page = (vm_page_t *)vm_page_family_ptr->first_page;     \
    for(; curr_vm_page; curr_vm_page = curr_vm_page->next)          \
    {                                                               

#define ITERATE_VM_PAGE_END(vm_page_family_ptr, curr_vm_page) }}    

#define ITERATE_VM_PAGE_ALL_BLOCKS_BEGIN(vm_page_ptr, curr_block_meta_data)                             \
{                                                                                                       \
    block_meta_data_t *first_meta_block = NULL;                                                         \
    curr_block_meta_data = &vm_page_ptr->block_meta_data;                                               \
    first_meta_block = curr_block_meta_data;                                                            \
    for(; (curr_block_meta_data &&                                                                      \
        (curr_block_meta_data < (first_meta_block + (block_meta_data_t *)VM_PAGE_USABLE_SIZE)));        \
        (curr_block_meta_data += (1 + (block_meta_data_t *)curr_block_meta_data->block_size)))          \
    {                                                                       

#define ITERATE_VM_PAGE_ALL_BLOCKS_END(vm_page_ptr, curr_block_meta_data) }}

/* Function Prototypes */
vm_page_family_t *mm_lookup_page_family_by_name(char *struct_name);

vm_bool_t mm_is_vm_page_empty(vm_page_t *vm_page);

vm_page_t *mm_allocate_vm_page(vm_page_family_t *vm_page_family);

void mm_vm_page_delete_and_free(vm_page_t *vm_page);

#endif
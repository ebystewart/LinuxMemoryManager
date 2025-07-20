#include <stdio.h>
#include <memory.h>
#include <unistd.h> /* to get page size using getpagesize()*/
#include <assert.h>
#include <sys/mman.h> /* for mmap()*/
#include "mm.h"
#include "uapi_mm.h"

static size_t SYSTEM_PAGE_SIZE = 0U;
static vm_page_for_families_t *first_vm_page_for_families = NULL;

/* Function to request VM page from kernel */
static void *mm_get_new_vm_page_from_kernel(int units)
{
    char *vm_page = mmap(0, units * SYSTEM_PAGE_SIZE, PROT_READ | PROT_WRITE | PROT_EXEC,
                            MAP_ANON | MAP_PRIVATE, 0, 0);
    if(vm_page == MAP_FAILED){
        printf("Error: VM Page allocation failed\n");
        return NULL;
    }
    memset(vm_page, 0, units * SYSTEM_PAGE_SIZE);
    return (void *)vm_page;
}

/* Function to return VM page back to kernel */
static void mm_return_vm_page_to_kernel(void *vm_page, int units)
{
    if(munmap(vm_page, units * SYSTEM_PAGE_SIZE)){
        printf("Error: could not unmap VM page back to kernel\n");
    }
}

static void mm_union_free_blocks(block_meta_data_t *first, block_meta_data_t *second)
{
    assert(first->is_free == MM_TRUE && second->is_free == MM_TRUE);
    first->block_size += (sizeof(block_meta_data_t) + second->block_size);
    first->next_block = second->next_block;
    if(first->next_block)
        first->next_block->prev_block = first;
}

/* assumption is, when contiguous pages (Giant VM pages) are allocated in which only the 
 * first vm page has the vm page meta data. for example for 2 contiguous pages, size would be 
 * (4096 * 2) - size of vm page meta data = 4044 + 4096
*/
static inline uint32_t mm_max_page_allocatable_memory(int units){

    return (uint32_t)((SYSTEM_PAGE_SIZE * units) - offset_of(vm_page_t, page_memory));
}

static int free_blocks_comparison_function(void *_block_metadata1, void *_block_metadata2)
{
    block_meta_data_t *block_metadata1 = (block_meta_data_t *)_block_metadata1;
    block_meta_data_t *block_metadata2 = (block_meta_data_t *)_block_metadata2;

    if(block_metadata1->block_size < block_metadata2->block_size)
        return 1;
    if(block_metadata1->block_size > block_metadata2->block_size)
        return -1;

    return 0;      
}

static void mm_add_free_block_meta_data_to_free_block_list(vm_page_family_t *vm_page_family, 
    block_meta_data_t *free_block){

    assert(free_block->is_free == MM_TRUE);
    glthread_priority_insert(&vm_page_family->free_block_priority_list_head,
                            &free_block->priority_list_glue,
                            free_blocks_comparison_function,
                            offset_of(block_meta_data_t, priority_list_glue));
    
}

/* get the first element in the priority queue */
static inline block_meta_data_t *mm_get_biggest_free_block_page_family(vm_page_family_t *vm_page_family)
{
    glthread_t *head =  &vm_page_family->free_block_priority_list_head;
    if(head->right)
        return glue_to_block_metadata(head->right);
    
    return NULL;
}

/* Global Function definitions */
void mm_init(void)
{
    SYSTEM_PAGE_SIZE = getpagesize();
}

void mm_instantiate_new_page_family(char *struct_name, uint32_t struct_size)
{
    vm_page_family_t *vm_page_family_curr = NULL;
    vm_page_for_families_t *new_vm_page_for_families = NULL;
    uint32_t count = 0U;

    if(struct_size > SYSTEM_USABLE_PAGE_SIZE){
        printf("Error: %s() - Size of structure %s exceeds system page size\n", __FUNCTION__, struct_name);
        return;
    }

    /* First time creation of new page family */
    if(!first_vm_page_for_families){
        first_vm_page_for_families = (vm_page_for_families_t *)mm_get_new_vm_page_from_kernel(1);
        first_vm_page_for_families->next = NULL;
        vm_page_family_curr  = (vm_page_family_t *)&first_vm_page_for_families->vm_page_family[0];
        strncpy(vm_page_family_curr->struct_name, struct_name, MM_MAX_STRUCT_NAME);
        vm_page_family_curr->struct_size = struct_size;
        vm_page_family_curr->first_page = NULL;
        init_glthread(&vm_page_family_curr->free_block_priority_list_head);
        return;
    }

    /* Iterate over the family page and look if space is available */
    ITERATE_PAGE_FAMILIES_BEGIN(first_vm_page_for_families, vm_page_family_curr){
        /* It should be possible to allocate memory for same structure multiple times */
        //if(strncmp(vm_page_family_curr->struct_name, struct_name, MM_MAX_STRUCT_NAME) == 0U){
            //assert(0);
        //}
        count++;
    }ITERATE_PAGE_FAMILIES_END(first_vm_page_for_families, vm_page_family_curr);

    /* If no space is available, create a new vm page family */
    if(count == MAX_FAMILIES_PER_VM_PAGE){

        new_vm_page_for_families = (vm_page_for_families_t *)mm_get_new_vm_page_from_kernel(1);
        new_vm_page_for_families->next = first_vm_page_for_families;
        first_vm_page_for_families = new_vm_page_for_families;
        vm_page_family_curr  = (vm_page_family_t *)&first_vm_page_for_families->vm_page_family[0];

    }
    /* Now a vm page family pointer would be available either from the old page family or a new page family */
    /* copy the entries to it */
    strncpy(vm_page_family_curr->struct_name, struct_name, MM_MAX_STRUCT_NAME);
    vm_page_family_curr->struct_size = struct_size;
    vm_page_family_curr->first_page = NULL;
    init_glthread(&vm_page_family_curr->free_block_priority_list_head);
}

vm_page_family_t *mm_lookup_page_family_by_name(char *struct_name)
{
    uint32_t count = 0U;
    vm_page_family_t *vm_page_family_curr = NULL;
    vm_page_for_families_t *curr_vm_page_for_families = NULL;


    if(!first_vm_page_for_families){
        printf("Error: No Page family exists\n");
        return NULL;
    }
    curr_vm_page_for_families = first_vm_page_for_families;

    do{
        ITERATE_PAGE_FAMILIES_BEGIN(curr_vm_page_for_families, vm_page_family_curr)
        {
            if (vm_page_family_curr->struct_size > 0U)
            {
                if(strncmp(vm_page_family_curr->struct_name, struct_name, MM_MAX_STRUCT_NAME) == 0U)
                {
                    return vm_page_family_curr;
                }
                count++;
            }
        }
        ITERATE_PAGE_FAMILIES_END(curr_vm_page_for_families, vm_page_family_curr);

        if(count == MAX_FAMILIES_PER_VM_PAGE){
            if(!curr_vm_page_for_families->next)
                return NULL;
            curr_vm_page_for_families = curr_vm_page_for_families->next;
            count = 0;
        }
        else{
            curr_vm_page_for_families = curr_vm_page_for_families->next;
        }
    }while(curr_vm_page_for_families != NULL); 
}

void mm_print_registered_page_families(void)
{
    uint32_t count = 0U;
    vm_page_family_t *vm_page_family_curr = NULL;
    vm_page_for_families_t *curr_vm_page_for_families = NULL;


    if(!first_vm_page_for_families){
        printf("Error: No entries to print\n");
        return;
    }
    curr_vm_page_for_families = first_vm_page_for_families;
    printf("Page Family List:\n");
    do{
        ITERATE_PAGE_FAMILIES_BEGIN(curr_vm_page_for_families, vm_page_family_curr)
        {
            if (vm_page_family_curr->struct_size > 0U)
            {
                printf("\t%d. Page Family: %s, Size = %d\n", count, vm_page_family_curr->struct_name,
                       vm_page_family_curr->struct_size);
                count++;
            }
        }
        ITERATE_PAGE_FAMILIES_END(curr_vm_page_for_families, vm_page_family_curr);
        
        if(count == MAX_FAMILIES_PER_VM_PAGE){
            if(!curr_vm_page_for_families->next)
                return;
            curr_vm_page_for_families = curr_vm_page_for_families->next;
            count = 0;
        }
        else{
            curr_vm_page_for_families = curr_vm_page_for_families->next;
        }
    }while(curr_vm_page_for_families != NULL);
}

vm_bool_t mm_is_vm_page_empty(vm_page_t *vm_page)
{
    if(vm_page->block_meta_data.prev_block == NULL &&
        vm_page->block_meta_data.next_block == NULL &&
        vm_page->block_meta_data.is_free == MM_TRUE){

        return MM_TRUE;
    }
    return MM_FALSE;
}

vm_page_t *mm_allocate_vm_page(vm_page_family_t *vm_page_family)
{
    vm_page_t *prev_first_page = NULL;
    vm_page_t *vm_page = mm_get_new_vm_page_from_kernel(1);

    MARK_VM_PAGE_EMPTY(vm_page);
    vm_page->block_meta_data.block_size = mm_max_page_allocatable_memory(1);
    vm_page->block_meta_data.offset = offset_of(vm_page_t, block_meta_data);
    vm_page->next = NULL;
    vm_page->prev = NULL;
    vm_page->page_family = vm_page_family;
    init_glthread(&vm_page->block_meta_data.priority_list_glue);

    if(!vm_page_family->first_page){
        vm_page_family->first_page = vm_page;
        return vm_page;
    }
    
    prev_first_page = vm_page_family->first_page;
    vm_page->next = prev_first_page;
    prev_first_page->prev = vm_page;
    vm_page_family->first_page = vm_page;
    return vm_page;
}

void mm_vm_page_delete_and_free(vm_page_t *vm_page)
{
    vm_page_family_t *vm_page_family = vm_page->page_family;

    if(vm_page_family->first_page == vm_page){

        vm_page_family->first_page = vm_page->next;
        if(vm_page->next)
            vm_page->next->prev = NULL;
        vm_page->next = NULL;
        vm_page->prev = NULL;
        vm_page->page_family = NULL;
        mm_return_vm_page_to_kernel((void *)vm_page, 1);
        return;
    }
    
    if(vm_page->next)
        vm_page->next->prev = vm_page->prev;
    if(vm_page->prev)
        vm_page->prev->next = vm_page->next;

    mm_return_vm_page_to_kernel((void *)vm_page, 1);
}



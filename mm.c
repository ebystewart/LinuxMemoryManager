#include <stdio.h>
#include <memory.h>
#include <unistd.h> /* to get page size using getpagesize()*/
#include <assert.h>
#include <sys/mman.h> /* for mmap()*/
#include "mm.h"
#include "uapi_mm.h"
#include "css.h"

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
    glthread_t *biggest_free_block_glue =  &vm_page_family->free_block_priority_list_head.right;
    if(biggest_free_block_glue)
        return glue_to_block_metadata(biggest_free_block_glue);
    
    return NULL;
}

static vm_page_t *mm_family_new_page_add(vm_page_family_t *vm_page_family)
{
    vm_page_t *vm_page = mm_allocate_vm_page(vm_page_family);
    if(!vm_page)
        return NULL;
    /* The new page is now, one full free block. Add it to the free block list */
    mm_add_free_block_meta_data_to_free_block_list(vm_page_family, &vm_page->block_meta_data);
    return vm_page;
}

static vm_bool_t mm_split_free_data_block_for_allocation(vm_page_family_t *vm_page_family, 
    block_meta_data_t *block_meta_data, uint32_t size){

    block_meta_data_t *next_block_meta_data = NULL;
    assert(block_meta_data->is_free == MM_TRUE);
    if(block_meta_data->block_size < size)
        return MM_FALSE;
    uint32_t remaining_size = block_meta_data->block_size - size;
    block_meta_data->is_free = MM_FALSE;
    block_meta_data->block_size = size;
    remove_glthread(&block_meta_data->priority_list_glue);
    /*block_meta_data->offset unchanged */

    /* Case #1: No split */
    if(!remaining_size)
        return MM_TRUE;

    /* Case #3: Partial Split: Soft Internal Fragmentation */
    /* enough space for next block's meta data is available 
     * but no space for application data.
    */
    else if((sizeof(block_meta_data_t) < remaining_size) &&
        ((sizeof(block_meta_data_t) + vm_page_family->struct_size) > remaining_size))
    {      
        next_block_meta_data = NEXT_META_BLOCK_BY_SIZE(block_meta_data);
        next_block_meta_data->is_free = MM_TRUE;
        next_block_meta_data->block_size = remaining_size - sizeof(block_meta_data_t);
        next_block_meta_data->offset = block_meta_data->offset + sizeof(block_meta_data_t) + block_meta_data->block_size;
        init_glthread(&next_block_meta_data->priority_list_glue);
        mm_add_free_block_meta_data_to_free_block_list(vm_page_family, next_block_meta_data);
        mm_bind_split_blocks_after_allocation(block_meta_data, next_block_meta_data);
    }
    /* Case #3: Partial Split: Hard Internal Fragmantation */
    else if(remaining_size < sizeof(block_meta_data_t)){
        /* No change in linkages required */
    }
    /* Case #2: Full Split */
    /* enough space for next block's meta data and application data is available 
    */
    else
    {      
        next_block_meta_data = NEXT_META_BLOCK_BY_SIZE(block_meta_data);
        next_block_meta_data->is_free = MM_TRUE;
        next_block_meta_data->block_size = remaining_size - sizeof(block_meta_data_t);
        next_block_meta_data->offset = block_meta_data->offset + sizeof(block_meta_data_t) + block_meta_data->block_size;
        init_glthread(&next_block_meta_data->priority_list_glue);
        mm_add_free_block_meta_data_to_free_block_list(vm_page_family, next_block_meta_data);
        mm_bind_split_blocks_after_allocation(block_meta_data, next_block_meta_data);
    }   
    return MM_TRUE;
}

static block_meta_data_t *mm_allocate_free_data_block(vm_page_family_t *vm_page_family, uint32_t req_size)
{
    vm_bool_t status = MM_FALSE;
    vm_page_t *vm_page = NULL;
    block_meta_data_t *biggest_block_meta_data =  mm_get_biggest_free_block_page_family(vm_page_family);

    if(!biggest_block_meta_data || biggest_block_meta_data->block_size < req_size){
        /* time to add a new page to meet the request */
        vm_page = mm_family_new_page_add(vm_page_family);
        printf("%s() - INFO: vm page created %p\n", __FUNCTION__, vm_page);
        printf("%s() - INFO: biggest data block found @ %p, met block size is %u and requested size is %u\n", 
            __FUNCTION__,
            biggest_block_meta_data,
            biggest_block_meta_data->block_size,
            req_size);
        /* allocate a free block from the new page */
        status = mm_split_free_data_block_for_allocation(vm_page_family, &vm_page->block_meta_data, req_size);
        if(status){
            return &vm_page->block_meta_data;
        }
        return NULL;
    }
    /* Check if the biggest meta data can satisfy the request */
    if(biggest_block_meta_data){
        /* allocate a free block from the new page */
        status = mm_split_free_data_block_for_allocation(vm_page_family, biggest_block_meta_data, req_size);
        if(status){
            return biggest_block_meta_data;
        }
    }
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
        strncpy(first_vm_page_for_families->vm_page_family[0].struct_name, struct_name, MM_MAX_STRUCT_NAME);
        first_vm_page_for_families->vm_page_family[0].struct_size = struct_size;
        first_vm_page_for_families->vm_page_family[0].first_page = NULL;
        init_glthread(&first_vm_page_for_families->vm_page_family[0].free_block_priority_list_head);
        return;
    }

    vm_page_family_curr = mm_lookup_page_family_by_name(struct_name);
    if(vm_page_family_curr)
        assert(0);

    /* Iterate over the family page and look if space is available */
    ITERATE_PAGE_FAMILIES_BEGIN(first_vm_page_for_families, vm_page_family_curr){
            
        count++;

    }ITERATE_PAGE_FAMILIES_END(first_vm_page_for_families, vm_page_family_curr);

    /* If no space is available, create a new vm page family */
    if(count == MAX_FAMILIES_PER_VM_PAGE){

        new_vm_page_for_families = (vm_page_for_families_t *)mm_get_new_vm_page_from_kernel(1);
        new_vm_page_for_families->next = first_vm_page_for_families;
        first_vm_page_for_families = new_vm_page_for_families;
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

    for(curr_vm_page_for_families = first_vm_page_for_families; curr_vm_page_for_families; 
        curr_vm_page_for_families = curr_vm_page_for_families->next)
    {
        ITERATE_PAGE_FAMILIES_BEGIN(curr_vm_page_for_families, vm_page_family_curr)
        {
            if (vm_page_family_curr->struct_size > 0U)
            {
                if (strncmp(vm_page_family_curr->struct_name, struct_name, MM_MAX_STRUCT_NAME) == 0U)
                {
                    return vm_page_family_curr;
                }
            }
        }
        ITERATE_PAGE_FAMILIES_END(curr_vm_page_for_families, vm_page_family_curr);
    }
    return NULL;
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
    //vm_page->block_meta_data.next_block = NULL;
    //vm_page->block_meta_data.prev_block = NULL;
    vm_page->next = NULL;
    vm_page->prev = NULL;
    vm_page->page_family = vm_page_family;
    init_glthread(&vm_page->block_meta_data.priority_list_glue);

    /*Set the back pointer to page family*/
    vm_page->page_family = vm_page_family;

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

void *xcalloc(char *struct_name, int units)
{
    /* Loop up if the structure is already registered in a vm page family */
    vm_page_family_t *page_family = mm_lookup_page_family_by_name(struct_name);
    if(!page_family){
        printf("Error: Structure %s is not registered with memory manager\n", struct_name);
        return NULL;
    }

    /* check if the requested memory fits with-in a vm page */
    if((page_family->struct_size * units) > mm_max_page_allocatable_memory(1)){
        printf("Error: Memory requested exceeds page size\n");
        return NULL;
    }

    /* find a data block which can satisfy the request */
    block_meta_data_t *free_block_meta_data = mm_allocate_free_data_block(page_family, (units * page_family->struct_size));
    if(free_block_meta_data){
        memset((char *)(free_block_meta_data + 1), 0, free_block_meta_data->block_size);
        return (void *)(free_block_meta_data + 1);
    }

    return NULL;
}

static int mm_get_hard_internal_memory_frag_size(block_meta_data_t *first, block_meta_data_t *second)
{
    //assert(first || second);
    /* confirm the existance of a hard fragmented memory between first and second arguments */
    //assert(first->is_free == MM_TRUE || second->is_free == MM_TRUE);

    block_meta_data_t *next_block = NEXT_META_BLOCK_BY_SIZE(first);
    return (int)(second - next_block);
}

static block_meta_data_t *mm_free_blocks(block_meta_data_t *to_be_free_block){

    block_meta_data_t *returning_block = NULL;
    block_meta_data_t *next_block = NULL;
    block_meta_data_t *prev_block = NULL;
    assert(to_be_free_block->is_free == MM_FALSE);

    vm_page_t *hosting_page = MM_GET_PAGE_FROM_META_BLOCK(to_be_free_block);
    vm_page_family_t *hosting_page_family = hosting_page->page_family;
    //returning_block = to_be_free_block;
    to_be_free_block->is_free = MM_TRUE;

    next_block = NEXT_META_BLOCK(to_be_free_block);
    prev_block = PREV_META_BLOCK(to_be_free_block);
    /* Handling Hard Fragmentated memory */
    if(next_block){
        /* Scenario #1: When data block to be freed is not the uppermost/last
         * meta block in the vm page. merge if the next met block is free.
        */
        to_be_free_block->block_size += mm_get_hard_internal_memory_frag_size(to_be_free_block, next_block);
    }
    else{
        /* Scenario #2: when data block is the uppermost/last meta block on the vm page boundary 
         * merge if there is a hard fragment on the boundary.
        */
        char *end_address_of_vm_page = ((char *)hosting_page + SYSTEM_PAGE_SIZE);
        char *end_address_of_free_data_block = (char *)(to_be_free_block + 1) + to_be_free_block->block_size;
        int internal_mem_fragmentation = (int)(end_address_of_vm_page - end_address_of_free_data_block);
        to_be_free_block->block_size += internal_mem_fragmentation;
    }

    /* Perform merging */
    if(next_block && next_block->is_free == MM_TRUE){
        mm_union_free_blocks(to_be_free_block, next_block);
        returning_block = to_be_free_block;
    }
    if(prev_block && prev_block->is_free == MM_TRUE){
        mm_union_free_blocks(prev_block, to_be_free_block);
        returning_block = prev_block;
    }

    if(mm_is_vm_page_empty(hosting_page)){
        mm_vm_page_delete_and_free(hosting_page);
        return NULL;
    }
    /* add the meta data block to the free blocks list */
    mm_add_free_block_meta_data_to_free_block_list(hosting_page->page_family, returning_block);
    return returning_block;
}

void xfree(void *app_data){

    block_meta_data_t *block_meta_data = (block_meta_data_t *)((char *)app_data - sizeof(block_meta_data_t));
    assert(block_meta_data->is_free == MM_FALSE);
    mm_free_blocks(block_meta_data);
}

void mm_print_block_usage(void)
{
    vm_page_for_families_t *vm_page_family_base_ptr = NULL;
    vm_page_family_t *vm_page_family_curr = NULL;
    vm_page_t *vm_page_curr = NULL;
    block_meta_data_t *block_meta_data_curr = NULL;

    uint32_t total_block_count = 0U;
    uint32_t free_block_count = 0U;
    uint32_t occupied_block_count = 0U;
    uint32_t app_memory_usage = 0U;

    vm_page_family_base_ptr = first_vm_page_for_families;

    ITERATE_PAGE_FAMILIES_BEGIN(vm_page_family_base_ptr, vm_page_family_curr){

        ITERATE_VM_PAGE_BEGIN(vm_page_family_curr, vm_page_curr){

            ITERATE_VM_PAGE_ALL_BLOCKS_BEGIN(vm_page_curr, block_meta_data_curr){

                total_block_count++;
                /* sanity checks */
                if(block_meta_data_curr->is_free == MM_FALSE){
                    assert(IS_GLTHREAD_LIST_EMPTY(&block_meta_data_curr->priority_list_glue));
                }
                if(block_meta_data_curr->is_free == MM_TRUE){
                    assert(!IS_GLTHREAD_LIST_EMPTY(&block_meta_data_curr->priority_list_glue));
                }

                if(block_meta_data_curr->is_free == MM_TRUE){
                    free_block_count++;
                }
                else{
                    app_memory_usage += block_meta_data_curr->block_size + sizeof(block_meta_data_t);
                    occupied_block_count++;
                }

            }ITERATE_VM_PAGE_ALL_BLOCKS_END(vm_page_curr, block_meta_data_curr);

        }ITERATE_VM_PAGE_END(vm_page_family_curr, vm_page_curr);

        printf("%-20s   TBC : %-4u    FBC : %-4u    OBC : %-4u AppMemUsage : %u\n",
            vm_page_family_curr->struct_name, total_block_count,
            free_block_count, occupied_block_count, app_memory_usage);

    }ITERATE_PAGE_FAMILIES_END(vm_page_family_base_ptr, vm_page_family_curr);
}

void mm_print_vm_page_details(vm_page_t *vm_page){

    printf("\t\t next = %p, prev = %p\n", vm_page->next, vm_page->prev);
    printf("\t\t page family = %s\n", vm_page->page_family->struct_name);

    uint32_t j = 0;
    block_meta_data_t *curr;
    ITERATE_VM_PAGE_ALL_BLOCKS_BEGIN(vm_page, curr){
        //printf("%s(): meta block @ %p\n", __FUNCTION__, (block_meta_data_t *)curr);
        printf("\t\t\t%-14p Block %-3u %s  block_size = %-6u  "
                "offset = %-6u  prev = %-14p  next = %p\n",
                curr,
                j++, curr->is_free ? "F R E E D" : "ALLOCATED",
                curr->block_size, curr->offset,
                curr->prev_block,
                curr->next_block);
    } ITERATE_VM_PAGE_ALL_BLOCKS_END(vm_page, curr);
}

void mm_print_memory_usage(char *struct_name)
{
    vm_page_for_families_t *vm_page_family_base_ptr = NULL;
    vm_page_family_t *vm_page_family_curr = NULL;
    vm_page_t *vm_page_curr = NULL;

    uint32_t number_of_struct_families = 0U;
    uint32_t cumulative_vm_pages_claimed_from_kernel = 0U;

    vm_page_family_base_ptr = first_vm_page_for_families;

    printf("\nPage Size = %zu Bytes\n", SYSTEM_PAGE_SIZE);

    ITERATE_PAGE_FAMILIES_BEGIN(vm_page_family_base_ptr, vm_page_family_curr){

        if(struct_name){
            if(strncmp(struct_name, vm_page_family_curr->struct_name, strlen(vm_page_family_curr->struct_name)))
                continue;
        }
        number_of_struct_families++;
        printf(ANSI_COLOR_GREEN "vm_page_family : %s, struct size = %u\n"
            ANSI_COLOR_RESET,
            vm_page_family_curr->struct_name,
            vm_page_family_curr->struct_size);

        ITERATE_VM_PAGE_BEGIN(vm_page_family_curr, vm_page_curr){

            cumulative_vm_pages_claimed_from_kernel++;
            printf("Entry\n");
            mm_print_vm_page_details(vm_page_curr);

        }ITERATE_VM_PAGE_END(vm_page_family_curr, vm_page_curr);

    }ITERATE_PAGE_FAMILIES_END(vm_page_family_base_ptr, vm_page_family_curr);

    printf(ANSI_COLOR_MAGENTA "# Of VM Pages in Use : %u (%lu Bytes)\n" \
        ANSI_COLOR_RESET,
        cumulative_vm_pages_claimed_from_kernel,
        SYSTEM_PAGE_SIZE * cumulative_vm_pages_claimed_from_kernel);

    float memory_app_use_to_total_memory_ratio = 0.0;

    printf("Total Memory being used by Memory Manager = %lu Bytes\n",
        cumulative_vm_pages_claimed_from_kernel * SYSTEM_PAGE_SIZE);
}
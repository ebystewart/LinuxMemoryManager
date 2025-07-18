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
        return;
    }

    /* Iterate over the family page and look if space is available */
    ITERATE_PAGE_FAMILIES_BEGIN(first_vm_page_for_families, vm_page_family_curr){
        if(strncmp(vm_page_family_curr->struct_name, struct_name, MM_MAX_STRUCT_NAME) != 0U){
            count++;
            continue;
        }
        assert(0);
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
}

vm_page_family_t *lookup_page_family_by_name(char *struct_name)
{
    uint32_t count = 0U;
    vm_page_family_t *vm_page_family_curr = NULL;
    vm_page_for_families_t *curr_vm_page_for_families = NULL;


    if(!first_vm_page_for_families){
        printf("Error: No Page family exists\n");
        return;
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
                return;
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

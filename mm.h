#ifndef MM_H_
#define MM_H_

#include <stdint.h>

#define MM_MAX_STRUCT_NAME 32U

typedef struct vm_page_family_{
    char struct_name[MM_MAX_STRUCT_NAME];
    uint32_t struct_size;
}vm_page_family_t;

typedef struct vm_page_for_families_{
    struct vm_page_for_families_ *next;
    vm_page_family_t vm_page_family[0];
}vm_page_for_families_t;

#define MAX_FAMILIES_PER_VM_PAGE \
        ((SYSTEM_PAGE_SIZE - sizeof(vm_page_for_families_t *))/sizeof(vm_page_family_t))

void mm_init(void);

#endif
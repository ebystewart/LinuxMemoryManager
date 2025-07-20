#ifndef _UAPI_MM_H_
#define _UAPI_MM_H_

#include "mm.h"

/* Function Prototypes */
void mm_init(void);
void mm_instantiate_new_page_family(char *struct_name, uint32_t struct_size);
void mm_print_registered_page_families(void);
void mm_print_memory_usage(char *struct_name);
void mm_print_block_usage(void);
void mm_print_vm_page_details(vm_page_t *vm_page);

void *xcalloc(char *struct_name, int units);

#define MM_REG_STRUCT(struct_name) \
    (mm_instantiate_new_page_family(#struct_name, sizeof(struct_name)))

#define XCALLOC(uints, struct_name) \
    (xcalloc(#struct_name, uints))

#endif
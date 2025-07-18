#ifndef _UAPI_MM_H_
#define _UAPI_MM_H_

#include "mm.h"

/* Function Prototypes */
void mm_init(void);
void mm_instantiate_new_page_family(char *struct_name, uint32_t struct_size);
void mm_print_registered_page_families(void);

#define MM_REG_STRUCT(struct_name) \
    (mm_instantiate_new_page_family(#struct_name, sizeof(struct_name)))

#endif
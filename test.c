#include <stdio.h>
#include <stdint.h>
#include "uapi_mm.h"

typedef struct emp_ {

    char name[32];
    uint32_t emp_id;
} emp_t;

typedef struct student_ {

    char name[32];
    uint32_t rollno;
    uint32_t marks_phys;
    uint32_t marks_chem;
    uint32_t marks_maths;
    struct student_ *next;
} student_t;


int main (int argc, char **argv)
{
    mm_init();
    //printf("VM Page size = %lu\n", SYSTEM_PAGE_SIZE);
    //void *addr1 = mm_get_new_vm_page_from_kernel(1);
    //void *addr2 = mm_get_new_vm_page_from_kernel(1);
    //printf("page 1 = %p, page 2 = %p\n", addr1, addr2);
    MM_REG_STRUCT(emp_t);
    MM_REG_STRUCT(student_t);
    mm_print_registered_page_families();
    XCALLOC(1, emp_t);
    XCALLOC(1, emp_t);
    XCALLOC(1, emp_t);

    XCALLOC(1, student_t);
    XCALLOC(1, student_t);
#if 0
    int i = 0;
    for(; i < 500; i++){
        XCALLOC(1, emp_t);
        XCALLOC(1, student_t);
    }
#endif
    mm_print_memory_usage(NULL);
    mm_print_block_usage();
    return 0;
}
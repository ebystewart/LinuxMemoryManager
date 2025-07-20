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
    int wait;
    mm_init();
    //printf("VM Page size = %lu\n", SYSTEM_PAGE_SIZE);
    //void *addr1 = mm_get_new_vm_page_from_kernel(1);
    //void *addr2 = mm_get_new_vm_page_from_kernel(1);
    //printf("page 1 = %p, page 2 = %p\n", addr1, addr2);
    MM_REG_STRUCT(emp_t);
    MM_REG_STRUCT(student_t);
    mm_print_registered_page_families();
    emp_t *emp1 = XCALLOC(1, emp_t);
    emp_t *emp2 = XCALLOC(1, emp_t);
    emp_t *emp3 = XCALLOC(1, emp_t);

    student_t *stud1 = XCALLOC(1, student_t);
    student_t *stud2 = XCALLOC(1, student_t);

    printf(" \nSCENARIO 1 : *********** \n");
    mm_print_memory_usage(0);
    mm_print_block_usage();


    scanf("%d", &wait); 

    XFREE(emp1);
    XFREE(emp3);
    XFREE(stud2);
    printf(" \nSCENARIO 2 : *********** \n");
    mm_print_memory_usage(0);
    mm_print_block_usage();


    scanf("%d", &wait); 
    
    XFREE(emp2);
    XFREE(stud1);
    printf(" \nSCENARIO 3 : *********** \n");
    mm_print_memory_usage(0);
    mm_print_block_usage();
    XCALLOC(1, student_t);
#if 0
    int i = 0;
    for(; i < 500; i++){
        XCALLOC(1, emp_t);
        XCALLOC(1, student_t);
    }
#endif
    return 0;
}
#include "glthread.h"
#include <stdlib.h>

void init_glthread(glthread_t *glthread)
{
    glthread->left  = NULL;
    glthread->right = NULL;

}

void glthread_add_next(glthread_t *curr_glthread, glthread_t *new_glthread)
{
    if(!curr_glthread->right)
    {
        new_glthread->left = curr_glthread;
        curr_glthread->right = new_glthread;
        return;
    }

    glthread_t *temp = curr_glthread->right;
    curr_glthread->right = new_glthread;
    new_glthread->left = curr_glthread;
    new_glthread->right = temp;
    temp->left = new_glthread;
}

void glthread_add_before(glthread_t *curr_glthread, glthread_t *new_glthread)
{
    if(!curr_glthread || !new_glthread)
        return;
    if(!curr_glthread->left){
        new_glthread->right = curr_glthread;
        new_glthread->left = NULL;
        curr_glthread->left = new_glthread;
        return;
    }
    new_glthread->right = curr_glthread;
    new_glthread->left = curr_glthread->left;
    curr_glthread->left->right = new_glthread;
    curr_glthread->left = new_glthread;
}

void glthread_add_last(glthread_t *glthread_head, glthread_t *new_glthread)
{
    if(!glthread_head || !new_glthread)
        return;
    glthread_t *last_ptr;
    glthread_t *curr;
    ITERATE_GLTHREAD_BEGIN(glthread_head, curr){
        if(!curr){
            last_ptr = curr;
        }
    }ITERATE_GLTHREAD_END(glthread_head, curr);
    if(!last_ptr){
        glthread_add_next(last_ptr, new_glthread);
    }
}

void remove_glthread(glthread_t *curr_glthread)
{
    if(!curr_glthread->left){
        if(curr_glthread->right){
            curr_glthread->right->left = NULL;
            curr_glthread->right = NULL;
            return;
        }
        return;
    }
    if(!curr_glthread->right){
        
        curr_glthread->left->right = NULL;
        curr_glthread->left = NULL;
        return;
    }
    curr_glthread->left->right = curr_glthread->right;
    curr_glthread->right->left = curr_glthread->left;
    curr_glthread->left = NULL;
    curr_glthread->right = NULL;
}

void delete_glthread_list(glthread_t *glthread_head)
{
    glthread_t *temp;
    ITERATE_GLTHREAD_BEGIN(glthread_head, temp){
        remove_glthread(temp);
    }ITERATE_GLTHREAD_END(glthread_head, temp);
}

unsigned int get_glthread_list_count(glthread_t *glthread_head)
{
    glthread_t *temp;
    unsigned int count = 0U;
    ITERATE_GLTHREAD_BEGIN(glthread_head, temp){
        count++;
    }ITERATE_GLTHREAD_END(glthread_head, temp);
}

void glthread_priority_insert(glthread_t *glthread_head, glthread_t *glthread, int (*comp_fn)(void *, void *), int offset)
{
    glthread_t *curr = NULL;
    glthread_t *prev = NULL;

    init_glthread(glthread);

    /* Empty list */
    if(IS_GLTHREAD_LIST_EMPTY(glthread_head)){
        glthread_add_next(glthread_head, glthread);
        return;
    }

    /* Only one node in the list */
    if(get_glthread_list_count(glthread_head) == 1U){
        if(comp_fn(GLTHREAD_GET_USER_DATA_FROM_OFFSET(glthread, offset), GLTHREAD_GET_USER_DATA_FROM_OFFSET(glthread_head->right, offset)) == -1){
            glthread_add_next(glthread_head, glthread);
        }
        else{
            glthread_add_next(glthread_head->right, glthread);
        }
        return;
    }

    ITERATE_GLTHREAD_BEGIN(glthread_head, curr){

        if(comp_fn(GLTHREAD_GET_USER_DATA_FROM_OFFSET(glthread, offset), 
                GLTHREAD_GET_USER_DATA_FROM_OFFSET(curr, offset)) != -1){
            prev = curr;
            continue;
        }

        if(!prev)
            glthread_add_next(glthread_head, glthread);
        else
            glthread_add_next(prev, glthread);
        
		return;

    }ITERATE_GLTHREAD_END(glthread_head, curr);

    /*Add in the end*/
    glthread_add_next(prev, glthread);
}

glthread_t *dequeue_glthread_first(glthread_t *base_glthread)
{
    glthread_t *temp;
    if(!base_glthread->right)
        return NULL;
    temp = base_glthread->right;
    remove_glthread(temp);
    return temp;
}
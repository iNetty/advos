/*_
 * Copyright (c) 2019 Hirochika Asai <asai@jar.jp>
 * All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef _ADVOS_PROC_H
#define _ADVOS_PROC_H

#include "kernel.h"
#include "memory.h"

#define SLAB_TASK               "task"
#define SLAB_PROC               "proc"
#define SLAB_TASK_STACK         "kstack"

#define PROC_PROG_ADDR          0x80000000ULL
#define PROC_PROG_SIZE          0x40000000ULL
#define PROC_STACK_SIZE         0x10000

typedef enum {
    TASK_CREATED,
    TASK_READY,
    TASK_BLOCKED,
    TASK_TERMINATED,
} task_state_t;

typedef struct _task task_t;
typedef struct _proc proc_t;

/*
 * Task
 */
struct _task {
    /* Architecture-specific structure; i.e., struct arch_task */
    void *arch;

    /* Process */
    proc_t *proc;

    /* Kernel stack */
    void *kstack;

    /* Task ID */
    int id;

    /* State */
    task_state_t state;

    /* Next scheduled task (runqueue) */
    task_t *next;

    /* Quantum */
    int credit;
};

/*
 * Process
 */
struct _proc {
    /* Process ID */
    pid_t pid;

    /* Process name */
    char name[PATH_MAX];

    /* Parent process */
    proc_t *parent;

    /* Task (currently, supporting single thread processes) */
    task_t *task;

    /* Process user information */
    uid_t uid;
    gid_t gid;

    /* Virtual memory */
    virt_memory_t *vmem;

    /* Code */
    struct {
        uintptr_t addr;
        size_t size;
    } code;

    /* Exit status */
    int exit_status;
};

/* Defined in task.c */
int task_mgr_init(size_t);
task_t * task_alloc(void);

/* Defined in proc. */
proc_t * proc_new(pid_t);
void proc_use(proc_t *);

#endif

/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: sw=4 ts=4 fdm=marker
 * vim<600: sw=4 ts=4
 */
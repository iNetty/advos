/*_
 * Copyright (c) 2018 Hirochika Asai <asai@jar.jp>
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

#include "memory.h"
#include "kernel.h"

/*
 * Allocate a slab
 */
static memory_slab_hdr_t *
_new_slab(memory_slab_allocator_t *slab, size_t objsize)
{
    void *pages;
    uintptr_t addr;
    volatile memory_slab_hdr_t *hdr;
    size_t size;
    int i;

    /* Allocate pages for slab */
    pages = memory_alloc_pages(slab->mem, MEMORY_SLAB_NUM_PAGES,
                               MEMORY_ZONE_NUMA_AWARE, 0);
    if ( NULL == pages ) {
        return NULL;
    }

    size = MEMORY_PAGESIZE * MEMORY_SLAB_NUM_PAGES;
    kmemset(pages, 0, size);
    hdr = pages;

    hdr->next = NULL;
    hdr->cache = NULL;

    /* Calculate the number of objects (64 byte alignment) */
    size = size - sizeof(memory_slab_hdr_t) - MEMORY_SLAB_ALIGNMENT;
    /* object and mark */
    hdr->nobjs = size / (objsize + 1);

    /* Calculate the pointer to the object array */
    addr = (uintptr_t)pages + sizeof(memory_slab_hdr_t) + hdr->nobjs;
    addr = ((addr + MEMORY_SLAB_ALIGNMENT - 1) / MEMORY_SLAB_ALIGNMENT)
        * MEMORY_SLAB_ALIGNMENT;
    hdr->obj_head = (void *)addr;
    /* Mark all objects free */
    for ( i = 0; i < hdr->nobjs; i++ ) {
        hdr->marks[i] = 1;
    }

    return (memory_slab_hdr_t *)hdr;
}

/*
 * Find the corresponding cache
 */
static memory_slab_cache_t *
_find_slab_cache(memory_slab_cache_t *n, const char *name)
{
    int ret;

    if ( NULL == n ) {
        /* Not found */
        return NULL;
    }

    ret = kstrncmp(n->name, name, MEMORY_SLAB_CACHE_NAME_MAX);
    if ( 0 == ret ) {
        return n;
    } else if ( ret < 0 ) {
        /* Search left */
        return _find_slab_cache(n->left, name);
    } else {
        /* Search right */
        return _find_slab_cache(n->right, name);
    }
}

/*
 * Add a slab cache
 */
static int
_add_slab_cache(memory_slab_cache_t **t, memory_slab_cache_t *n)
{
    int ret;

    if ( NULL == *t ) {
        /* Insert here */
        *t = n;
        return 0;
    }

    /* Compare and search */
    ret = kstrncmp((*t)->name, n->name, MEMORY_SLAB_CACHE_NAME_MAX);
    if ( 0 == ret ) {
        return -1;
    } else if ( ret < 0 ) {
        /* Search left */
        return _add_slab_cache(&(*t)->left, n);
    } else {
        /* Search right */
        return _add_slab_cache(&(*t)->right, n);
    }
}


/*
 * Allocate an object from the slab cache specified by the name
 */
static void *
_slab_alloc(memory_slab_allocator_t *slab, const char *name)
{
    memory_slab_cache_t *c;
    memory_slab_hdr_t *s;
    int i;
    void *obj;

    /* Find the slab cache corresponding to the name */
    c = _find_slab_cache(slab->root, name);
    if ( NULL == c ) {
        /* Not found */
        return NULL;
    }

    if ( NULL == c->freelist.partial && NULL == c->freelist.full ) {
        /* No object found, then try to allocate a new slab */
        s = _new_slab(slab, c->size);
        if ( NULL == s ) {
            /* Could not allocate a new slab */
            return NULL;
        }
        s->cache = c;
        c->freelist.full = s;
    }

    if ( NULL == c->freelist.partial ) {
        kassert( c->freelist.full != NULL );
        /* Take one full slab to the partial */
        c->freelist.partial = c->freelist.full;
        c->freelist.full = c->freelist.full->next;
    }

    /* Get an object */
    s = c->freelist.partial;
    obj = NULL;
    for ( i = 0; i < s->nobjs; i ++ ) {
        if ( s->marks[i] ) {
            /* Found a free object */
            obj = s->obj_head + c->size * i;
            s->marks[i] = 0;
            s->nused++;
            break;
        }
    }

    /* Check if the slab is still partial or becomes empty */
    if ( s->nused == s->nobjs ) {
        /* Move this partial slab to the empty free list */
        c->freelist.partial = s->next;
        s->next = c->freelist.empty;
        c->freelist.empty = s;
    }

    return obj;
}
void *
memory_slab_alloc(memory_slab_allocator_t *slab, const char *name)
{
    void *obj;

    spin_lock(&slab->lock);
    obj = _slab_alloc(slab, name);
    spin_unlock(&slab->lock);

    return obj;
}

/*
 * Search a slab containing to the object
 */
static memory_slab_hdr_t *
_find_slab_for_object(memory_slab_cache_t *c, memory_slab_hdr_t *h, void *obj)
{
    memory_slab_hdr_t *s;
    uintptr_t addr;
    uintptr_t start;
    uintptr_t end;

    /* Address of the object */
    addr = (uintptr_t)obj;

    /* Search a slab containing the object */
    s = h;
    while ( NULL != s ) {
        start = (uintptr_t)s->obj_head;
        end = start + c->size * s->nobjs;
        if ( addr >= start && addr < end ) {
            return s;
        }
        s = s->next;
    }

    return NULL;
}

/*
 * Free an object to the slab cache
 */
int
memory_slab_free(memory_slab_allocator_t *slab, const char *name, void *obj)
{
    memory_slab_cache_t *c;
    memory_slab_hdr_t *s;
    int waspartial;
    memory_slab_hdr_t **head;
    uintptr_t off;
    int idx;

    spin_lock(&slab->lock);

    /* Find the slab cache corresponding to the name */
    c = _find_slab_cache(slab->root, name);
    if ( NULL == c ) {
        /* Not found */
        spin_unlock(&slab->lock);
        return -1;
    }

    /* Search a slab containing the object */
    waspartial = 1;
    s = _find_slab_for_object(c, c->freelist.partial, obj);
    if ( NULL == s ) {
        /* Not found, then try to search empty list */
        waspartial = 0;
        s = _find_slab_for_object(c, c->freelist.empty, obj);
        if ( NULL == s ) {
            /* Not found */
            spin_unlock(&slab->lock);
            return -1;
        }
    }

    /* Calculate the offset and the index */
    off = obj - s->obj_head;
    idx = off / c->size;
    kassert( idx < s->nobjs );

    /* Mark as free */
    s->marks[idx] = 1;
    s->nused--;

    if ( !waspartial ) {
        /* Remove from emtpy, then add to the partial */
        head = &c->freelist.empty;
        while ( *head ) {
            if ( *head == s ) {
                /* Found, then remove s */
                *head = s->next;
                break;
            }
            head = &(*head)->next;
        }
        /* Add it to the partial list */
        s->next = c->freelist.partial;
        c->freelist.partial = s;
    }

    if ( s->nused == 0 ) {
        /* Remove from partial, then add to the full */
        head = &c->freelist.partial;
        while ( *head ) {
            if ( *head == s ) {
                /* Found, then remove s */
                *head = s->next;
                break;
            }
            head = &(*head)->next;
        }
        /* Add it to the full list */
        s->next = c->freelist.full;
        c->freelist.full = s;
    }

    spin_unlock(&slab->lock);

    return 0;
}

/*
 * Create a new slab cache
 */
int
memory_slab_create_cache(memory_slab_allocator_t *slab, const char *name,
                         size_t size)
{
    memory_slab_cache_t *cache;
    memory_slab_hdr_t *s;
    int ret;

    spin_lock(&slab->lock);

    /* Duplicate check */
    cache = _find_slab_cache(slab->root, name);
    if ( NULL != cache ) {
        /* Already exists */
        spin_unlock(&slab->lock);
        return -1;
    }

    /* Try to allocate a memory_slab_cache_t from the named slab cache */
    cache = _slab_alloc(slab, MEMORY_SLAB_CACHE_NAME);
    if ( NULL == cache ) {
        spin_unlock(&slab->lock);
        return -1;
    }
    kstrlcpy(cache->name, name, MEMORY_SLAB_CACHE_NAME_MAX);
    cache->size = size;
    cache->freelist.partial = NULL;
    cache->freelist.full = NULL;
    cache->freelist.empty = NULL;
    cache->left = NULL;
    cache->right = NULL;

    /* Allocate one slab for the full free list */
    s = _new_slab(slab, size);
    s->cache = cache;
    cache->freelist.full = s;

    /* Add to the cache tree */
    ret = _add_slab_cache(&slab->root, cache);
    kassert( ret == 0 );

    spin_unlock(&slab->lock);

    return 0;
}

/*
 * Prepare a slab_cache slab cache
 */
static int
_slab_cache_init(memory_slab_allocator_t *slab)
{
    memory_slab_hdr_t *s;
    memory_slab_cache_t *cache;
    int ret;

    /* Allocate one slab for the full free list */
    s = _new_slab(slab, sizeof(memory_slab_cache_t));
    if ( NULL == s ) {
        return -1;
    }

    /* Take the first object */
    s->marks[0] = 0;
    s->nused++;
    cache = s->obj_head;
    kstrlcpy(cache->name, MEMORY_SLAB_CACHE_NAME, MEMORY_SLAB_CACHE_NAME_MAX);
    cache->size = sizeof(memory_slab_cache_t);
    cache->freelist.partial = s;
    cache->freelist.full = NULL;
    cache->freelist.empty = NULL;
    cache->left = NULL;
    cache->right = NULL;
    s->cache = cache;

    /* Add to the cache tree */
    ret = _add_slab_cache(&slab->root, cache);
    kassert( ret == 0 );

    return 0;
}

/*
 * Initialize the slab allocator
 */
int
memory_slab_init(memory_slab_allocator_t *slab, memory_t *mem)
{
    int ret;

    slab->lock = 1;
    slab->mem = mem;
    slab->root = NULL;

    /* Create a slab cache for slab cache */
    ret = _slab_cache_init(slab);
    if ( ret < 0 ) {
        return -1;
    }

    /* Unlock */
    slab->lock = 0;

    return 0;
}


/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: sw=4 ts=4 fdm=marker
 * vim<600: sw=4 ts=4
 */

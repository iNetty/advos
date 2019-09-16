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
 * FITNESS FOR A PARTICULAR PURPSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "kernel.h"
#include "kvar.h"
#include "vfs.h"

#define SLAB_VFS_MODULE     "vfs_module"
#define SLAB_VFS_MOUNT      "vfs_mount"
#define SLAB_VNODE          "vnode"
#define VFS_DIR_DELIMITER   '/'

vfs_t vfs;

/*
 * Initialize the vfs
 */
int
vfs_init(void)
{
    int i;
    int ret;

    /* Initialize the modules */
    for ( i = 0; i < VFS_MAXFS; i++ ) {
        vfs.modules[i] = NULL;
    }

    /* Create a slab cache for filesystem modules */
    ret = kmem_slab_create_cache(SLAB_VFS_MODULE, sizeof(vfs_module_t));
    if ( ret < 0 ) {
        return -1;
    }

    /* Create a slab cache for the mount data structure */
    ret = kmem_slab_create_cache(SLAB_VFS_MOUNT, sizeof(vfs_mount_t));
    if ( ret < 0 ) {
        return -1;
    }

    /* Create a slab cache for vnodes */
    ret = kmem_slab_create_cache(SLAB_VNODE, sizeof(vfs_vnode_t));
    if ( ret < 0 ) {
        return -1;
    }

    return 0;
}

/*
 * open
 */
int
vfs_open(const char *path, int oflag, ...)
{
    const char *dir;

    /* Resolve the filesystem */
    dir = path;
    while ( '\0' != *path ) {
        if ( '/' == *path ) {
            /* Delimiter */
            break;
        }
        path++;
    }

    return -1;
}

/*
 * Register filesystem
 */
int
vfs_register(const char *type, vfs_interfaces_t *ifs, void *spec)
{
    int pos;
    vfs_module_t *e;

    /* Find the position to insert */
    for ( pos = 0; pos < VFS_MAXFS; pos++ ) {
        if ( NULL == vfs.modules[pos] ) {
            break;
        }
    }
    if ( pos >= VFS_MAXFS ) {
        return -1;
    }

    /* Check the length of the type */
    if ( kstrlen(type) >= VFS_MAXTYPE ) {
        return -1;
    }

    /* Allocate a vfs module */
    e = kmem_slab_alloc(SLAB_VFS_MODULE);
    if ( NULL == e ) {
        return -1;
    }
    e->spec = spec;
    kstrcpy(e->type, type);
    kmemcpy(&e->ifs, ifs, sizeof(vfs_interfaces_t));

    /* Set the entry */
    vfs.modules[pos] = e;

    return 0;
}

/*
 * Search the corresponding inode object
 */
static vfs_vnode_t *
_search_vnode(const char *path)
{
    vfs_vnode_t *vnode;

    /* Rootfs */
    if ( NULL == g_kvar->rootfs ) {
        /* Create a cache for the root directory */
        vnode = kmem_slab_alloc(SLAB_VNODE);
        if ( NULL == vnode ) {
            return NULL;
        }
        kmemset(vnode, 0, sizeof(vfs_vnode_t));
        g_kvar->rootfs = vnode;
    }

    if ( 0 == kstrcmp(path, "/") ) {
        return g_kvar->rootfs;
    }

    return NULL;
}

/*
 * vfs_mount
 */
int
vfs_mount(const char *type, const char *dir, int flags, void *data)
{
    int i;
    vfs_module_t *e;
    vfs_mount_t *mount;
    vfs_vnode_t *vnode;

    e = NULL;
    for ( i = 0; i < VFS_MAXFS; i++ ) {
        if ( NULL != vfs.modules[i]
             && 0 == kstrcmp(vfs.modules[i]->type, type) ) {
            e = vfs.modules[i];
        }
    }
    if ( NULL == e ) {
        return -1;
    }

    if ( NULL == e->ifs.mount ) {
        return -1;
    }
#if 0
    /* Search the mount point */
    vnode = _search_vnode(dir);
    if ( NULL != vnode->mount ) {
        /* Already mounted */
        return -1;
    }

    /* Allocate mount data structure */
    mount = kmem_slab_alloc(SLAB_VFS_MOUNT);
    if ( NULL == mount ) {
        return -1;
    }
#endif
    return e->ifs.mount(e->spec, dir, flags, data);
}

/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: sw=4 ts=4 fdm=marker
 * vim<600: sw=4 ts=4
 */

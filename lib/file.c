#include <inc/fs.h>
#include <inc/string.h>
#include <inc/lib.h>

union Fsipc fsipcbuf __attribute__((aligned(PAGE_SIZE)));

/* Send an inter-environment request to the file server, and wait for
 * a reply.  The request body should be in fsipcbuf, and parts of the
 * response may be written back to fsipcbuf.
 * type: request code, passed as the simple integer IPC value.
 * dstva: virtual address at which to receive reply page, 0 if none.
 * Returns result from the file server. */
static int
fsipc(unsigned type, void *dstva) {
    static envid_t fsenv;

    if (!fsenv) fsenv = ipc_find_env(ENV_TYPE_FS);

    static_assert(sizeof(fsipcbuf) == PAGE_SIZE, "Invalid fsipcbuf size");

    if (debug) {
        cprintf("[%08x] fsipc %d %08x\n",
                thisenv->env_id, type, *(uint32_t *)&fsipcbuf);
    }

    ipc_send(fsenv, type, &fsipcbuf, PAGE_SIZE, PROT_RW);
    size_t maxsz = PAGE_SIZE;
    return ipc_recv(NULL, dstva, &maxsz, NULL);
}

static int devfile_flush(struct Fd *fd);
static ssize_t devfile_read(struct Fd *fd, void *buf, size_t n);
static ssize_t devfile_write(struct Fd *fd, const void *buf, size_t n);
static int devfile_stat(struct Fd *fd, struct Stat *stat);
static int devfile_trunc(struct Fd *fd, off_t newsize);

struct Dev devfile = {
        .dev_id = 'f',
        .dev_name = "file",
        .dev_read = devfile_read,
        .dev_close = devfile_flush,
        .dev_stat = devfile_stat,
        .dev_write = devfile_write,
        .dev_trunc = devfile_trunc};

/* Set 'formatted_path' to formatted path without dots ('/.' situations) */
int
drop_dots(char *formatted_path, const char *path) {
    int len = strlen(path);
    for (int i = 0, j = 0; i < len; i++) {
        /* situation: 'smth/.' and current sym = '/' --> stop formatting */
        if (path[i] == '/' && path[i + 1] == '.' && i == len - 2) {
            return 0;
        }
        /* situation: 'smth/./' and current sym = '/' --> drop './' */
        if (path[i] == '/' && path[i + 1] == '.' && path[i + 2] == '/') {
            formatted_path[j] = '/';
            j++;
            i++;
            continue;
        }
        formatted_path[j] = path[i];
        j++;
    }
    return 0;
}

/* Set 'formatted_path' to formatted path (from original 'path'). 
 * Remove dots and duplicated slashes from original 'path'. */
int
format_path(char *formatted_path, const char *path) {
    char tmp[MAXPATHLEN] = {0};

    /* Remove dots from path */
    drop_dots(tmp, path);

    int len = strlen(tmp);
    /* Remove duplucated slashes from path */
    for (int i = 0, j = 0; i < len; i++) {
        if (tmp[i] == '/') {
            formatted_path[j] = tmp[i];
            j++;
        }
        while (tmp[i] == '/') {
            i++;
        }
        formatted_path[j] = tmp[i];
        j++;
    }
    return 0;
}

/* Set 'formatted_path' to formatted path without doubledots ('..' situations) */
int
drop_doubledots(char *formatted_path, const char *path) {
    /* Replace doubledots with # */
    int len = strlen(path);
    char tmp[MAXPATHLEN] = {0};
    strncpy(tmp, path, len);
    int skip = 0;
    for (int i = len - 1; i >= 0; i--) {
        if (tmp[i] == '.' && tmp[i - 1] == '.' && tmp[i - 2] == '/') {
            skip = 1;
            tmp[i] = '#';
            tmp[i - 1] = '#';
            i = i - 1;
            continue;
        }
        if (tmp[i] == '/' && tmp[i - 1] == '.' && tmp[i - 2] == '.' && tmp[i - 3] == '/') {
            skip++;
            tmp[i - 2] = '#';
            tmp[i - 1] = '#';
            i = i - 2;
            continue;
        }
        if (tmp[i] == '/' && skip > 0) {
            if (i == 0) {
                break;
            }
            i--;
            while (tmp[i] != '/') {
                tmp[i] = '#';
                i--;
            }
            i++;
            skip--;
        }
    }

    /* Skip # in path */
    for (int i = 0, j = 0; i < len; i++) {
        if (tmp[i] != '#') {
            formatted_path[j] = tmp[i];
            j++;
        }
    }

    /* Format path */
    memset(tmp, 0, MAXPATHLEN);
    format_path(tmp, formatted_path);
    strcpy(formatted_path, tmp);
    return 0;
}

/* Open a file (or directory).
 *
 * Returns:
 *  The file descriptor index on success
 *  -E_BAD_PATH if the path is too long (>= MAXPATHLEN)
 *  < 0 for other errors. */
int
open(const char *path, int mode) {
    /* Find an unused file descriptor page using fd_alloc.
     * Then send a file-open request to the file server.
     * Include 'path' and 'omode' in request,
     * and map the returned file descriptor page
     * at the appropriate fd address.
     * FSREQ_OPEN returns 0 on success, < 0 on failure.
     *
     * (fd_alloc does not allocate a page, it just returns an
     * unused fd address.  Do you need to allocate a page?)
     *
     * Return the file descriptor index.
     * If any step after fd_alloc fails, use fd_close to free the
     * file descriptor. */

    int res;
    struct Fd *fd;

    if (strlen(path) >= MAXPATHLEN)
        return -E_BAD_PATH;

    if (*strfind(path, '#') != '\0') {
		cprintf("Don't use # in filenames\n");
		return -E_BAD_PATH;
	}

    if ((res = fd_alloc(&fd)) < 0) return res;

    //strcpy(fsipcbuf.open.req_path, path);

    char cur_path[MAXPATHLEN] = {0};
	char new[MAXPATHLEN] = {0};
    char tmp[MAXPATHLEN] = {0};

    if (path[0] != '/') {
		getcwd(cur_path, MAXPATHLEN);
		strcat(cur_path, path);
	} else {
		strcat(cur_path, path);
	}

	format_path(new, cur_path);
	drop_doubledots(tmp, new);
	strcpy(new, tmp);
    strcpy(fsipcbuf.open.req_path, new);

    fsipcbuf.open.req_omode = mode;

    if ((res = fsipc(FSREQ_OPEN, fd)) < 0) {
        fd_close(fd, 0);
        return res;
    }

    if (!strcmp(new, "/dev/stdin")) {
		if (mode & O_SPAWN) {
			cprintf("You does not have permission to open dev/stdin.\n");
			return -E_INVAL;
		}
		fd_close(fd, 0);
		return 0;
	} else if (!strcmp(new, "/dev/stdout")) {
		if (mode & O_SPAWN) {
			cprintf("You does not have permission to open dev/stdout.\n");
			return -E_INVAL;
		}
		fd_close(fd, 0);
		return 1;
	} else if (!strcmp(new, "/dev/stderr")) {
		if (mode & O_SPAWN) {
			cprintf("You does not have permission to open dev/stderr.\n");
			return -E_INVAL;
		}
		fd_close(fd, 0);
		return 2;
	} else if (!strcmp(new, "/pipe")) {
		if (mode & O_SPAWN) {
			cprintf("You does not have permission to open pipe.\n");
			return -E_INVAL;
		}
		fd_close(fd, 0);
		return 3;
    }
    
    return fd2num(fd);
}

/* Flush the file descriptor.  After this the fileid is invalid.
 *
 * This function is called by fd_close.  fd_close will take care of
 * unmapping the FD page from this environment.  Since the server uses
 * the reference counts on the FD pages to detect which files are
 * open, unmapping it is enough to free up server-side resources.
 * Other than that, we just have to make sure our changes are flushed
 * to disk. */
static int
devfile_flush(struct Fd *fd) {
    fsipcbuf.flush.req_fileid = fd->fd_file.id;
    return fsipc(FSREQ_FLUSH, NULL);
}

/* Read at most 'n' bytes from 'fd' at the current position into 'buf'.
 *
 * Returns:
 *  The number of bytes successfully read.
 *  < 0 on error. */
static ssize_t
devfile_read(struct Fd *fd, void *buf, size_t n) {
    /* Make an FSREQ_READ request to the file system server after
     * filling fsipcbuf.read with the request arguments.  The
     * bytes read will be written back to fsipcbuf by the file
     * system server. */

    // LAB 10: Your code here:
    size_t i = 0;
    for (i = 0; i < n;) {
        fsipcbuf.read.req_fileid = fd->fd_file.id;
        fsipcbuf.read.req_n = n;
        int ret;
        if ((ret = fsipc(FSREQ_READ, NULL)) <= 0)
            return ret ? ret : i;

        memcpy(buf, fsipcbuf.readRet.ret_buf, ret);

        buf += ret;
        i += ret;
    }
    return i;
}

/* Write at most 'n' bytes from 'buf' to 'fd' at the current seek position.
 *
 * Returns:
 *   The number of bytes successfully written.
 *   < 0 on error. */
static ssize_t
devfile_write(struct Fd *fd, const void *buf, size_t n) {
    /* Make an FSREQ_WRITE request to the file system server.  Be
     * careful: fsipcbuf.write.req_buf is only so large, but
     * remember that write is always allowed to write *fewer*
     * bytes than requested. */
    // LAB 10: Your code here:
    size_t i = 0;
    for (i = 0; i < n;) {
        size_t next = MIN(n, sizeof(fsipcbuf.write.req_buf));
        memcpy(fsipcbuf.write.req_buf, buf, next);
        fsipcbuf.write.req_fileid = fd->fd_file.id;
        fsipcbuf.write.req_n = next;

        int ret;
        if ((ret = fsipc(FSREQ_WRITE, NULL)) < 0)
            return ret;

        buf += ret;
        i += ret;
    }
    return i;
}

/* Get file information */
static int
devfile_stat(struct Fd *fd, struct Stat *st) {
    fsipcbuf.stat.req_fileid = fd->fd_file.id;
    int res = fsipc(FSREQ_STAT, NULL);
    if (res < 0) return res;

    strcpy(st->st_name, fsipcbuf.statRet.ret_name);
    st->st_size = fsipcbuf.statRet.ret_size;
    st->st_isdir = fsipcbuf.statRet.ret_isdir;
    st->st_perm = fsipcbuf.statRet.ret_perm;
    st->st_issym = fsipcbuf.statRet.ret_issym;

    return 0;
}

/* Truncate or extend an open file to 'size' bytes */
static int
devfile_trunc(struct Fd *fd, off_t newsize) {
    fsipcbuf.set_size.req_fileid = fd->fd_file.id;
    fsipcbuf.set_size.req_size = newsize;

    return fsipc(FSREQ_SET_SIZE, NULL);
}

/* Synchronize disk with buffer cache */
int
sync(void) {
    /* Ask the file server to update the disk
     * by writing any dirty blocks in the buffer cache. */

    return fsipc(FSREQ_SYNC, NULL);
}

/* Remove file at 'path'. Sends the FSREQ_REMOVE IPC signal
 * and the 'path' to the file to be deleted.
 * serv.c/serve_remove receives signal and calls fs.c/file_remove. */
int
remove(const char *path) {
    char cur_path[MAXPATHLEN] = {0};
    if (path[0] != '/') {
        /* If relative path */
        getcwd(cur_path, MAXPATHLEN);
        strcat(cur_path, path);
    } else {
        /* If absolute path */
        strcat(cur_path, path);
    }
    char new[MAXPATHLEN] = {0};
    char tmp[MAXPATHLEN] = {0};
    format_path(new, cur_path);
    drop_doubledots(tmp, new);
    strcpy(fsipcbuf.remove.req_path, tmp);
    int res = fsipc(FSREQ_REMOVE, NULL);
    if (res < 0) {
        return res;
    }
    return 0;
}

/* Create symlink file at 'symlink_path' for file at 'path' */
int
symlink(const char *symlink_path, const char *path) {
    char cur_path[MAXPATHLEN] = {0};
    if (path[0] != '/') {
        /* If relative path */
        getcwd(cur_path, MAXPATHLEN);
        strcat(cur_path, path);
    } else {
        /* If absolute path */
        strcat(cur_path, path);
    }
    char symlink_cur_path[MAXPATHLEN] = {0};
    if (symlink_path[0] != '/') {
        /* If relative path */
        getcwd(symlink_cur_path, MAXPATHLEN);
        strcat(symlink_cur_path, symlink_path);
    } else {
        /* If absolute path */
        strcat(symlink_cur_path, symlink_path);
    }
    /* Create symlink file */
    int fd = open(symlink_cur_path, O_MKLINK | O_WRONLY | O_SYSTEM | O_EXCL);
    if (fd < 0) {
        return fd;
    }
    /* Write 'cur_path' to symlink file */
    int res = write(fd, cur_path, sizeof(cur_path));
    if (res != sizeof(cur_path)) {
        return res;
    }
    close(fd);
    return 0;
}

/* Set 'perm' permission for file at 'path'.
 * In RWX notation: 'perm' is 0 (000) to 7 (111). */
int
chmod(const char *path, int perm) {
	char cur_path[MAXPATHLEN] = {0};
	if (path[0] != '/') {
        /* If relative path */
		getcwd(cur_path, MAXPATHLEN);
		strcat(cur_path, path);
	} else {
        /* If absolute path */
		strcat(cur_path, path);
	}
    /* file.c/open --> (ipc signal) --> serv.c/serve_open --> fs.c/file_set_perm
     * So, set permissions 'perm' to file at 'path'. */
	int res = open(cur_path, O_CHMOD | (perm << 0x4));
	if (res < 0) {
		return res;
	}
	close(res);
	return 0;
}
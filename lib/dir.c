#include <inc/string.h>
#include <inc/lib.h>

/* Set 'buf' to the current working directory
 * of the calling process (to 'workpath' field from struct Env).
 * (It's for 'pwd' utility)
 * (cwd - Current Working Directory) */
char *
getcwd(char *buf, int maxlen) {
	if (!buf || maxlen < 0) {
		return (char *)thisenv->workpath;
	}
	/* strncpy copy 2nd arg to 1st, strncpy returns 1st arg */
	return strncpy((char *)buf, (const char *)thisenv->workpath, maxlen);
}

/* Сhanges the current working directory of the calling
 * process to the directory specified in 'path'. 
 * (It's for 'cd' utility) */
int
chdir(const char *path) {
	/* Forming new path in 'curr_path' */
	char curr_path[MAXPATHLEN] = {0};
    if (path[0] != '/') {
		getcwd(curr_path, MAXPATHLEN);
		strcat(curr_path, path);
	} else {
		strcat(curr_path, path);
	}
	
	/* Check 'curr_path' is dir */
	int fd;
	if ((fd = open(curr_path, O_RDONLY)) < 0) {
        printf("Error: open file at path: %s, fd: %i\n", curr_path, fd);
        return fd;
	}
	struct Stat st;
	int res = fstat(fd, &st);
	if (res < 0) {
		printf("Error: get fstat\n");
		return res;
	}
	close(fd);
	/* Error if 'curr_path' is not dir */
	if (!st.st_isdir) {
		return -E_INVAL;
	}

	/* Formatting new path in 'curr_path' */
	char tmp[MAXPATHLEN] = {0};
	char skip_dots_path[MAXPATHLEN] = {0};
	format_path(tmp, curr_path);
	drop_doubledots(skip_dots_path, tmp);
	strcpy(curr_path, skip_dots_path);
	if (curr_path[strlen(curr_path) - 1] != '/') {
		strcat((char *)curr_path, "/");
	}

	/* Call special syscall to set 'env->workpath' to 'curr_path' */
	return sys_env_set_workpath(thisenv->env_id, curr_path);
}

/* Create new directory by dirname */
int
mkdir(const char *dirname) {
	char curr_path[MAXPATHLEN] = {0};
	if (dirname[0] != '/') {
		/* If relative path */
		getcwd(curr_path, MAXPATHLEN);
		strcat(curr_path, dirname);
	} else {
		/* If absolute path */
		strcat(curr_path, dirname);
	}
	int res;
	if ((res = open(curr_path, O_MKDIR | O_EXCL)) < 0) {
		return res;
	}
	close(res);
	return 0;
}

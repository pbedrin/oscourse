#include <inc/lib.h>

void 
umain(int argc, char **argv) {
    if (argc != 2) {
        printf("Usage: cd [directory name]\n");
        return;
    }
	int res = chdir(argv[1]);
	if (res < 0) {
		printf("Error on chdir %s: %d\n", argv[1], res);
	}
}

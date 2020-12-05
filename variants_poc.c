// clang test.c

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>

int main(void) {
    struct dirent *dp;
    DIR *dirp;

    while (1) {
        dirp = opendir("./temp");
        if (dirp == NULL) {
            perror("opendir");
            exit(1);
        }

        dp = readdir(dirp);

        if (dp->d_off != 0) {
            printf("%lx\n", dp->d_off);
        }

        (void)closedir(dirp);
    }
}

// clang test.c

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>

int main(void) {
    struct dirent *dp;
    DIR *dirp;

    while (1) {
        dirp = opendir("./temp/test_dir");
        if (dirp == NULL) {
            perror("opendir");
            exit(1);
        }

        dp = readdir(dirp);

        if (dp->d_pad0 != 0 && dp->d_pad1 != 0) {
            printf("Leaked bytes:\n");
            printf("%x\n", dp->d_pad0); // Uninitialized
            printf("%x\n", dp->d_pad1); // Uninitialized
        }

        (void)closedir(dirp);
    }
}

# How did I find the bugs?

1. Randomly decide to audit the filesystems in FreeBSD
2. Do some research and find that the default filesystem in use is a combination of FFS and UFS
3. Spend some time auditing `ufs_create` and find 0 bugs
4. Go [here](https://github.com/freebsd/freebsd/commits/master/sys/ufs/ufs/ufs_vnops.c) and look through the commit history for the UFS file system functions
5. [Spot this commit about information being leaked through padding bytes in `struct dirent` objects allocated on the stack](https://github.com/freebsd/freebsd/commit/d08f717585ab19a81dfc5bd8aed1ac4edcb2b157#diff-5b073ff4500a0d79e01d6cc47a7821ce3ee02319a7e40d506900c8b54e52795a)
6. Analyze the patch and find that the patch to `msdosfs_readdir` is incomplete. They patched one instance of the bug, but not a second.
7. Write a PoC to confirm that I can leak 3 bytes from the padding. Then start looking for variants.
8. Find the variants in `mqueuefs`, `autofs`, `smbfs`, and `tmpfs` that let me leak a full 8 byte pointer. Write PoC to confirm.

# Original bug

As mentioned above, the original bug I found was in `msdosfs_readdir` while I was analyzing the patch for the commit linked above.

The basic flow for calling `readdir` on FreeBSD is as follows:

```c
#include <dirent.h>

int main(void) {
    struct dirent *dp;
    DIR *dirp;

    dirp = opendir("./somedir");
    dp = readdir(dirp);
}
```

Depending on the filesystem that `somedir` resides in, any one of many `*_readdir` functions in the FreeBSD kernel might be called.

The patch above adds a function called `dirent_terminate` which is intended to be called before a `struct dirent` object is to be returned to userspace (often done using the `uiomove` function). This function will zero out the padding bytes as well as any remaining bytes in the `d_name` field of the struct. [The definition of `struct dirent` is here](https://github.com/freebsd/freebsd/blob/b76de27e061f889036bec57de86bb0f0f404e269/sys/sys/dirent.h#L66).

Looking at the patch, [on line 1562](https://github.com/freebsd/freebsd/blob/b76de27e061f889036bec57de86bb0f0f404e269/sys/fs/msdosfs/msdosfs_vnops.c#L1562), you can see `dirent_terminate` being called with the `dirbuf` variable as the argument. Following this, [`uiomove` is called to copy `dirbuf`'s contents out back to userspace](https://github.com/freebsd/freebsd/blob/b76de27e061f889036bec57de86bb0f0f404e269/sys/fs/msdosfs/msdosfs_vnops.c#L1565). However, notice that these lines of code are within [this if statement's block](https://github.com/freebsd/freebsd/blob/b76de27e061f889036bec57de86bb0f0f404e269/sys/fs/msdosfs/msdosfs_vnops.c#L1534). The comment above this if statement explains that this branch is only taken if `readdir` is called on the root of the MSDOS filesystem, so we can simply skip this if statement by calling `readdir` in any sub-directory past the root of the filesystem.

Further down, [we see another call to `uiomove` on line 1691](https://github.com/freebsd/freebsd/blob/b76de27e061f889036bec57de86bb0f0f404e269/sys/fs/msdosfs/msdosfs_vnops.c#L1691). However, reading the code closely, you'll see that `dirent_terminate` isn't called in this instance, which means the padding bytes will remain uninitialized. Unfortunately the `d_name` field was zeroed out at the start of this function ([here](https://github.com/freebsd/freebsd/blob/b76de27e061f889036bec57de86bb0f0f404e269/sys/fs/msdosfs/msdosfs_vnops.c#L1505)), so we can't get a bigger leak.

# PoC

First, I didn't have a USB drive, so I had to figure out a way to mount an MSDOS filesystem. The following works:

```
$ dd if=/dev/zero of=test.img bs=512 count=256000
$ sudo mdconfig -a -t vnode -f test.img
$ sudo newfs_msdos -s 131072000 /dev/md1 # My mdconfig returned md1
$ mkdir ./temp
$ sudo mount -t msdosfs /dev/md1 ./temp
$ mkdir ./temp/test_dir
```

The PoC can be found in `original_poc.c`. Simply compile with `clang` and run it from the same directory as the commands above, and you will see the leaked bytes being printed out.

# Variants

I started looking for variants of this. I think I just grepped for `uiomove\(&.*,` which returned around 15-20 results, and I just checked all of them by hand. Unfortunately none of the variants exist in FreeBSD by default (the filesystems have to be manually enabled / compiled into the kernel). The functions with the variants are as follows:

1. `mqfs_readdir`
2. `tmpfs_dir_getdotdent`
3. `tmpfs_dir_getdotdotdent`
4. `smbfs_readvdir`
5. `autofs_readdir_one`

The bug is the exact same in all of these functions, so I'll just cover `mqfs_readdir`.

1. First, [a `struct dirent entry` is allocated on the stack](https://github.com/freebsd/freebsd/blob/b76de27e061f889036bec57de86bb0f0f404e269/sys/kern/uipc_mqueue.c#L1386)
2. Next, [`dirent_terminate` is called to zero out the padding + `d_name` fields of the struct](https://github.com/freebsd/freebsd/blob/b76de27e061f889036bec57de86bb0f0f404e269/sys/kern/uipc_mqueue.c#L1448)
3. Finally, [`vfs_read_dirent` is called. This function will call `uiomove` to copy the struct out to userspace](https://github.com/freebsd/freebsd/blob/b76de27e061f889036bec57de86bb0f0f404e269/sys/kern/uipc_mqueue.c#L1452)

Everything looks good so far, right? Not necessarily. We have to ensure all the struct's fields are initialized. If you look closely at the code, you'll see that the `d_off` field is left uninitialized. The type of this field is `off_t`, which is essentially an `int64_t`. When the struct is copied out to userspace, we get uninitialized data in this field.

# PoC

This same PoC will work for all of the variants, you just have to run it on a different filesystem. For `mqueuefs`, do the following (needs `mqueuefs` enabled / compiled into the kernel first):

```
$ mkdir ./temp
$ sudo mount -t mqueuefs null ./temp
```

The PoC itself can be found in `variants_poc.c`. Simply compile with `clang` and run it from the same directory as the commands above. You will see kernel pointers being printed out (presumably one stack pointer and one code section / heap pointer, I didn't check).

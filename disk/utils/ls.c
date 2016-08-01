
/*
 * Copyright (C) 2015 Matt Kilgore
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License v2 as published by the
 * Free Software Foundation.
 */

#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <dirent.h>

int main(int argc, char **argv)
{
    DIR *dir;

    dir = opendir("./");

    if (!dir) {
        printf("Error - Unable to open directory\n");
        return 1;
    }

    struct dirent *dent;

    while ((dent = readdir(dir))) {
        struct stat st;

        stat(dent->d_name, &st);

        printf("%d %d %s\n", dent->d_ino, st.st_size, dent->d_name);
    }

    closedir(dir);

    return 0;
}

/*
 * Copyright (C) 2016 Matt Kilgore
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License v2 as published by the
 * Free Software Foundation.
 */

// sh - shell, command line interpreter
#define UTILITY_NAME "sh"

#include "common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "readline_lite.h"
#include "shell.h"

#define INPUT_MAX 100

static int interactive = 0;
static FILE *inp_file;
static uid_t effective_uid;

struct job *current_job;

/* Gets the next line of input, either from the console or from the input file.
 * On EOF it returns NULL */
static char *get_next_line(void)
{
    char *line = NULL;

    if (interactive) {
        char prompt_buf[1024];
        snprintf(prompt_buf, sizeof(prompt_buf), "%s %c ", cwd, effective_uid == 0? '#': '$');

        line = readline_lite(prompt_buf);

        if (*line)
            history_add(line);
    } else {
        size_t buf_len = 0;
        ssize_t len = getline(&line, &buf_len, inp_file);

        /* EOF */
        if (len == -1) {
            free(line);
            return NULL;
        }

        if (len > 0) {
            /* Remove the newline at the end */
            line[len - 1] = '\0';
            len--;
        }
    }

    return line;
}

static void input_loop(void)
{
    char *line = NULL;
    struct job *job;

    effective_uid = geteuid();

    while (!feof(inp_file)) {
        job_update_background();

        if (current_job) {
            job_make_forground(current_job);
            current_job = NULL;
            continue;
        }

        free(line);
        line = get_next_line();

        /* EOF */
        if (!line)
            break;

        if (strcmp(line, "exit") == 0)
            break;

        job = shell_parse_job(line);

        if (!job)
            continue;

        if (job_is_simple_builtin(job)) {
            job_builtin_run(job);
            job_clear(job);
            free(job);
            continue;
        }

        job_add(job);
        job_first_start(job);
        current_job = job;
    }

    free(line);
}

void keyboard_input_loop(void)
{
    interactive = 1;
    inp_file = stdin;
    input_loop();
}

void script_input_loop(int scriptfile)
{
    inp_file = fdopen(scriptfile, "r");
    input_loop();
}

int load_script(const char *scriptfile)
{
    inp_file = fopen(scriptfile, "r");
    if (!inp_file)
        return 1;

    input_loop();

    return 0;
}

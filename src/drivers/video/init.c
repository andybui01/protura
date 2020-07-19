/*
 * Copyright (C) 2020 Matt Kilgore
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License v2 as published by the
 * Free Software Foundation.
 */

#include <protura/types.h>
#include <protura/debug.h>
#include <protura/string.h>
#include <protura/cmdline.h>

static int __video_is_disabled = 0;

int video_is_disabled(void)
{
    return __video_is_disabled;
}

void video_mark_disabled(void)
{
    __video_is_disabled = 1;
}

void video_init(void)
{
    __video_is_disabled = strcasecmp(kernel_cmdline_get_string("video", ""), "off") == 0;
    if (__video_is_disabled)
        kp(KP_NORMAL, "Video output is disabled, text only!\n");
}

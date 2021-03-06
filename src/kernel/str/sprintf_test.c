/*
 * Copyright (C) 2019 Matt Kilgore
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License v2 as published by the
 * Free Software Foundation.
 */
/*
 * Tests for sprintf.c - included directly at the end of sprintf.c
 */

#include <protura/types.h>
#include <protura/snprintf.h>
#include <protura/ktest.h>

static void snprintf_test_trimming(struct ktest *kt)
{
    char buf[100];

    int err = snprintf(buf, sizeof(buf), "This is 26 characters long");

    ktest_assert_equal(kt, err, 26);
    ktest_assert_equal_str(kt, "This is 26 characters long", buf);

    err = snprintf(buf, 26, "This is 26 characters long");

    ktest_assert_equal(kt, err, 25);
    ktest_assert_equal_str(kt, "This is 26 characters lon", buf);

    err = snprintf(buf, 10, "This is 26 characters long");

    ktest_assert_equal(kt, err, 9);
    ktest_assert_equal_str(kt, "This is 2", buf);
}

static const struct ktest_unit snprintf_test_units[] = {
    KTEST_UNIT("snprintf-trimming", snprintf_test_trimming),
};

KTEST_MODULE_DEFINE("snprintf", snprintf_test_units);

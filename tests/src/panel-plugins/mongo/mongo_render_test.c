/*
   src/panel-plugins/mongo -- tests for the rendering helpers.

   Copyright (C) 2026
   Free Software Foundation, Inc.

   Written by:
   Ilia Maslakov <il.smind@gmail.com>, 2026

   This file is part of the Midnight Commander.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#define TEST_SUITE_NAME "/src/panel-plugins/mongo"

#include "tests/mctest.h"

#include <string.h>

#include <bson/bson.h>

#include "src/panel-plugins/mongo/mongo_render.h"

/* --------------------------------------------------------------------------------------------- */
/* mongo_render_value                                                                            */
/* --------------------------------------------------------------------------------------------- */

START_TEST (test_render_value_null_input)
{
    char buf[32];
    mongo_render_value (NULL, buf, sizeof (buf));
    mctest_assert_str_eq (buf, "?");
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_render_value_oid)
{
    bson_value_t v;
    char buf[64];

    v.value_type = BSON_TYPE_OID;
    bson_oid_init_from_string (&v.value.v_oid, "507f1f77bcf86cd799439011");

    mongo_render_value (&v, buf, sizeof (buf));
    mctest_assert_str_eq (buf, "507f1f77bcf86cd799439011");
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_render_value_utf8)
{
    bson_value_t v;
    char buf[32];

    v.value_type = BSON_TYPE_UTF8;
    v.value.v_utf8.str = (char *) "alice";
    v.value.v_utf8.len = 5;

    mongo_render_value (&v, buf, sizeof (buf));
    mctest_assert_str_eq (buf, "\"alice\"");
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_render_value_int32)
{
    bson_value_t v;
    char buf[32];

    v.value_type = BSON_TYPE_INT32;
    v.value.v_int32 = -42;

    mongo_render_value (&v, buf, sizeof (buf));
    mctest_assert_str_eq (buf, "-42");
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_render_value_int64)
{
    bson_value_t v;
    char buf[64];

    v.value_type = BSON_TYPE_INT64;
    v.value.v_int64 = 9223372036854775807LL; /* INT64_MAX */

    mongo_render_value (&v, buf, sizeof (buf));
    mctest_assert_str_eq (buf, "9223372036854775807");
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_render_value_bool_true)
{
    bson_value_t v;
    char buf[16];

    v.value_type = BSON_TYPE_BOOL;
    v.value.v_bool = TRUE;

    mongo_render_value (&v, buf, sizeof (buf));
    mctest_assert_str_eq (buf, "true");
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_render_value_null_type)
{
    bson_value_t v;
    char buf[16];

    v.value_type = BSON_TYPE_NULL;
    mongo_render_value (&v, buf, sizeof (buf));
    mctest_assert_str_eq (buf, "null");
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_render_value_doc)
{
    bson_value_t v;
    char buf[16];

    v.value_type = BSON_TYPE_DOCUMENT;
    mongo_render_value (&v, buf, sizeof (buf));
    mctest_assert_str_eq (buf, "<doc>");
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_render_value_truncation_fits_buffer)
{
    bson_value_t v;
    char buf[8];

    v.value_type = BSON_TYPE_OID;
    bson_oid_init_from_string (&v.value.v_oid, "507f1f77bcf86cd799439011");

    /* Buffer too small for full oid (24 chars + NUL); g_strlcpy must
       NUL-terminate within the limit. */
    mongo_render_value (&v, buf, sizeof (buf));
    ck_assert_int_le ((int) strlen (buf), (int) sizeof (buf) - 1);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */
/* mongo_render_type                                                                             */
/* --------------------------------------------------------------------------------------------- */

START_TEST (test_render_type_oid)
{
    bson_value_t v;
    char buf[8];

    v.value_type = BSON_TYPE_OID;
    mongo_render_type (&v, buf, sizeof (buf));
    mctest_assert_str_eq (buf, "oid");
}
END_TEST

START_TEST (test_render_type_array)
{
    bson_value_t v;
    char buf[8];

    v.value_type = BSON_TYPE_ARRAY;
    mongo_render_type (&v, buf, sizeof (buf));
    mctest_assert_str_eq (buf, "arr");
}
END_TEST

START_TEST (test_render_type_null_input)
{
    char buf[8];
    mongo_render_type (NULL, buf, sizeof (buf));
    mctest_assert_str_eq (buf, "?");
}
END_TEST

/* --------------------------------------------------------------------------------------------- */
/* mongo_render_truncate_base64                                                                  */
/* --------------------------------------------------------------------------------------------- */

START_TEST (test_truncate_short_base64_untouched)
{
    /* 10 chars, below threshold -- must be left alone. */
    const char *in = "{\"base64\":\"SGVsbG8gd28=\"}";
    char *out = mongo_render_truncate_base64 (in, 24);
    mctest_assert_str_eq (out, in);
    g_free (out);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_truncate_long_base64_replaces_tail)
{
    /* 50 chars >> 24; expect first 21 chars + "..." inside the quotes. */
    const char *in = "{\"base64\":\"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwx\"}";
    const char *expected = "{\"base64\":\"ABCDEFGHIJKLMNOPQRSTU...\"}";
    char *out = mongo_render_truncate_base64 (in, 24);
    mctest_assert_str_eq (out, expected);
    g_free (out);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_truncate_preserves_other_fields)
{
    /* base64 truncated, neighbour fields untouched, $binary wrapping kept. */
    const char *in = "{\"data\":{\"$binary\":{\"base64\":"
                     "\"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\",\"subType\":\"00\"}}}";
    const char *expected = "{\"data\":{\"$binary\":{\"base64\":"
                           "\"AAAAAAAAAAAAAAAAAAAAA...\",\"subType\":\"00\"}}}";
    char *out = mongo_render_truncate_base64 (in, 24);
    mctest_assert_str_eq (out, expected);
    g_free (out);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_truncate_handles_whitespace_between_colon_and_quote)
{
    /* libbson typically writes no space, but a manual JSON should still pass. */
    const char *in = "{\"base64\":  \"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefgh\"}";
    const char *expected = "{\"base64\":  \"ABCDEFGHIJKLMNOPQRSTU...\"}";
    char *out = mongo_render_truncate_base64 (in, 24);
    mctest_assert_str_eq (out, expected);
    g_free (out);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_truncate_handles_spaces_around_colon)
{
    /* libmongoc 1.26+ relaxed Ext-JSON puts a space on BOTH sides of every
       colon: `"base64" : "..."`. The truncator must still catch it. */
    const char *in =
        "{ \"base64\" : \"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefgh\" , \"subType\" : \"00\" }";
    const char *expected = "{ \"base64\" : \"ABCDEFGHIJKLMNOPQRSTU...\" , \"subType\" : \"00\" }";
    char *out = mongo_render_truncate_base64 (in, 24);
    mctest_assert_str_eq (out, expected);
    g_free (out);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_truncate_null_input)
{
    char *out = mongo_render_truncate_base64 (NULL, 24);
    mctest_assert_null (out);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_truncate_keeps_minimum_room_for_ellipsis)
{
    /* Caller asks for a tiny @keep; helper must clamp so the ellipsis fits
       (3 chars + at least 1 content char). */
    const char *in = "{\"base64\":\"ABCDEFGHIJ\"}";
    char *out = mongo_render_truncate_base64 (in, 1);
    /* Expect "X..." inside the quotes: first 1 char + "..." */
    mctest_assert_str_eq (out, "{\"base64\":\"A...\"}");
    g_free (out);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_truncate_no_base64_pattern_passthrough)
{
    const char *in = "{\"name\":\"alice\",\"age\":30}";
    char *out = mongo_render_truncate_base64 (in, 24);
    mctest_assert_str_eq (out, in);
    g_free (out);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_truncate_plain_user_base64_field_also_trimmed)
{
    /* The text reducer intentionally matches any literal "base64" key. */
    const char *in = "{\"name\":\"x\",\"base64\":\"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnop\"}";
    const char *expected = "{\"name\":\"x\",\"base64\":\"ABCDEFGHIJKLMNOPQRSTU...\"}";
    char *out = mongo_render_truncate_base64 (in, 24);
    mctest_assert_str_eq (out, expected);
    g_free (out);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */
/* mongo_render_pretty_json                                                                      */
/* --------------------------------------------------------------------------------------------- */

START_TEST (test_pretty_null_input) { mctest_assert_null (mongo_render_pretty_json (NULL, 2)); }
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_pretty_simple_object)
{
    const char *in = "{\"a\":1,\"b\":2}";
    const char *expected = "{\n  \"a\": 1,\n  \"b\": 2\n}";
    char *out = mongo_render_pretty_json (in, 2);
    mctest_assert_str_eq (out, expected);
    g_free (out);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_pretty_nested_object)
{
    const char *in = "{\"a\":{\"b\":1}}";
    const char *expected = "{\n  \"a\": {\n    \"b\": 1\n  }\n}";
    char *out = mongo_render_pretty_json (in, 2);
    mctest_assert_str_eq (out, expected);
    g_free (out);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_pretty_empty_containers_inline)
{
    const char *in = "{\"a\":{},\"b\":[]}";
    const char *expected = "{\n  \"a\": {},\n  \"b\": []\n}";
    char *out = mongo_render_pretty_json (in, 2);
    mctest_assert_str_eq (out, expected);
    g_free (out);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_pretty_preserves_strings_with_braces)
{
    /* Braces inside strings must not change indentation. */
    const char *in = "{\"k\":\"{x:1,y:2}\"}";
    const char *expected = "{\n  \"k\": \"{x:1,y:2}\"\n}";
    char *out = mongo_render_pretty_json (in, 2);
    mctest_assert_str_eq (out, expected);
    g_free (out);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_pretty_handles_escaped_quote_in_string)
{
    const char *in = "{\"k\":\"a\\\"b\",\"n\":1}";
    const char *expected = "{\n  \"k\": \"a\\\"b\",\n  \"n\": 1\n}";
    char *out = mongo_render_pretty_json (in, 2);
    mctest_assert_str_eq (out, expected);
    g_free (out);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_pretty_strips_existing_whitespace)
{
    const char *in = "{ \"a\" : 1 , \"b\" : 2 }";
    const char *expected = "{\n  \"a\": 1,\n  \"b\": 2\n}";
    char *out = mongo_render_pretty_json (in, 2);
    mctest_assert_str_eq (out, expected);
    g_free (out);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_pretty_zero_indent)
{
    const char *in = "{\"a\":{\"b\":1}}";
    const char *expected = "{\n\"a\": {\n\"b\": 1\n}\n}";
    char *out = mongo_render_pretty_json (in, 0);
    mctest_assert_str_eq (out, expected);
    g_free (out);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

/* Negative indent is clamped to 0 (same output as indent == 0). */
START_TEST (test_pretty_negative_indent_clamped)
{
    const char *in = "{\"a\":1}";
    const char *expected = "{\n\"a\": 1\n}";
    char *out = mongo_render_pretty_json (in, -5);
    mctest_assert_str_eq (out, expected);
    g_free (out);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

/* All whitespace kinds (space, tab, newline, CR) are stripped outside strings,
   both in the main scan and in the empty-container look-ahead. */
START_TEST (test_pretty_skips_tab_newline_cr)
{
    const char *in = "{\n\t\"a\" :\r1 }";
    const char *expected = "{\n  \"a\": 1\n}";
    char *out = mongo_render_pretty_json (in, 2);
    mctest_assert_str_eq (out, expected);
    g_free (out);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_pretty_array_with_values)
{
    const char *in = "[1,2,{\"a\":3}]";
    const char *expected = "[\n  1,\n  2,\n  {\n    \"a\": 3\n  }\n]";
    char *out = mongo_render_pretty_json (in, 2);
    mctest_assert_str_eq (out, expected);
    g_free (out);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

int
main (void)
{
    TCase *tc_core;

    tc_core = tcase_create ("Core");

    tcase_add_test (tc_core, test_render_value_null_input);
    tcase_add_test (tc_core, test_render_value_oid);
    tcase_add_test (tc_core, test_render_value_utf8);
    tcase_add_test (tc_core, test_render_value_int32);
    tcase_add_test (tc_core, test_render_value_int64);
    tcase_add_test (tc_core, test_render_value_bool_true);
    tcase_add_test (tc_core, test_render_value_null_type);
    tcase_add_test (tc_core, test_render_value_doc);
    tcase_add_test (tc_core, test_render_value_truncation_fits_buffer);

    tcase_add_test (tc_core, test_render_type_oid);
    tcase_add_test (tc_core, test_render_type_array);
    tcase_add_test (tc_core, test_render_type_null_input);

    tcase_add_test (tc_core, test_truncate_short_base64_untouched);
    tcase_add_test (tc_core, test_truncate_long_base64_replaces_tail);
    tcase_add_test (tc_core, test_truncate_preserves_other_fields);
    tcase_add_test (tc_core, test_truncate_handles_whitespace_between_colon_and_quote);
    tcase_add_test (tc_core, test_truncate_handles_spaces_around_colon);
    tcase_add_test (tc_core, test_truncate_null_input);
    tcase_add_test (tc_core, test_truncate_keeps_minimum_room_for_ellipsis);
    tcase_add_test (tc_core, test_truncate_no_base64_pattern_passthrough);
    tcase_add_test (tc_core, test_truncate_plain_user_base64_field_also_trimmed);

    tcase_add_test (tc_core, test_pretty_null_input);
    tcase_add_test (tc_core, test_pretty_simple_object);
    tcase_add_test (tc_core, test_pretty_nested_object);
    tcase_add_test (tc_core, test_pretty_empty_containers_inline);
    tcase_add_test (tc_core, test_pretty_preserves_strings_with_braces);
    tcase_add_test (tc_core, test_pretty_handles_escaped_quote_in_string);
    tcase_add_test (tc_core, test_pretty_strips_existing_whitespace);
    tcase_add_test (tc_core, test_pretty_zero_indent);
    tcase_add_test (tc_core, test_pretty_negative_indent_clamped);
    tcase_add_test (tc_core, test_pretty_skips_tab_newline_cr);
    tcase_add_test (tc_core, test_pretty_array_with_values);

    return mctest_run_all (tc_core);
}

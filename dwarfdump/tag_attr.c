/*
  Copyright (C) 2000-2005 Silicon Graphics, Inc.  All Rights Reserved.
  Portions Copyright (C) 2009-2012 SN Systems Ltd. All Rights Reserved.
  Portions Copyright (C) 2009-2012 David Anderson. All Rights Reserved.

  This program is free software; you can redistribute it and/or modify it
  under the terms of version 2 of the GNU General Public License as
  published by the Free Software Foundation.

  This program is distributed in the hope that it would be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

  Further, this software is distributed without any warranty that it is
  free of the rightful claim of any third person regarding infringement
  or the like.  Any license provided herein, whether implied or
  otherwise, applies only to this software file.  Patent licenses, if
  any, provided herein do not apply to combinations of this program with
  other software, or any other product whatsoever.

  You should have received a copy of the GNU General Public License along
  with this program; if not, write the Free Software Foundation, Inc., 51
  Franklin Street - Fifth Floor, Boston MA 02110-1301, USA.

*/

#include <dwarf.h>
#include <stdio.h>
#include <stdarg.h>   /* For va_start va_arg va_list */
#include <stdlib.h>             /* For exit() declaration etc. */
#include <errno.h>              /* For errno declaration. */
#include <unistd.h>

#include "globals.h"
#include "libdwarf.h"
#include "common.h"
#include "tag_common.h"
#include "dwgetopt.h"

boolean ellipsis = FALSE; /* So we can use dwarf_names.c */

#define DW_VERSION_DATE_STR " 2016-06-13 09:14:52-07:00  "

/* Expected input format

0xffffffff
value of a tag
value of a standard attribute that follows that tag
...
0xffffffff
value of a tag
value of a standard attribute that follows that tag
...
0xffffffff
...

*/

unsigned int tag_attr_combination_table[ATTR_TABLE_ROW_MAXIMUM][ATTR_TABLE_COLUMN_MAXIMUM];

#ifdef HAVE_USAGE_TAG_ATTR
/* Working array for a specific tag and will contain its valid attributes */
static Dwarf_Half tag_attr_vector[DW_AT_last] = {0};
static Dwarf_Half tag_parents[DW_TAG_last] = {0};
static Dwarf_Half tag_children[DW_TAG_last] = {0};
static Dwarf_Small tag_attr_legal[DW_TAG_last] = {0};
#endif /* HAVE_USAGE_TAG_ATTR */

static const char *usage[] = {
  "Usage: tag_attr_build <options>",
  "    -i input-table-path",
  "    -o output-table-path",
  "    -s (Generate standard attribute table)",
  "    -e (Generate extended attribute table (common extensions))",
  ""
};

const char *program_name = 0;
char *input_name = 0;
char *output_name = 0;
int standard_flag = FALSE;
int extended_flag = FALSE;

/* process arguments */
static void
process_args(int argc, char *argv[])
{
    int c = 0;
    boolean usage_error = FALSE;

    program_name = argv[0];

    while ((c = dwgetopt(argc, argv, "i:o:se")) != EOF) {
        switch (c) {
        case 'i':
            input_name = dwoptarg;
            break;
        case 'o':
            output_name = dwoptarg;
            break;
        case 'e':
            extended_flag = TRUE;
            break;
        case 's':
            standard_flag = TRUE;
            break;
        default:
            usage_error = TRUE;
            break;
        }
    }

    if (usage_error || 1 == dwoptind || dwoptind != argc) {
        print_usage_message(argv[0],usage);
        exit(FAILED);
    }
}
/*  Two new naming routines May 2016.
    Instead of directly calling dwarf_get_*()
    When bad tag/attr numbers are presented we return
    a warning string through the pointer.
    The thought is that eventually someone will notice the error.
    It might, of course, be better to emit an error message
    and stop. */
static void
ta_get_AT_name(unsigned int attrnum,const char **nameout)
{
    int res = 0;

    res = dwarf_get_AT_name(attrnum,nameout);
    if (res == DW_DLV_OK) {
        return;
    }
    if (res == DW_DLV_NO_ENTRY) {
        *nameout = "<no name known for the attribute>";
        return;
    }
    *nameout = "<ERROR so no name known for the attribute>";
    return;
}

static void
ta_get_TAG_name(unsigned int tag,const char **nameout)
{
    int res = 0;

    res = dwarf_get_TAG_name(tag,nameout);
    if (res == DW_DLV_OK) {
        return;
    }
    if (res == DW_DLV_NO_ENTRY) {
        *nameout = "<no name known for the tag>";
        return;
    }
    *nameout = "<ERROR so no name known for the tag>";
    return;
}

int
main(int argc, char **argv)
{
    unsigned u = 0;
    unsigned int num = 0;
    int input_eof = 0;
    unsigned table_rows = 0;
    unsigned table_columns = 0;
    unsigned current_row = 0;
    FILE * fileInp = 0;
    FILE * fileOut = 0;
    const char *aname = 0;
    unsigned int index = 0;

    print_version_details(argv[0],FALSE);
    process_args(argc,argv);
    print_args(argc,argv);

    if (!input_name ) {
        fprintf(stderr,"Input name required, not supplied.\n");
        print_usage_message(argv[0],usage);
        exit(FAILED);
    }
    fileInp = fopen(input_name,"r");
    if (!fileInp) {
        fprintf(stderr,"Invalid input filename, could not open '%s'\n",
            input_name);
        print_usage_message(argv[0],usage);
        exit(FAILED);
    }

    if (!output_name ) {
        fprintf(stderr,"Output name required, not supplied.\n");
        print_usage_message(argv[0],usage);
        exit(FAILED);
    }
    fileOut = fopen(output_name,"w");
    if (!fileOut) {
        fprintf(stderr,"Invalid output filename, could not open: '%s'\n",
            output_name);
        print_usage_message(argv[0],usage);
        exit(FAILED);
    }
    if ((standard_flag && extended_flag) ||
        (!standard_flag && !extended_flag)) {
        fprintf(stderr,"Invalid table type\n");
        fprintf(stderr,"Choose -e  or -s .\n");
        print_usage_message(argv[0],usage);
        exit(FAILED);
    }

    if (standard_flag) {
        table_rows = STD_ATTR_TABLE_ROWS;
        table_columns = STD_ATTR_TABLE_COLUMNS;
    } else {
        table_rows = EXT_ATTR_TABLE_ROWS;
        table_columns = EXT_ATTR_TABLE_COLS;
    }

    input_eof = read_value(&num,fileInp);       /* 0xffffffff */
    if (IS_EOF == input_eof) {
        bad_line_input("Empty input file");
    }
    if (num != MAGIC_TOKEN_VALUE) {
        bad_line_input("Expected 0xffffffff");
    }

    /*  Generate main header, regardless of contents */
    fprintf(fileOut,"/* Generated code, do not edit. */\n");
    fprintf(fileOut,"/* Generated sourcedate %s */\n",DW_VERSION_DATE_STR);
    fprintf(fileOut,"\n/* BEGIN FILE */\n\n");

#ifdef HAVE_USAGE_TAG_ATTR
    /* Generate the data type to record the usage of the pairs tag-attr */
    if (standard_flag) {
        fprintf(fileOut,"#ifndef HAVE_USAGE_TAG_ATTR\n");
        fprintf(fileOut,"#define HAVE_USAGE_TAG_ATTR 1\n");
        fprintf(fileOut,"#endif /* HAVE_USAGE_TAG_ATTR */\n\n");
        fprintf(fileOut,"#ifdef HAVE_USAGE_TAG_ATTR\n");
        fprintf(fileOut,"#include \"dwarf.h\"\n");
        fprintf(fileOut,"#include \"libdwarf.h\"\n\n");
        fprintf(fileOut,"typedef struct {\n");
        fprintf(fileOut,"    unsigned int count; /* Attribute count */\n");
        fprintf(fileOut,"    Dwarf_Half attr;    /* Attribute value */\n");
        fprintf(fileOut,"} Usage_Tag_Attr;\n\n");
    }
#endif /* HAVE_USAGE_TAG_ATTR */

    while (!feof(stdin)) {
        unsigned int tag;
        unsigned int curcol = 0;
        unsigned int cur_attr = 0;
        unsigned int attr;

        input_eof = read_value(&tag,fileInp);
        if (IS_EOF == input_eof) {
            /* Reached normal eof */
            break;
        }
        if (standard_flag) {
            if (tag >= table_rows ) {
                bad_line_input("tag %d exceeds standard table size",tag);
            }
        } else {
            if (current_row >= table_rows) {
                bad_line_input("too many extended table rows.");
            }
            tag_attr_combination_table[current_row][0] = tag;
        }

        input_eof = read_value(&num,fileInp);
        if (IS_EOF == input_eof) {
            bad_line_input("Not terminated correctly..");
        }
        curcol = 1;
        cur_attr = 1;

#ifdef HAVE_USAGE_TAG_ATTR
        /* Check if we have duplicated tags */
        if (standard_flag) {
            if (tag_parents[tag]) {
                bad_line_input("tag 0x%02x value already defined",tag);
            }
            tag_parents[tag] = tag;

            /* Clear out the working attribute vector */
            memset(tag_attr_vector,0,DW_AT_last * sizeof(Dwarf_Half));
        }
#endif /* HAVE_USAGE_TAG_ATTR */

        while (num != MAGIC_TOKEN_VALUE) {
            if (standard_flag) {
                unsigned idx = num / BITS_PER_WORD;
                unsigned bit = num % BITS_PER_WORD;

                if (idx >= table_columns) {
                    char msg[200];
                    snprintf(msg, sizeof(msg),
                        "too many attributes a: table incomplete "
                        "index %d cols %d.",idx,table_columns);
                    bad_line_input(msg);
                }
                tag_attr_combination_table[tag][idx] |= (1 << bit);
            } else {
                if (curcol >= table_columns) {
                    char msg[200];
                    snprintf(msg, sizeof(msg),
                        "too many attributes b: table incomplete "
                        "index %d cols %d.",curcol,table_columns);
                    bad_line_input(msg);
                }
                tag_attr_combination_table[current_row][curcol] = num;
                curcol++;

            }

#ifdef HAVE_USAGE_TAG_ATTR
            /* Record the usage only for standard tables */
            if (standard_flag) {
                /* Add attribute to current tag */
                if (cur_attr >= DW_AT_last) {
                    char msg[200];
                    snprintf(msg, sizeof(msg),
                        "too many attributes c: table incomplete "
                        "index %d cols %d.",cur_attr,DW_AT_last);
                    bad_line_input(msg);
                }
                /* Check for duplicated entries */
                if (tag_attr_vector[cur_attr]) {
                    bad_line_input("duplicated attributes: table incomplete.");
                }
                tag_attr_vector[cur_attr] = num;
                cur_attr++;
            }
#endif /* HAVE_USAGE_TAG_ATTR */

            input_eof = read_value(&num,fileInp);
            if (IS_EOF == input_eof) {
                bad_line_input("Not terminated correctly.");
            }
        }

#ifdef HAVE_USAGE_TAG_ATTR
        /* Generate the tag-attributes vector for current tag */
        if (standard_flag) {
            if (tag >= DW_TAG_last) {
                bad_line_input("tag 0x%02x exceeds standard table size",tag);
            }
            if (tag_children[tag]) {
                bad_line_input("tag 0x%02x already defined",tag);
            }
            tag_children[tag] = tag;
            /* Generate reference vector */
            aname = 0;
            ta_get_TAG_name(tag,&aname);
            fprintf(fileOut,"/* 0x%02x - %s */\n",tag,aname);
            fprintf(fileOut,
                "static Usage_Tag_Attr tag_attr_%02x[] = {\n",tag);
            for (index = 1; index < cur_attr; ++index) {
                attr = tag_attr_vector[index];
                ta_get_AT_name(attr,&aname);
                fprintf(fileOut,"    {/* 0x%02x */  0, %s},\n",attr,aname);
            }
            fprintf(fileOut,"    {/* %4s */  0, 0}\n};\n\n"," ");
            /* Record allowed number of attributes */
            tag_attr_legal[tag] = cur_attr - 1;
        }
#endif /* HAVE_USAGE_TAG_ATTR */

        ++current_row;
    }

#ifdef HAVE_USAGE_TAG_ATTR
    /* Generate the parent of the individual vectors */
    if (standard_flag) {
        unsigned int tag;
        unsigned int legal;

        fprintf(fileOut,
            "static Usage_Tag_Attr *usage_tag_attr[] = {\n");
        for (index = 0; index < DW_TAG_last; ++index) {
            tag = tag_children[index];
            if (tag) {
                aname = 0;
                ta_get_TAG_name(tag,&aname);
                fprintf(fileOut,
                    "    tag_attr_%02x, /* 0x%02x - %s */\n",tag,tag,aname);
            } else {
                fprintf(fileOut,"    0,\n");
            }
        }
        fprintf(fileOut,"    0\n};\n\n");

        /* Generate table with allowed number of attributes */
        fprintf(fileOut,"typedef struct {\n");
        fprintf(fileOut,"    Dwarf_Small legal; /* Legal attributes */\n");
        fprintf(fileOut,"    Dwarf_Small found; /* Found attributes */\n");
        fprintf(fileOut,"} Rate_Tag_Attr;\n\n");
        fprintf(fileOut,
            "static Rate_Tag_Attr rate_tag_attr[] = {\n");
        for (tag = 0; tag < DW_TAG_last; ++tag) {
            if (tag_children[tag]) {
                legal = tag_attr_legal[tag];
                aname = 0;
                ta_get_TAG_name(tag,&aname);
                fprintf(fileOut,
                    "    {%2d, 0, /* 0x%02x - %s */},\n",legal,tag,aname);
            } else {
                fprintf(fileOut,"    {0, 0},\n");
            }
        }
        fprintf(fileOut,"    {0, 0}\n};\n\n");
        fprintf(fileOut,"#endif /* HAVE_USAGE_TAG_ATTR */\n\n");
    }
#endif /* HAVE_USAGE_TAG_ATTR */

    if (standard_flag) {
        fprintf(fileOut,"#define ATTR_TREE_ROW_COUNT %d\n\n",table_rows);
        fprintf(fileOut,"#define ATTR_TREE_COLUMN_COUNT %d\n\n",table_columns);
        fprintf(fileOut,
            "static unsigned int tag_attr_combination_table"
            "[ATTR_TREE_ROW_COUNT][ATTR_TREE_COLUMN_COUNT] = {\n");
    }
    else {
        fprintf(fileOut,"/* Common extensions */\n");
        fprintf(fileOut,"#define ATTR_TREE_EXT_ROW_COUNT %d\n\n",table_rows);
        fprintf(fileOut,"#define ATTR_TREE_EXT_COLUMN_COUNT %d\n\n",
            table_columns);
        fprintf(fileOut,
            "static unsigned int tag_attr_combination_ext_table"
            "[ATTR_TREE_EXT_ROW_COUNT][ATTR_TREE_EXT_COLUMN_COUNT] = {\n");
    }

    for (u = 0; u < table_rows; u++) {
        unsigned j = 0;
        const char *name = 0;
        if (standard_flag) {
            ta_get_TAG_name(u,&name);
            fprintf(fileOut,"/* 0x%02x - %-37s*/\n",u,name);
        } else {
            unsigned k = tag_attr_combination_table[u][0];
            ta_get_TAG_name(k,&name);
            fprintf(fileOut,"/* 0x%02x - %-37s*/\n",k,name);
        }
        fprintf(fileOut,"    { ");
        for (j = 0; j < table_columns; ++j ) {
            fprintf(fileOut,"0x%08x,",tag_attr_combination_table[u][j]);
        }
        fprintf(fileOut,"},\n");
    }
    fprintf(fileOut,"};\n");
    fprintf(fileOut,"\n/* END FILE */\n");
    fclose(fileInp);
    fclose(fileOut);
    return (0);
}
/* A fake so we can use dwarf_names.c */
void print_error (UNUSEDARG Dwarf_Debug dbg,
    UNUSEDARG const char * msg,
    UNUSEDARG int res,
    UNUSEDARG Dwarf_Error localerr)
{
}

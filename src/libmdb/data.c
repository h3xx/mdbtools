/* MDB Tools - A library for reading MS Access database file
 * Copyright (C) 2000 Brian Bruns
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include "mdbtools.h"

char *mdb_money_to_string(MdbHandle *mdb, int start, char *s);

#define MDB_DEBUG 0

void mdb_bind_column(MdbTableDef *table, int col_num, void *bind_ptr)
{
MdbColumn *col;

	/* 
	** the column arrary is 0 based, so decrement to get 1 based parameter 
	*/
	col=g_ptr_array_index(table->columns, col_num - 1);
	col->bind_ptr = bind_ptr;
}
int mdb_find_end_of_row(MdbHandle *mdb, int row)
{
int row_start, row_end, i;

        /* Search the previous "row start" values for the first non-deleted
one.
         * If we don't find one, then the end of the page is the correct value.
         */
        for (i = row - 1; i >= 0; i--) {
                row_start = mdb_get_int16(mdb, (10 + i * 2));
                if (!(row_start & 0x8000)) {
                        break;
                }
        }

        if (i == -1) {
                row_end = mdb->pg_size - 1;
        } else {
                row_end = row_start - 1;
        }

	return row_end;
}
static int mdb_is_null(unsigned char *null_mask, int col_num)
{
int byte_num = (col_num - 1) / 8;
int bit_num = (col_num - 1) % 8;

	if ((1 << bit_num) & null_mask[byte_num]) {
		return 0;
	} else {
		return 1;
	}
}
/* bool has to be handled specially because it uses the null bit to store its 
** value*/
static int mdb_xfer_bound_bool(MdbHandle *mdb, MdbColumn *col, int value)
{
	if (col->bind_ptr) {
		strcpy(col->bind_ptr,  value ? "0" : "1");
	}
}
static int mdb_xfer_bound_data(MdbHandle *mdb, int start, MdbColumn *col, int len)
{
	if (col->bind_ptr) {
		if (len) {
			strcpy(col->bind_ptr, 
				mdb_col_to_string(mdb, start, col->col_type, len));
			return col->col_size;
		} else {
			strcpy(col->bind_ptr,  "");
		}
	}
	return 0;
}
int mdb_read_row(MdbTableDef *table, int row)
{
MdbHandle *mdb = table->entry->mdb;
MdbColumn *col;
int i, j;
int num_cols, var_cols, fixed_cols;
int row_start, row_end;
int fixed_cols_found, var_cols_found;
int col_start, len;
int num_of_jumps=0, jumps_used=0;
int eod; /* end of data */
int delflag, lookupflag;
int bitmask_sz;
unsigned char null_mask[33]; /* 256 columns max / 8 bits per byte */

	row_start = mdb_get_int16(mdb, 10+(row*2)); 
	row_end = mdb_find_end_of_row(mdb, row);

	delflag = lookupflag = 0;
	if (row_start & 0x8000) delflag++;
	if (row_start & 0x4000) lookupflag++;
	row_start &= 0x0FFF; /* remove flags */
#if DEBUG
	fprintf(stdout,"Row %d bytes %d to %d %s %s\n", 
		row, row_start, row_end,
		lookupflag ? "[lookup]" : "",
		delflag ? "[delflag]" : "");
#endif	
	if (delflag || lookupflag) {
		row_end = row_start-1;
		return 0;
	}

#if MDB_DEBUG
	buffer_dump(mdb->pg_buf, row_start, row_end);
#endif

	/* find out all the important stuff about the row */
	num_cols = mdb->pg_buf[row_start];
	var_cols = 0; /* mdb->pg_buf[row_end-1]; */
	fixed_cols = 0; /* num_cols - var_cols; */
	for (j = 0; j < table->num_cols; j++) {
		col = g_ptr_array_index (table->columns, j);
		if (mdb_is_fixed_col(col)) 
			fixed_cols++;
		else
			var_cols++;
	}
	bitmask_sz = (num_cols - 1) / 8 + 1;
	eod = mdb->pg_buf[row_end-1-var_cols-bitmask_sz];
	for (i=0;i<bitmask_sz;i++) {
		null_mask[i]=mdb->pg_buf[row_end - bitmask_sz + i + 1];
	}

#if MDB_DEBUG
	fprintf(stdout,"#cols: %-3d #varcols %-3d EOD %-3d\n", 
		num_cols, var_cols, eod);
#endif

	/* data starts at 1 */
	col_start = 1;
	fixed_cols_found = 0;
	var_cols_found = 0;

	/* fixed columns */
	for (j=0;j<table->num_cols;j++) {
		col = g_ptr_array_index(table->columns,j);
		if (mdb_is_fixed_col(col) &&
		    ++fixed_cols_found <= fixed_cols) {
			if (col->col_type == MDB_BOOL) {
				mdb_xfer_bound_bool(mdb, col, mdb_is_null(null_mask, j+1));
			} else if (mdb_is_null(null_mask, j+1)) {
				mdb_xfer_bound_data(mdb, 0, col, 0);
			} else {
				mdb_xfer_bound_data(mdb,row_start + col_start, col, col->col_size);
			}
			col_start += col->col_size;
		}
	}

       if (col_start >= 256) {
               num_of_jumps++;
               jumps_used++;
               row_start = row_start + col_start - (col_start % 256);
       }

       col_start = row_start;
       while (col_start+256 < row_end-bitmask_sz-1-var_cols-num_of_jumps){
               col_start += 256;
               num_of_jumps++;
       }
       eod = mdb->pg_buf[row_end-1-var_cols-bitmask_sz-num_of_jumps];

       col_start = mdb->pg_buf[row_end-bitmask_sz-1-num_of_jumps];


	/* variable columns */
	for (j=0;j<table->num_cols;j++) {
		col = g_ptr_array_index(table->columns,j);
		if (!mdb_is_fixed_col(col) &&
		    ++var_cols_found <= var_cols) {
			/* col_start = mdb->pg_buf[row_end-bitmask_sz-var_cols_found]; */
			/* more code goes here but the diff is mangled */
			
			if (var_cols_found == mdb->pg_buf[row_end-bitmask_sz-jumps_used-1] &&
				jumps_used < num_of_jumps) {
				row_start += 256;
				col_start -= 256;
				jumps_used++;
			}


			if (var_cols_found==var_cols) 
				len=eod - col_start;
			else 
				len=mdb->pg_buf[row_end 
					- bitmask_sz 
					- var_cols_found 
					- 1 - num_of_jumps ] - col_start;

			if (col->col_type == MDB_BOOL) {
				mdb_xfer_bound_bool(mdb, col, mdb_is_null(null_mask, j+1));
			} else if (mdb_is_null(null_mask, j+1)) {
				mdb_xfer_bound_data(mdb, 0, col, 0);
			} else {
				mdb_xfer_bound_data(mdb,row_start + col_start, col, len);
			}
			col_start += len;
		}
	}

	return 1;
}
int mdb_read_next_dpg(MdbTableDef *table)
{
MdbCatalogEntry *entry = table->entry;
MdbHandle *mdb = entry->mdb;

	do {
		if (!mdb_read_pg(mdb, table->cur_phys_pg++))
			return 0;
	} while (mdb->pg_buf[0]!=0x01 || mdb_get_int32(mdb, 4)!=entry->table_pg);
	return table->cur_phys_pg;
}
int mdb_rewind_table(MdbTableDef *table)
{
	table->cur_pg_num=0;
	table->cur_phys_pg=0;
	table->cur_row=0;
}
int mdb_fetch_row(MdbTableDef *table)
{
MdbHandle *mdb = table->entry->mdb;
int rows;

	if (table->num_rows==0)
		return 0;

	/* initialize */
	if (!table->cur_pg_num) {
		table->cur_pg_num=1;
		table->cur_row=0;
		mdb_read_next_dpg(table);
	}

	rows = mdb_get_int16(mdb,8);

	/* if at end of page, find a new page */
	if (table->cur_row >= rows) {
		table->cur_row=0;
		if (!mdb_read_next_dpg(table)) return 0;
	}

	mdb_read_row(table, 
		table->cur_row);

	table->cur_row++;
	return 1;
}
void mdb_data_dump(MdbTableDef *table)
{
MdbHandle *mdb = table->entry->mdb;
int i, j, pg_num;
int rows;
char *bound_values[MDB_MAX_COLS]; 

	for (i=0;i<table->num_cols;i++) {
		bound_values[i] = (char *) malloc(256);
		mdb_bind_column(table, i+1, bound_values[i]);
	}
	mdb_rewind_table(table);
	while (mdb_fetch_row(table)) {
		for (j=0;j<table->num_cols;j++) {
			fprintf(stdout, "column %d is %s\n", j+1, bound_values[j]);
		}
	}
	for (i=0;i<table->num_cols;i++) {
		free(bound_values[i]);
	}
}

int mdb_is_fixed_col(MdbColumn *col)
{
	return col->is_fixed;
}
static char *mdb_memo_to_string(MdbHandle *mdb, int start, int size)
{
short memo_len;
static char text[MDB_BIND_SIZE];

	if (size<MDB_MEMO_OVERHEAD) {
		return "";
	} else if (size == MDB_MEMO_OVERHEAD) {
		/* If the only thing here is the field information, the
		* data is linked.  Go read it from another page.
		* Question: Do we need to read and evaluate the whole page?
		* Question: It seems to be the only record on the page.  If
		*           it weren't, how do we know which record it is?
		*
		* WARNING: Assuming the storage area is at least 2048 bytes!
		*/

		/* The 16 bit integer at offset 0 is the length of the memo field.
		* The 16 bit integer at offset 5 is the page it is stored on.
		*/
		memo_len = mdb_get_int16(mdb, start);

		if(mdb_read_alt_pg(mdb, mdb_get_int16(mdb, start+5)) != mdb->pg_size) {
			/* Failed to read */
			return "";
		}
		strncpy(text, &mdb->alt_pg_buf[mdb->pg_size - memo_len], memo_len);
		return text;
	} else {
		strncpy(text, &mdb->pg_buf[start + MDB_MEMO_OVERHEAD],
			size - MDB_MEMO_OVERHEAD);
		text[size - MDB_MEMO_OVERHEAD]='\0';
		return text;
	}
#if 0
			strncpy(text, &mdb->pg_buf[start + MDB_MEMO_OVERHEAD], 
				size - MDB_MEMO_OVERHEAD);
			text[size - MDB_MEMO_OVERHEAD]='\0';
			return text;
#endif
}
char *mdb_col_to_string(MdbHandle *mdb, int start, int datatype, int size)
{
/* FIX ME -- not thread safe */
static char text[MDB_BIND_SIZE];
time_t t;

	switch (datatype) {
		case MDB_BOOL:
			/* shouldn't happen.  bools are handled specially
			** by mdb_xfer_bound_bool() */
		break;
		case MDB_BYTE:
			sprintf(text,"%d",mdb_get_byte(mdb, start));
			return text;
		break;
		case MDB_INT:
			sprintf(text,"%ld",mdb_get_int16(mdb, start));
			return text;
		break;
		case MDB_LONGINT:
			sprintf(text,"%ld",mdb_get_int32(mdb, start));
			return text;
		break;
		case MDB_FLOAT:
			sprintf(text,"%f",mdb_get_double(mdb, start));
			return text;
		break;
		case MDB_DOUBLE:
			sprintf(text,"%f",mdb_get_double(mdb, start));
			return text;
		break;
		case MDB_TEXT:
			if (size<0) {
				return "";
			}
			strncpy(text, &mdb->pg_buf[start], size);
			text[size]='\0';
			return text;
		break;
		case MDB_SDATETIME:
			t = (long int)((mdb_get_double(mdb, start) - 25569.0) * 86400.0);
			strftime(text, MDB_BIND_SIZE, "%x %X",
				(struct tm*)gmtime(&t));
			return text;

		break;
		case MDB_MEMO:
			return mdb_memo_to_string(mdb, start, size);
		break;
		case MDB_MONEY:
			mdb_money_to_string(mdb, start, text);
			return text;
		break;
		default:
			return "";
		break;
	}
	return NULL;
}

#include "pstore/table.h"

#include "pstore/disk-format.h"
#include "pstore/read-write.h"
#include "pstore/column.h"
#include "pstore/extent.h"
#include "pstore/header.h"
#include "pstore/die.h"
#include "pstore/row.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

struct pstore_table *pstore_table__new(const char *name, uint64_t table_id)
{
	struct pstore_table *self = calloc(sizeof *self, 1);

	if (!self)
		die("out of memory");

	self->name	= strdup(name);
	self->table_id	= table_id;

	return self;
}

void pstore_table__delete(struct pstore_table *self)
{
	unsigned long ndx;

	for (ndx = 0; ndx < self->nr_columns; ndx++) {
		struct pstore_column *column = self->columns[ndx];

		pstore_column__delete(column);
	}

	free(self->columns);
	free(self->name);
	free(self);
}

void pstore_table__add(struct pstore_table *self, struct pstore_column *column)
{
	void *p;

	self->nr_columns++;

	p = realloc(self->columns, sizeof(struct pstore_column *) * self->nr_columns);
	if (!p)
		die("out of memory");

	self->columns = p;

	self->columns[self->nr_columns - 1] = column;
}

struct pstore_table *pstore_table__read(int fd)
{
	struct pstore_file_table f_table;
	struct pstore_table *self;
	uint64_t nr;

	read_or_die(fd, &f_table, sizeof(f_table));

	self = pstore_table__new(f_table.name, f_table.table_id);

	for (nr = 0; nr < f_table.c_index.nr_columns; nr++) {
		struct pstore_column *column = pstore_column__read(fd);

		pstore_table__add(self, column);
	}

	return self;
}

void pstore_table__write(struct pstore_table *self, int fd)
{
	struct pstore_file_table f_table;
	uint64_t start_off, end_off;
	unsigned long ndx;
	uint64_t size;

	start_off = seek_or_die(fd, sizeof(f_table), SEEK_CUR);

	for (ndx = 0; ndx < self->nr_columns; ndx++) {
		struct pstore_column *column = self->columns[ndx];

		pstore_column__write(column, fd);
	}

	end_off = seek_or_die(fd, 0, SEEK_CUR);

	size = end_off - start_off;

	seek_or_die(fd, -(sizeof(f_table) + size), SEEK_CUR);

	f_table = (struct pstore_file_table) {
		.table_id	= self->table_id,

		.c_index	= (struct pstore_file_column_idx) {
			.nr_columns	= self->nr_columns,
			.c_index_next	= PSTORE_END_OF_CHAIN,
		},
	};
	strncpy(f_table.name, self->name, PSTORE_TABLE_NAME_LEN);

	write_or_die(fd, &f_table, sizeof(f_table));

	seek_or_die(fd, size, SEEK_CUR);
}

void pstore_table__import_values(struct pstore_table *self,
				 int fd, struct pstore_iterator *iter,
				 void *private,
				 struct pstore_import_details *details)
{
	struct pstore_row row;
	unsigned long ndx;

	/*
	 * Prepare extents
	 */
	for (ndx = 0; ndx < self->nr_columns; ndx++) {
		struct pstore_column *column = self->columns[ndx];

		column->extent = pstore_extent__new(column, details->comp);

		pstore_extent__prepare_write(column->extent, fd, details->max_extent_len);
	}

	iter->begin(private);

	while (iter->next(private, &row)) {
		for (ndx = 0; ndx < self->nr_columns; ndx++) {
			struct pstore_column *column = self->columns[ndx];
			struct pstore_value value;

			if (!pstore_row__value(&row, column, &value))
				die("premature end of file");

			if (!pstore_extent__has_room(column->extent, &value)) {
				off_t offset;

				pstore_extent__flush_write(column->extent, fd);
				offset = seek_or_die(fd, 0, SEEK_CUR);
				pstore_extent__finish_write(column->extent, offset, fd);
				pstore_extent__prepare_write(column->extent, fd, details->max_extent_len);
			}
			pstore_extent__write_value(column->extent, &value, fd);
		}
	}

	iter->end(private);

	/*
	 * Finish extents
	 */
	for (ndx = 0; ndx < self->nr_columns; ndx++) {
		struct pstore_column *column = self->columns[ndx];

		pstore_extent__flush_write(column->extent, fd);
		pstore_extent__finish_write(column->extent, PSTORE_LAST_EXTENT, fd);
	}
}

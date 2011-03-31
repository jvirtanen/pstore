#include "pstore/read-write.h"
#include "pstore/buffer.h"
#include "pstore/builtins.h"
#include "pstore/core.h"
#include "pstore/row.h"
#include "pstore/segment.h"
#include "pstore/compat.h"
#include "pstore/header.h"
#include "pstore/die.h"

#include <sys/types.h>
#include <inttypes.h>
#include <sys/stat.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>

struct pstore_table_iterator_state {
	int			fd;
	struct pstore_table	*table;

	struct pstore_segment	**segments;
	void			**row;
};

static char *input_file;
static char *output_file;

static void pstore_table_iterator_begin(void *private)
{
	struct pstore_table_iterator_state *iter = private;
	struct pstore_table *table = iter->table;
	unsigned long ndx;

	iter->segments = calloc(sizeof(struct pstore_segment *), table->nr_columns);
	if (!iter->segments)
		die("out of memory");

	for (ndx = 0; ndx < table->nr_columns; ndx++) {
		struct pstore_column *column = table->columns[ndx];

		iter->segments[ndx] = pstore_segment__read(column, iter->fd);
	}

	iter->row = calloc(sizeof(void *), table->nr_columns);
	if (!iter->row)
		die("out of memory");
}

static bool pstore_table_row_value(struct pstore_row *self, struct pstore_column *column, struct pstore_value *value)
{
	struct pstore_table_iterator_state *iter = self->private;
	struct pstore_table *table = iter->table;
	char *str = NULL;
	unsigned long ndx;

	for (ndx = 0; ndx < table->nr_columns; ndx++) {
		if (table->columns[ndx]->column_id == column->column_id)
			str = iter->row[ndx];
	}

	if (!str)
		return false;

	value->s	= str;
	value->len	= strlen(str);

	return true;
}

static struct pstore_row_operations row_ops = {
	.row_value	= pstore_table_row_value,
};

static bool pstore_table_iterator_next(void *private, struct pstore_row *row)
{
	struct pstore_table_iterator_state *iter = private;
	struct pstore_table *table = iter->table;
	unsigned long ndx;

	for (ndx = 0; ndx < table->nr_columns; ndx++)
		iter->row[ndx] = pstore_segment__next_value(iter->segments[ndx]);

	if (!iter->row[0])
		return false;

	*row	= (struct pstore_row) {
		.private	= private,
		.ops		= &row_ops,
	};

	return true;
}

static void pstore_table_iterator_end(void *private)
{
	struct pstore_table_iterator_state *iter = private;
	struct pstore_table *table = iter->table;
	unsigned long ndx;

	for (ndx = 0; ndx < table->nr_columns; ndx++)
		pstore_segment__delete(iter->segments[ndx]);

	free(iter->segments);
	free(iter->row);
}

static struct pstore_iterator pstore_table_iterator = {
	.begin		= pstore_table_iterator_begin,
	.next		= pstore_table_iterator_next,
	.end		= pstore_table_iterator_end,
};

#define BUFFER_SIZE		MiB(128)

static void pstore_table__export_values(struct pstore_table *self, struct pstore_iterator *iter, void *private, int output)
{
	struct pstore_row row;
	struct buffer *buffer;
	unsigned long ndx;

	buffer = buffer__new(BUFFER_SIZE);

	iter->begin(private);

	while (iter->next(private, &row)) {
		for (ndx = 0; ndx < self->nr_columns; ndx++) {
			struct pstore_column *column = self->columns[ndx];
			struct pstore_value value;

			if (!pstore_row__value(&row, column, &value))
				die("premature end of file");

			if (!buffer__has_room(buffer, value.len + 1)) {
				buffer__write(buffer, output);
				buffer__clear(buffer);
			}

			buffer__append(buffer, value.s, value.len);
			if (ndx == self->nr_columns - 1)
				buffer__append_char(buffer, '\n');
			else
				buffer__append_char(buffer, ',');
		}
	}

	iter->end(private);

	buffer__write(buffer, output);

	buffer__delete(buffer);
}

static void export(struct pstore_table *table, int input, int output)
{
	struct pstore_table_iterator_state state;
	unsigned long ndx;

	for (ndx = 0; ndx < table->nr_columns; ndx++) {
		struct pstore_column *column = table->columns[ndx];

		write_or_die(output, column->name, strlen(column->name));
		if (ndx == table->nr_columns - 1)
			write_or_die(output, "\n", 1);
		else
			write_or_die(output, ",", 1);
	}

	state = (struct pstore_table_iterator_state) {
		.fd		= input,
		.table		= table,
	};

	pstore_table__export_values(table, &pstore_table_iterator, &state, output);
}

static void parse_args(int argc, char *argv[])
{
	input_file		= argv[2];
	output_file		= argv[3];
}

static void usage(void)
{
	printf("\n usage: pstore export INPUT OUTPUT\n\n");
	exit(EXIT_FAILURE);
}

int cmd_export(int argc, char *argv[])
{
	struct pstore_header *header;
	int input, output;

	if (argc < 4)
		usage();

	parse_args(argc, argv);

	if (!input_file || !output_file)
		usage();

	input = open(input_file, O_RDONLY);
	if (input < 0)
		die("Failed to open input file '%s': %s", input_file, strerror(errno));

	output = open(output_file, O_WRONLY|O_CREAT|O_TRUNC, S_IRUSR|S_IWUSR);
	if (output < 0)
		die("Failed to open output file '%s': %s", output_file, strerror(errno));

	header = pstore_header__read(input);
	if (header->nr_tables != 1)
		die("number of tables does not match");

	export(header->tables[0], input, output);

	pstore_header__delete(header);

	if (fsync(output) < 0)
		die("fsync");

	if (close(output) < 0)
		die("close");

	if (close(input) < 0)
		die("close");

	return 0;
}
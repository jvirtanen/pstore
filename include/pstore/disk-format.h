#ifndef PSTORE_DISK_FORMAT_H
#define PSTORE_DISK_FORMAT_H

/*
 * This header file contains on-disk data structures of pstore files.
 */

struct pstore_file_header {
	uint64_t		magic;
	uint64_t		n_index_offset;
	uint64_t		t_index_offset;
};

struct pstore_file_table_idx {
	uint64_t		nr_tables;
	uint64_t		t_index_next;
};

struct pstore_file_column_idx {
	uint64_t		nr_columns;
	uint64_t		c_index_next;
};

#define PSTORE_TABLE_NAME_LEN		32

struct pstore_file_table {
	char				name[PSTORE_TABLE_NAME_LEN];
	uint64_t			table_id;
	struct pstore_file_column_idx	c_index;
};

#define PSTORE_COLUMN_NAME_LEN		32

struct pstore_file_column {
	char			name[PSTORE_COLUMN_NAME_LEN];
	uint64_t		column_id;
	uint64_t		type;
	uint64_t		f_offset;
};

struct pstore_file_extent {
	uint64_t		size;
	uint64_t		next_extent;
};

#endif /* PSTORE_DISK_FORMAT_H */

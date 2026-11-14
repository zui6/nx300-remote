#ifndef FIELDS_H_INCLUDED
#define FIELDS_H_INCLUDED

#define MAX_FIELD_ITEMS 256

struct field_item {
    unsigned long addr;
    unsigned int value;
    const char *name;   /* NULL = 语义未知 */
};

struct field_dump {
    int pid;
    int count;
    struct field_item items[MAX_FIELD_ITEMS];
};

int fields_read_all(struct field_dump *out);
const char *field_name(unsigned long addr);

#endif
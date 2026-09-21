#ifndef FAMILY_H
#define FAMILY_H
#include <stdio.h>
#define MAX_PEOPLE 500
#define MAX_NAME 100
#define MAX_DEPTH 64

/* A general tree, represented with a linked list of children.
   Sibling links do NOT mean ancestry; parent links do. */
typedef struct Person {
    unsigned id;
    char name[MAX_NAME + 1];
    struct Person *parent;
    struct Person *first_child;
    struct Person *next_sibling;
} Person;

typedef struct { Person *root; unsigned count, next_id; } Family;
Person *find_id(Person *node, unsigned id);
Person *find_name(Person *node, const char *name);
Person *add_person(Family *tree, unsigned parent_id, const char *name, const char **error);
void undo_add(Family *tree, Person *person);
void free_family(Person *node);
int save_family(const Family *tree, const char *path);
int load_family(Family *tree, const char *path);
void write_tree(FILE *out, const Family *tree);
void write_relations(FILE *out, const Person *person);
void json_string(FILE *out, const char *value);
#endif

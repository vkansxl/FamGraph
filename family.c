#define _POSIX_C_SOURCE 200809L
#include "family.h"
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <ctype.h>
#include <unistd.h>

Person *find_id(Person *node, unsigned id) {
    if (!node) return NULL;
    if (node->id == id) return node;
    for (Person *c = node->first_child; c; c = c->next_sibling) {
        Person *match = find_id(c, id); /* Depth-first search */
        if (match) return match;
    }
    return NULL;
}
Person *find_name(Person *node, const char *name) {
    if (!node) return NULL;
    if (!strcasecmp(node->name, name)) return node;
    for (Person *c = node->first_child; c; c = c->next_sibling) {
        Person *match = find_name(c, name);
        if (match) return match;
    }
    return NULL;
}
Person *add_person(Family *tree, unsigned parent_id, const char *name, const char **error) {
    size_t len = strlen(name);
    if (!len || len > MAX_NAME) { *error = "Use a name between 1 and 100 UTF-8 bytes."; return NULL; }
    for (size_t i = 0; i < len; i++)
        if ((unsigned char)name[i] < 32 || (unsigned char)name[i] == 127) {
            *error = "Names cannot contain control characters."; return NULL;
        }
    if (isspace((unsigned char)name[0]) || isspace((unsigned char)name[len-1])) {
        *error = "Remove spaces at the start or end of the name."; return NULL;
    }
    if (tree->count >= MAX_PEOPLE) { *error = "This demo supports up to 500 people."; return NULL; }
    if (find_name(tree->root, name)) { *error = "That name already exists. Add a surname or initial."; return NULL; }
    Person *parent = NULL;
    if (parent_id) {
        parent = find_id(tree->root, parent_id);
        if (!parent) { *error = "Parent not found."; return NULL; }
        unsigned depth = 1;
        for (Person *p = parent; p->parent; p = p->parent) depth++;
        if (depth > MAX_DEPTH) { *error = "Maximum generation depth reached."; return NULL; }
    } else if (tree->root) { *error = "An originator already exists."; return NULL; }
    Person *p = calloc(1, sizeof *p);
    if (!p) { *error = "Could not allocate memory."; return NULL; }
    p->id = tree->next_id++;
    memcpy(p->name, name, len + 1);
    p->parent = parent;
    if (!parent) tree->root = p;
    else {
        Person **slot = &parent->first_child;
        while (*slot) slot = &(*slot)->next_sibling;
        *slot = p;
    }
    tree->count++;
    return p;
}
void undo_add(Family *tree, Person *p) {
    Person **slot = p->parent ? &p->parent->first_child : &tree->root;
    while (*slot != p) slot = &(*slot)->next_sibling;
    *slot = p->next_sibling;
    tree->count--; tree->next_id--; free(p);
}
void free_family(Person *node) {
    if (!node) return;
    Person *c = node->first_child;
    while (c) { Person *next = c->next_sibling; free_family(c); c = next; }
    free(node);
}
static void save_nodes(FILE *f, const Person *p) {
    if (!p) return;
    fprintf(f, "%u\t%u\t%s\n", p->id, p->parent ? p->parent->id : 0, p->name);
    for (const Person *c = p->first_child; c; c = c->next_sibling) save_nodes(f, c);
}
int save_family(const Family *tree, const char *path) {
    char temp[1024];
    if (snprintf(temp, sizeof temp, "%s.tmp", path) >= (int)sizeof temp) return 0;
    FILE *f = fopen(temp, "w");
    if (!f) return 0;
    fputs("FAMGRAPH1\n", f); save_nodes(f, tree->root);
    int ok = !ferror(f);
    if (fflush(f) != 0) ok = 0;
    if (ok && fsync(fileno(f)) != 0) ok = 0;
    if (fclose(f) != 0) ok = 0;
    if (ok && rename(temp, path) == 0) return 1;
    unlink(temp); return 0;
}
int load_family(Family *tree, const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return errno == ENOENT;
    char line[256]; Family loaded = {NULL, 0, 1}; int ok = 1;
    if (!fgets(line, sizeof line, f) || strcmp(line, "FAMGRAPH1\n")) ok = 0;
    unsigned max_id = 0;
    while (ok && fgets(line, sizeof line, f)) {
        unsigned id, parent; int offset = 0;
        if (!strchr(line, '\n') || sscanf(line, "%u\t%u\t%n", &id, &parent, &offset) != 2 || !offset || !id || id > MAX_PEOPLE || find_id(loaded.root, id)) { ok = 0; break; }
        line[strcspn(line, "\n")] = 0;
        const char *error = NULL;
        Person *p = add_person(&loaded, parent, line + offset, &error);
        if (!p) { ok = 0; break; }
        p->id = id;
        if (id > max_id) max_id = id;
        loaded.next_id = max_id + 1;
    }
    if (ferror(f)) ok = 0;
    fclose(f);
    if (!ok) { free_family(loaded.root); return 0; }
    *tree = loaded; return 1;
}
void json_string(FILE *out, const char *value) {
    fputc('"', out);
    for (const unsigned char *s = (const unsigned char *)value; *s; s++) {
        if (*s == '"' || *s == '\\') { fputc('\\', out); fputc(*s, out); }
        else if (*s < 32) fprintf(out, "\\u%04x", *s);
        else fputc(*s, out);
    }
    fputc('"', out);
}
static void write_nodes(FILE *out, const Person *p, int *first) {
    if (!p) return;
    if (!*first) fputc(',', out);
    *first = 0;
    fprintf(out, "{\"id\":%u,\"parentId\":%u,\"name\":", p->id, p->parent ? p->parent->id : 0);
    json_string(out, p->name); fputc('}', out);
    for (const Person *c = p->first_child; c; c = c->next_sibling) write_nodes(out, c, first);
}
void write_tree(FILE *out, const Family *tree) {
    fprintf(out, "{\"count\":%u,\"nodes\":[", tree->count);
    int first = 1; write_nodes(out, tree->root, &first); fputs("]}", out);
}
static void emit_name(FILE *out, const Person *p, int *first) {
    if (!*first) fputc(',', out);
    *first = 0; json_string(out, p->name);
}
static void emit_descendants(FILE *out, const Person *p, int *first) {
    for (const Person *c = p->first_child; c; c = c->next_sibling) {
        emit_name(out, c, first); emit_descendants(out, c, first);
    }
}
void write_relations(FILE *out, const Person *p) {
    fprintf(out, "{\"id\":%u,\"name\":", p->id); json_string(out, p->name);
    fputs(",\"parent\":[", out); int first = 1;
    if (p->parent) emit_name(out, p->parent, &first);
    fputs("],\"children\":[", out); first = 1;
    for (const Person *c = p->first_child; c; c = c->next_sibling) emit_name(out, c, &first);
    fputs("],\"siblings\":[", out); first = 1;
    if (p->parent) for (const Person *c = p->parent->first_child; c; c = c->next_sibling) if (c != p) emit_name(out, c, &first);
    fputs("],\"grandparent\":[", out); first = 1;
    if (p->parent && p->parent->parent) emit_name(out, p->parent->parent, &first);
    fputs("],\"grandchildren\":[", out); first = 1;
    for (const Person *c = p->first_child; c; c = c->next_sibling)
        for (const Person *g = c->first_child; g; g = g->next_sibling) emit_name(out, g, &first);
    fputs("],\"ancestors\":[", out); first = 1;
    for (const Person *a = p->parent; a; a = a->parent) emit_name(out, a, &first);
    fputs("],\"descendants\":[", out); first = 1; emit_descendants(out, p, &first);
    fputs("]}", out);
}

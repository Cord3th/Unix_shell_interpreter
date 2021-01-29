#ifndef list_h
#define list_h

typedef char ** list;

extern char * path;

list build_list();
void clear_list(list l);
void print_list(list l);

#endif

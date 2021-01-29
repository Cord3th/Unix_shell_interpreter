#ifndef exec_h
#define exec_h

#include "tree.h"

typedef struct backgrndList {   // Структура списка процессов-зомби
    int pid;
    struct backgrndList *next;
} intlist;

int execute(tree);         // Выполняет команды из указанного дерева
void kill_zombies(intlist *);   // Очищает список зомби процессов

#endif

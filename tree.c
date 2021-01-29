#include <stdio.h>
#include <stdlib.h>
#include <setjmp.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include "tree.h"

#define SIZE 16

static char * plex; // Указатель текущей лексемы
void error(int , char * );
static int lex_count = 0; // Индекс слова в списке лексем
static int brckts_count = 0; // Счетчик открывающих скобок
static int * cmd_count = NULL; // Количество команд в скобках
static int conveyor = 0;
int err = 0; // =1, в случае ошибки

// Создает массив для считывания количества комманд в скобках
static void mem_add() {
    brckts_count++;
    if (brckts_count == 1)
        cmd_count = calloc(1, sizeof(int));
    else
        cmd_count = realloc(cmd_count, (brckts_count) * sizeof(int));
    cmd_count[brckts_count - 1] = 0;
    if (brckts_count > 1)
        cmd_count[brckts_count - 2]++;
}

// Очищает память массива для скобок в случае ошибки
static void clear_brackets() {
    if (brckts_count != 0) {
        brckts_count = 0;
        cmd_count = realloc(cmd_count, 0);
        free(cmd_count);
    }
}

// Обнуляет счётчик слов списка, очищает промежуточную память, выставляет err = 1
static void error_treatment() {
    lex_count = 0;
    clear_brackets();
    err = 1;
}

static int symset(char * s) {
    return (strcmp(s, "&") != 0) &&
           (strcmp(s, "&&") != 0) &&
           (strcmp(s, "|") != 0) &&
           (strcmp(s, "||") != 0) &&
           (strcmp(s, ";") != 0) &&
           (strcmp(s, ">") != 0) &&
           (strcmp(s, ">>") != 0) &&
           (strcmp(s, "<") != 0) &&
           (strcmp(s, "(") != 0) &&
           (strcmp(s, ")") != 0);
}
// Создает дерево из одного элемента, обнуляет все поля
static tree make_cmd() {
    tree t = NULL;
    t = calloc(1, sizeof(*t));
    t->argv = NULL;
    t->infile = NULL;
    t->outfile = NULL;
    t->append = 0;
    t->backgrnd = 0;
    t->psubcmd = NULL;
    t->pipe = NULL;
    t->next = NULL;
    return t;
}

// Добавляет очередной элемент в массив argv текущей команды
static void add_arg(tree t) {
    int i = 0;
    if (t->argv == NULL)
        t->argv = calloc(1, sizeof(*t->argv));
    while (t->argv[i] != NULL)
        i++;
    long temp = strlen(
        plex);
    t->argv[i] = calloc(temp + 1, sizeof(char));
    strncpy(t->argv[i++], plex, temp + 1);
    t->argv = realloc(t->argv, ((i + 1) * sizeof(*t->argv)));
    t->argv[i] = NULL;
}

// Печать дерева с вспомогательными функциями
static void make_shift(int n) {
    while (n--)
        putc(' ', stderr);
}

static void print_argv(char **p, int shift) {
    char **q = p;
    if (p != NULL)
        while (*p != NULL) {
            make_shift(shift);
            fprintf(stderr, "argv[%d]=%s\n",(int) (p - q), *p);
            p++;
        }
}

void print_tree(tree t, int shift) {
    char **p;
    if (t == NULL)
        return;
    p = t->argv;
    if (p != NULL)
        print_argv(p, shift);
    else {
        make_shift(shift);
        fprintf(stderr, "psubshell\n");
    }
    make_shift(shift);
    if (t->infile == NULL)
        fprintf(stderr, "infile=NULL\n");
    else
        fprintf(stderr, "infile=%s\n", t->infile);
    make_shift(shift);
    if (t->outfile == NULL)
        fprintf(stderr, "outfile=NULL\n");
    else
        fprintf(stderr, "outfile=%s\n", t->outfile);
    make_shift(shift);
    fprintf(stderr, "append=%d\n", t->append);
    make_shift(shift);
    fprintf(stderr, "background=%d\n", t->backgrnd);
    make_shift(shift);
    fprintf(stderr, "type=%s\n", t->type == NXT ? "NXT": t->type ==OR ? "OR": "AND");
    make_shift(shift);
    if(t->psubcmd == NULL)
        fprintf(stderr, "psubcmd=NULL \n");
    else{
        fprintf(stderr, "psubcmd---> \n");
        print_tree(t->psubcmd, shift + 5);
    }
    make_shift(shift);
    if(t->pipe == NULL)
        fprintf(stderr, "pipe=NULL \n");
    else{
        fprintf(stderr, "pipe---> \n");
        print_tree(t->pipe, shift + 5);
    }
    make_shift(shift);
    if(t->next == NULL)
        fprintf(stderr, "next=NULL \n");
    else {
        fprintf(stderr, "next---> \n");
        print_tree(t->next, shift + 5);
    }
}

// Удаляет дерево с высвобождением памяти
void clear_tree(tree t) {
    if (t == NULL)
        return;
    clear_tree(t->next);
    clear_tree(t->pipe);
    clear_tree(t->psubcmd);
    free(t->infile);
    free(t->outfile);
    clear_list(t->argv);
    free(t);
    t = NULL;
}

tree build_tree(list l) {
    tree beg_cmd;
    tree prev_cmd;
    tree cur_cmd;
    if (!l)
        return NULL;
    else
        plex = l[lex_count++];
    beg_cmd = cur_cmd = make_cmd();
    prev_cmd = cur_cmd;
    // Проверяем синтаксическую ошибку
    if ((strcmp(plex, ")") == 0) || (strcmp(plex, "<") == 0)
       || (strcmp(plex, ">") == 0) || (strcmp(plex, ">>") == 0)
       || (strcmp(plex, ";") == 0) || (strcmp(plex, "&") == 0)
       || (strcmp(plex, "||") == 0) || (strcmp(plex, "&&") == 0)
       || (strcmp(plex, "|") == 0)) {
           error_treatment();
           error(2, "Unexpected symbol.");
           return cur_cmd;
    }
    else if (strcmp(plex, "(") == 0) {
        // Проверям возможную ошибку
        if (l[lex_count] == NULL) {
            error_treatment();
            error(2, "Missing ) .");
            return cur_cmd;
        }
        mem_add();
        cur_cmd = build_tree(l);
        prev_cmd->psubcmd = cur_cmd;
        cur_cmd = prev_cmd;
    }
    else {
       if ((brckts_count > 0) && (cur_cmd->argv == NULL))
           cmd_count[brckts_count - 1]++;
        add_arg(cur_cmd);
    }
    plex = l[lex_count++];
    while (plex && !err) {
        // Проверяемм возможные ошибки
        if (((strcmp(plex, "||") == 0) || (strcmp(plex, "&&") == 0)
            || (strcmp(plex, "|") == 0)) && (l[lex_count] == NULL)) {
            error_treatment();
            error(2, "Missing command after ||, && or | .");
            return cur_cmd;
        }
        else if (strcmp(plex, "(") == 0) {
            error_treatment();
            error(2, "No command separator before ( .");
        }
        // Если попалась закрывающая скобка, возвращаемся из subshell
        else  if (strcmp(plex, ")") == 0) {
            // Проверяем возможные ошибки
            if ((brckts_count != 0) && (cmd_count[brckts_count - 1] == 0)) {
                error_treatment();
                error(2, "No commands in brackets.");
            }
            else if (brckts_count == 0)  {
                error_treatment();
                error(2, "Missing ( .");
            }
            // Возвращаясь, очищаем память
            if ((brckts_count > 0) && (cmd_count[brckts_count - 1] > 1)) {
                cmd_count[brckts_count - 1]--;
                return beg_cmd;
            }
            else if ((brckts_count > 0) && (cmd_count[brckts_count - 1] == 1)) {
                cmd_count[brckts_count - 1] = 0;
                cmd_count = realloc(cmd_count, (brckts_count - 1) * sizeof(int));
                if (brckts_count == 1)
                    free(cmd_count);
                brckts_count--;
                return beg_cmd;
            }
        }
        else if (strcmp(plex, "<") == 0) {
            if (l[lex_count] == NULL)  {
                error_treatment();
                error(2, "Missing argument after < .");
                return cur_cmd;
            }
            plex = l[lex_count++];
            if (cur_cmd->infile != NULL) {
                error_treatment();
                error(2, "Only one input file is allowed.");
                return cur_cmd;
            }
            if (symset(plex)) {
                long temp = strlen(plex);
                cur_cmd->infile = calloc(temp + 1, sizeof(char));
                strncpy(cur_cmd->infile, plex, temp + 1);
                plex = l[lex_count++];
            }
            else {
                error_treatment();
                error(2, "Unexpected symbol.");
                return cur_cmd;
            }
        }
        else if ((strcmp(plex, ">") == 0) || (strcmp(plex, ">>") == 0)) {
            if (cur_cmd->outfile != NULL) {
                error_treatment();
                error(2, "Only one output file is allowed.");
                return cur_cmd;
            }
            if (l[lex_count] == NULL)  {
                error_treatment();
                error(2, "Nothing after > (>>) .");
                return cur_cmd;
            }
            if (strcmp(plex, ">>") == 0)
                cur_cmd->append = 1;
            plex = l[lex_count++];
            if (symset(plex)) {
                long temp = strlen(plex);
                cur_cmd->outfile= calloc(temp + 1, sizeof(char));
                strncpy(cur_cmd->outfile, plex, temp + 1);
                plex = l[lex_count++];
            }
            else {
                error_treatment();
                error(2, "Unexpected symbol.");
                return cur_cmd;
            }
        }
        else if (((strcmp(plex, ";") == 0) || (strcmp(plex, "||") == 0)
                || (strcmp(plex, "&&") == 0)) && (conveyor > 0) && (brckts_count == 0)) {
               return(beg_cmd);
        }
        else if (strcmp(plex, "&&") == 0) {
            cur_cmd = build_tree(l);
            prev_cmd->next = cur_cmd;
            prev_cmd->type = AND;
            prev_cmd = cur_cmd;
        }
        else if (strcmp(plex, "||") == 0) {
            cur_cmd = build_tree(l);
            prev_cmd->next = cur_cmd;
            prev_cmd->type = OR;
            prev_cmd = cur_cmd;
        }
        else if (strcmp(plex, ";") == 0) {
            if (l[lex_count] && (strcmp(l[lex_count], ")") != 0)) {
                cur_cmd = build_tree(l);
                prev_cmd->next = cur_cmd;
                prev_cmd->type = NXT;
                prev_cmd = cur_cmd;
            }
            else {
                if (l[lex_count])
                    plex = l[lex_count++];
                else
                    plex = l[lex_count];
            }
        }
        else if (strcmp(plex, "&") == 0) {
            if (l[lex_count] && (strcmp(l[lex_count], ")") != 0)) {
                cur_cmd = build_tree(l);
                prev_cmd->backgrnd = 1;
                prev_cmd->next = cur_cmd;
                prev_cmd->type = NXT;
                prev_cmd = cur_cmd;
            }
            else {
                prev_cmd->backgrnd = 1;
                if (l[lex_count])
                    plex = l[lex_count++];
                else
                    plex = l[lex_count];
            }
        }
        else if (strcmp(plex, "|") == 0) {
            conveyor++;
            cur_cmd = build_tree(l);
            conveyor--;
            prev_cmd->pipe = cur_cmd;
            if (cur_cmd->backgrnd == 1)
                prev_cmd->backgrnd = 1;
            if ((conveyor > 0) && (brckts_count == 0))
                return(beg_cmd);
        }
        else {
            add_arg(cur_cmd);
            plex = l[lex_count++];
        }
    }
    if ((brckts_count != 0) && !plex) {
        error_treatment();
        error(2, "Missing ) .");
        return cur_cmd;
    }
    lex_count = 0;
    return beg_cmd;
}

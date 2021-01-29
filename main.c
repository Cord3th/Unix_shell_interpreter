#include <stdio.h>
#include <setjmp.h>
#include "list.h"
#include "tree.h"
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include "exec.h"

list l;
extern int err;
tree t = NULL;
char * path;
sigjmp_buf begin;

void inv();

void sigret(int s) {
    putchar('\n');
    l = NULL;
    t = NULL;
    siglongjmp(begin, 1);
}

extern void error(int n, char * message) {
    if (n == 1)
        fprintf(stderr, "\nLexis error. Please enter the correct data.\n");
    else if (n == 2)
        fprintf(stderr, "\nSyntax error. %s\n", message);
    return;
}

int main(int argc, char * argv[]) {
    path = argv[0];
    signal(SIGINT, sigret);
    while (1) {
        sigsetjmp(begin, 1);
        clear_list(l);
        clear_tree(t);
        err = 0;
        inv();
        l = build_list();
        /*if (!err && l) {
            printf("The list:\n");
            print_list(l);
        }*/
        if (feof(stdin))
            break;
        t = build_tree(l);
        if (!err && t) {
            //printf("The tree:\n");
            //print_tree(t, 1);
            execute(t);
        }
    }
    printf("zdarpva\n");
    clear_list(l);
    return 0;
}

void inv() {
    printf("%s", "\x1b[32m");  // здесь изменяется цвет на зеленый
    char s[100];
    gethostname(s, 100);
    printf("%s@%s", getenv("USER"), s);
    printf("%s", "\x1B[34m");   // здесь изменяется цвет на серый
    getcwd(s, 100);
    printf(":%s$ ", s);
    printf("%s", "\x1B[37m");
}

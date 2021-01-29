#include "list.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <libgen.h>
#include <setjmp.h>
#include <signal.h>

#define SIZE 16

static int c; //Текущий символ
int err; //Индикатор ошибки
char * path; // Путь для обработки $SHELL
list l; //Список слов (в виде массива)
void error(int , char * ); // Функция ошибок
static char * buf; //Буфер для накопления текущего слова
static int sizebuf; //Размер буфера текущего слова
static int sizelist; //Размер списка слов
static int quote, slash; // Индикаторы обатного слэша и кавычек
static int curbuf; //Индекс текущего символа в буфере
static int curlist; //Индекс текущего слова в списке
static char str[SIZE];//Статический массив для считывания
static int strcount = 0;//Счётчик элемента статическог/о массива

/*Получает из str очередной символ; если массив str исчерапался,
то считывает в str очередную порцию данных и выдает
следующий символ*/
static char getsym() {
    if (strcount == SIZE) {
        fgets(str, SIZE + 1, stdin);
        strcount = 0;
    }
    if (feof(stdin))
        return EOF;
    return str[strcount++];
}

/*Освобождает память, занимаемую списком (если он не пуст), и
делает список пустым. Переменную sizelist (размер списка) обнуляет, переменную
curlist, указывающую очередную свободную позицию в списке, тоже обнуляет*/
void clear_list(list l) {
    int i;
    sizelist = curlist = 0;
    if (l == NULL) return;
    for (i = 0; l[i] != NULL; i++)
        free(l[i]);
    free(l);
}

/*Присваивает переменной l, представляющую список, значение
NULL (пустой список). Переменную sizelist (размер списка) обнуляет, переменную
curlist, указывающую очередную свободную позицию в списке, тоже обнуляет*/
static void null_list() {
    sizelist = curlist = 0;
    l = NULL;
}

/*Завершает список, добавляя NULL,
обрезает память, занимаемую списком, до точного размера*/
static void term_list() {
    if (l == NULL) return;
    if (curlist > sizelist - 1)
        l = realloc(l, (sizelist+1) * sizeof(*l));
    l[curlist] = NULL;
  //Выравниваем используемаю под список память точно по размеру списку
    l = realloc(l, (sizelist  = curlist + 1) * sizeof(*l));
}

/*Присваивает переменной buf значение NULL, переменной sizebuf
(размер буфера) присваивает значение 0, переменной curbuf, указывающей очередную
свободную позицию в буфере, присваивает значение 0*/
static void nullbuf() {
    buf = NULL;
    sizebuf = curbuf = 0;
}

/*Добавляет очередной символ в буфер в позицию curbuf, после чего
переменная curbuf увеличивается на 1. Если буфер был пуст, то он создается. Если
размер буфера превышен, то он увеличивается на константу SIZE, заданную директивой
define*/
static void addsym() {
  if (curbuf > sizebuf - 1)
    //Увеличиваем буфер при необходимости
    buf = realloc(buf, sizebuf += SIZE);
  buf[curbuf++] = c;
}

/*Завершает текущее слово в буфере, добавляя ’\0’ в позицию curbuf
(увеличив, если нужно, буфер), и обрезает память, занимаемую словом, до точного
размера; затем добавляет слово в список в позицию curlist, после чего значение
curlist увеличивается на 1. Если список был пуст, то он создается. Если размер списка
превышен, то он увеличивается на константу SIZE*/
static void addword() {
    if (curbuf > sizebuf - 1)
        //для записи ’\0’ увеличиваем буфер при необходимости
        buf = realloc(buf, sizebuf += 1);
    buf[curbuf++] = '\0';
    if (buf[0] == '$') {
        if (strcmp(buf, "$EUID") == 0) {
            free(buf);
            nullbuf();
            unsigned long temp = geteuid();
            sizebuf = curbuf = snprintf(NULL, 0, "%lu", temp);
            sizebuf++;
            buf = malloc(curbuf + 1);
            snprintf(buf, curbuf + 1, "%lu", temp);
        }
        else if ((strcmp(buf, "$HOME") == 0)
                || (strcmp(buf, "$SHELL") == 0)
                || (strcmp(buf, "$USER") == 0)) {
            char *temp;
            char save;
            int i = 1;
            if (buf[1] == 'H')
                temp = getenv("HOME");
            else if (buf[1] == 'S')
                temp = path;
            else if (buf[1] == 'U')
                temp = getlogin();
            free(buf);
            nullbuf();
            save = c;
            for (i = 0; (c = temp[i]) != '\0'; i++)
                addsym();
            c = save;
        }
    }
    if (quote) {
            nullbuf();
            term_list();
            error(1, NULL);
    }
    if (curbuf > sizebuf - 1)
        //для записи ’\0’ увеличиваем буфер при необходимости
        buf = realloc(buf, sizebuf += 1);
    buf[curbuf++] = '\0';
    //Выравниваем используемую память точно по размеру слова
    buf = realloc(buf, sizebuf = curbuf);
    if (curlist > sizelist - 1)
        //Увеличиваем массив под список при необходимости
        l = realloc(l, (sizelist += SIZE) * sizeof(*l));
    l[curlist++] = buf;
    nullbuf();
}

/*Печатает элементы списка слов*/
void print_list(list l) {
    if (l == NULL) return;
    while (*l != NULL)
        printf("%s\n", *l++);
    putchar('\n');
}

/*Проверяет очередной символ на табуляцию, перевод строки,
пробел, спец. символ или конец файла*/
static int symset(char c) {
    return c != '\n' &&
           c != '\t' &&
           c != ' ' &&
           c != '>' &&
           c != '<' &&
           c != '&' &&
           c != ';' &&
           c != '|' &&
           c != '(' &&
           c != ')' &&
           c != '#' &&
           c != EOF;
}

/*Проверяет, является ли символ ошибочным*/
static int check(char c) {
    char errmas[81] = "#\n\t \\><&;|()-1234567890qwertyuiopasdfghjklzxcvbnmQWERTYUIOPASDFGHJKLZXCVBNM$_/.";
    errmas[79] = '"';
    errmas[80] = EOF;
    for (int i = 0; i < 81; i++)
        if (errmas[i] == c)
            return 0;
    return 1;
}

static void destruct() {
    int getchar_needed = 1;
    for (int i = 0; i < SIZE; i++) {
        if ((str[i] == '\n') || (str[i] == '\0')) {
            getchar_needed = 0;
            break;
        }
    }
    if (getchar_needed)
        while (((c = getchar()) != EOF) && (c != '\n'));
    rewind(stdin);
}

/*Строит список*/
list build_list() {
    typedef enum {start, word, greater, greater2, stop, erro} vertex;
    vertex V = start;
    char cprev;
    quote = slash = strcount = 0;
    fgets(str, SIZE + 1, stdin);
    c = getsym();
    null_list();
    while (1)
        switch (V) {
            case start:
                if (c == '#' && !slash && !quote) {
                    destruct();
                    V = stop;
                }
                else if (c == EOF || c == '\n')
                    V = stop;
                else if (slash == 1) {
                    addsym();
                    c = getsym();
                    slash = 0;
                    V = start;
                }
                else if (c == '\\' && slash == 0) {
                    slash = 1;
                    c = getsym();
                }
                else if (c == '"')
                    if (!quote) {
                        quote = 1;
                        c = getsym();
                        V = (c == '\n' ||
                             c == EOF) ? erro : word;
                        cprev = c;
                    }
                    else {
                        quote = 0;
                        c = getsym();
                    }
                else if (c == ' ' || c == '\t')
                    c = getsym();
                else if (check(c))
                    V = erro;
                else {
                    cprev = c;
                    nullbuf();
                    addsym();
                    V = (cprev == '>' ||
                         cprev == '&' ||
                         cprev == '|') ? greater : word;
                    c = getsym();
                }
            break;

            case word:
                if (check(c) && !quote) {
                    V = erro;
                    buf = realloc(buf, 0);
                }
                else if (c == '"') {
                    if (quote) {
                        quote = 0;
                        V = start;
                        c = getsym();
                        addword();
                    }
                    else {
                            addword();
                            V = erro;
                    }
                }
                else if (symset(c) || quote) {
                    if (!symset(cprev)) {
                        addword();
                        V = start;
                    }
                    else {
                        addsym();
                        c = getsym();
                    }
                }
                else {
                    V = start;
                    addword();
                }
            break;

            case greater:
                if (check(c))
                    V = erro;
                else if (c == cprev) {
                    addsym();
                    V = greater2;
                    c = getsym();
                }
                else {
                    V = start;
                    addword();
                }
            break;

            case greater2:
                V = start;
                addword();
            break;

            case stop:
                term_list();
                return l;
            break;

            case erro:
                err = 1;
                destruct();
                nullbuf();
                term_list();
                error(1, NULL);
                return l;
      }
}

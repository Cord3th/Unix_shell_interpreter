#include "exec.h"
#include <stdio.h>
#include <stdlib.h>
#include <setjmp.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>

#define SIZE 16

int beg_pid; // ID исходного процесса

// Завершает процесс в случае прерывания
void sig_kill(int s) {
    int pid;
    pid = getpid();
    fprintf(stderr, "\n");
    if (beg_pid != pid) {
        exit(1);
    }
}

// Реализация сd
int exec_cd(char * argv[]) {
    char * s;
    // Если аргументов нет, переходим домашнюю папку
    if (argv[1] == NULL) {
        s = getenv("HOME");
        if (s == NULL) {
            fprintf(stderr, "Can't find home folder.\n");
            return 1;
        }
        else
            chdir(s);
    }
    // Если аргументов больше одного, ошибка
    else if (argv[2] != NULL)  {
        fprintf(stderr, "Command cd can't have more than one argument.\n");
        return 1;
    }
    // Перейдем в каталог argv[1] или сообщим об ошибке
    else if (chdir(argv[1])) {
        perror(argv[1]);
        return 1;
    }
    return 0;
}
// Реализация pwd
int exec_pwd(tree T) {
    char * path;
    int lack = 1;
    // Если есть аргументы, сообщим об ошибке
    if (T->argv[1] != NULL) {
        fprintf(stderr, "Command pwd can't have arguments.\n");
        return 1;
    }
    // Запишем наш каталог в path
    path = (char *) calloc(SIZE, (sizeof(char)));
    getcwd(path, lack++ * SIZE);
        while (path[0] == '\0') {
            path = realloc(path, SIZE * lack * sizeof(char));
            getcwd(path, lack++ * SIZE);
        }
    // Печать и освобождение памяти
    fprintf(stdout, "%s\n", path);
    free(path);
    exit(0);
}

// Выполнение команды
int exec_cmd(tree T) {
    int in, out = 0, res = 0; //in, out - файловые дескрипторы, res - результат выполнения процесса
    // Если команда открыта в фоновом режиме, то игнорируем сигнал прерывания
    if (T->backgrnd == 1)
        signal(SIGINT, SIG_IGN);
    else
       signal(SIGINT, SIG_DFL);
    if  (T->infile != NULL) {
        if ((in = open(T->infile, O_RDONLY)) < 0) {
            // Сообщим об ошибке если не удалось прочесть такой файл
            fprintf(stderr, "Input file %s doesn't exist.\n", T->infile);
            exit(1);
        }
        dup2(in, 0);
        close(in);
    }
    // Добавим источник вывода
    if  (T->outfile != NULL) {
        if (T->append == 1) {
            if ((out = open(T->outfile, O_WRONLY | O_CREAT |O_APPEND, 0777)) < 0) {
                fprintf(stderr, "Not enough rights to write in file %s .\n", T->outfile);
                exit(1);
            }
        }
        else if ((out = open(T->outfile, O_WRONLY | O_CREAT |O_TRUNC, 0777)) < 0) {
                fprintf(stderr, "Not enough rights to write in file %s .\n", T->outfile);
                exit(1);
            }
        dup2(out, 1);
        close(out);
    }
    // Если команда открыта в фоновом режиме, подадим ей на вход EOF
    if (T->backgrnd == 1) {
        int f = open("/dev/null", O_RDONLY);
        dup2(f, 0);
        if (fork() == 0) {
            execvp(T->argv[0], T->argv);
            exit(0);
        }
        else
            kill(getpid(), SIGKILL);
    }
    // Если это сабшелл, запустим его
    if (T->psubcmd != NULL) {
        res = execute(T->psubcmd);
        if (res != 0)
            exit(1);
        else
            exit(0);
    }
    // Проверим, не является ли argv[0] встроенной командой pwd
    else if ((T->argv != NULL) && (strcmp(T->argv[0], "pwd") == 0)) {
            res = exec_pwd(T);
        exit(res);
    }
    // Просто выполним команду
    else {
        execvp(T->argv[0], T->argv);
        fprintf(stderr, "Command %s is not found.\n", T->argv[0]);
        exit(1);
    }
}

// Выполняет конвейер команд по построенному дереву
int exec_conv(tree T) {
    tree P = T;
    int pid = 0;      // ID данного и родительского процесса
    int fd[2], old = -1;    // Переменные для преобразования файловых дескрипторов
    int status = 0;         // Статус завершения процесса
    // Изменим обработку сигнала прерывания
    signal(SIGINT, sig_kill);
    // Обработаем все команды конвейера
    while (P != NULL) {
        // Проверим, не является ли argv[0] встроенной командой cd
        if ((P->argv != NULL) && (strcmp(P->argv[0], "cd") == 0)) {
            if (old != -1)
                close(old);
            // Cлучай последней команды в конвеере
            if (P->pipe != NULL) {
                if (pipe(fd) < 0)
                    exit(1);
                // Закроем открытые старые дескрипторы
                old = fd[0];
                close(fd[1]);

            }
            // Вернем результат exec_cd
            status = exec_cd(T->argv);
            return status;
        }
        // Проверим, не является ли argv[0] встроенной командой exit
        else if ((P->argv != NULL) && (strcmp(P->argv[0], "exit") == 0)) {
            // Выйдем из процесса
            if (T->argv[1] != NULL)
                fprintf(stderr, "\nexit can't have arguments. Exiting anyway.");
            exit(0);
        }
        // Первая команда конвеера
        if (P == T) {
            // Но не последняя
            if (P->pipe != NULL) {
                if (pipe(fd) < 0)
                    exit(1);
                // Создание копии процесса, перенаправление ввода-вывода и замена тела процесса
                if ((pid = fork()) == 0) {
                    dup2(fd[1], 1);
                    close(fd[0]);
                    close(fd[1]);
                    exec_cmd(P);
                }
                // Закроем открытые старые дескрипторы
                old = fd[0];
                close(fd[1]);
            }
            // Первая и последняя
            // Создание копии процесса и замена тела процесса
            else if ((pid = fork()) == 0)
                exec_cmd(P);
        }
        // Не первая, но последняя команда
        else if (!P->pipe) {
            // Создание копии процесса, перенаправление ввода-вывода и замена тела процесса
            if ((pid = fork()) == 0) {
                dup2(old, 0);
                close(old);
                exec_cmd(P);
            }
            // Закроем старый файловый дескриптор
            close(old);
        }
        // Команда не первая и не последняя
        else {
            // Выйдем если не удастся перенаправить ввод-вывод
            if (pipe(fd) < 0)
                exit(1);
            // Создание копии процесса, перенаправление ввода-вывода и замена тела процесса
            if ((pid = fork()) == 0) {
                dup2(old, 0);
                dup2(fd[1], 1);
                close(fd[0]);
                close(fd[1]);
                close(old);
                exec_cmd(P);
            }
            // Закроем открытые старые дескрипторы
            close(fd[1]);
            close(old);
            old = fd[0];
        }
        P = P->pipe;
    }
    // Получение статуса завершенного процесса (последнего)
    while (wait(&status) != -1);
    return status;
}

// Выполняет команды по заданному дереву
int execute(tree T) {
    int res;
    tree P = T;
    beg_pid = getpid();
    // Пока дерево Р не закончится, будем выполнять конвейеры команд
    while (P != NULL) {
        res = exec_conv(P);
            //  Проверим условия && и ||
        if ((P->type == AND) && (res != 0))
            while ((P->next != NULL) && (P->type == AND))
                    P = P->next;
        else if ((P->type == OR) && (res == 0))
            while ((P->next != NULL) && (P->type == OR))
                P = P->next;
        P = P->next;
    }
    return 0;
}

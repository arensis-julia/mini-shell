#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define MAXARGS 128
#define MAXLINE 2048

void eval(char * cmdline);
int parseline(char * buf, char ** argv);
int builtIn_command(int argc, char ** argv);
void execute(int argc, char ** argv);
int redir(int argc, char ** argv);
int findPipe(int argc, char ** argv);

void my_exit(int argc, char ** argv);
void my_cd(int argc, char ** argv);
void my_pwd(int argc, char ** argv);
void my_rm(int argc, char ** argv);
void my_mv(int argc, char ** argv);
void my_cp(int argc, char ** argv);
void my_cat(int argc, char ** argv);
void my_head(int argc, char ** argv);
void my_tail(int argc, char ** argv);

int main() {
    char cmdline[MAXLINE];

    signal(SIGINT, SIG_IGN);
    signal(SIGTSTP, SIG_IGN);

    while(1) {
        /* READ */
        printf("> ");
        fgets(cmdline, MAXLINE, stdin);
        if(feof(stdin))     exit(0);

        /* EVALUATE */
        eval(cmdline);
    }
}

/* EVAL - evaluate a command line */
void eval(char * cmdline) {
    char * argv[MAXARGS];       // argument list
    char buf[MAXLINE];          // modified command line
    int argc = 0;               // num of args
    int bg;                     // background & foreground flag (if bg == 1, the process will be operated on background)
    int i;
    pid_t pid;                  // process id

    strcpy(buf, cmdline);
    bg = parseline(buf, argv);

    if(argv[0] == NULL)     return;         // ignore empty lines
    for(i=0; argv[i]; i++)  argc++;         // count argc

    if(!builtIn_command(argc, argv)) {
        if((pid = fork()) == 0)     execute(argc, argv);
        waitpid(pid, 0, 0);
        
        if(bg)  printf("%d %s", pid, cmdline);
    }

    return;
}

/* PARSELINE - parse the command line and build argv */
int parseline(char * buf, char ** argv) {
    char * delim;       // points to first space delimiter
    char * end;         // end of string bind by " or '
    int argc;           // num of args
    int br;             // bracket flag
    int bg;             // background & foreground flag

    buf[strlen(buf) - 1] = ' ';                 // replace '\n' with ' '
    while(*buf && (*buf == ' '))    buf++;      // ignore leading space

    // organize command line with <, |, > to wanted format
    delim = buf;
    while((delim = strpbrk(delim, "<|>"))) {
        end = delim;
        delim = buf;
        br = 0;
        
        while(*delim && delim < end) {                  // if number of " or ' odd number, than bg == 0
            if(*delim == '\"')  br ^= 1;
            if(*delim == '\'')  br ^= 2;
            delim++;
        }
        
        delim = end;
        if(*delim == '>' && *(delim+1) == '>') {        // >>
            // e.g. ./hello >> "hello.txt"
            if(((buf <= delim-1) && (delim+2 <= &buf[MAXLINE-1]) && (*(delim-1) == ' ') && (*(delim+2) == ' ')) || br) {
                delim += 2;
                continue;
            }

            // e.g. ./hello>>"hello.txt" --> ./hello >> "hello.txt"
            while(*end && end < &buf[MAXLINE-4])    end++;
            while(delim <= end) {
                *(end+2) = *end;
                end--;
            }
            *(delim+2) = *(delim+1) = *delim;   // place >> into center
            *delim = *(delim+3) = ' ';          // place spaces at both sides
            delim += 4;
        }

        else {
            if(((buf <= delim-1) && (delim+1 <= &buf[MAXLINE-1]) && (*(delim-1) == ' ') && (*(delim+1) == ' ')) || br) {
                delim++;
                continue;
            }

            while(*end && end < &buf[MAXLINE-3]) end++;
            while(delim <= end) {
                *(end+2) = *end;
                end--;
            }
            *(delim+1) = *delim;                // place |, >, < into center
            *delim = *(delim+2) = ' ';          // place spaces at both sides
            delim += 3;
        }
    }

    // build argv
    argc = 0;
    while((delim = strpbrk(buf, " \"\'"))) {
        if(*delim == '\"') {
            // ignore "
            for(end=delim; *end; end++)
                *end = *(end + 1);
            
            end = delim++;
            while(*end && (*end != '\"'))   end++;
            if(!*end)   {
                fprintf(stderr, "swsh: missing character \"\n");
                return 1;
            }
            
            argv[argc++] = buf;
            *end = '\0';
            buf = end + 1;
            while(*buf && (*buf == ' '))    buf++;
        }

        else if(*delim == '\'') {
            // ignore '
            for(end=delim; *end; end++)
                *end = *(end + 1);
            
            end = delim++;
            while(*end && (*end != '\''))   end++;
            if(!*end)   {
                fprintf(stderr, "swsh: missing character \"\n");
                return 1;
            }

            argv[argc++] = buf;
            *end = '\0';
            buf = end + 1;
            while(*buf && (*buf == ' '))    buf++;
        }
    }

    argv[argc] = '\0';
    if(argc == 0)   return 1;                   // ignore blank line
    if((bg = (*argv[argc-1] == '&')) != 0)
        argv[--argc] = '\0';
    
    return bg;
}

/* BUILTIN_COMMAND - built in commands*/
int builtIn_command(int argc, char ** argv) {
    if(!strcmp(argv[0], "quit"))    exit(0);
    if(!strcmp(argv[0], "&"))       return 1;       // ignore singleton &
    if(!strcmp(argv[0], "exit")) {
        my_exit(argc, argv);
        return 1;
    }
    if(!strcmp(argv[0], "cd")) {
        my_cd(argc, argv);
        return 1;
    }

    return 0;
}

/* EXECUTE - execute commands using pipe */
void execute(int argc, char ** argv) {
    pid_t pid;
    int k, i;
    int tmp;
    int pi[2];

    if(pipe(pi) < 0) {
        fprintf(stderr, "swsh: pipe failed\n");
        exit(-1);
    }

    if((k = findPipe(argc, argv)) > 0)      pid = fork();
    else {
        k = 0;
        pid = 1;
    }

    if(pid == 0) {
        argc = k;

        close(pi[0]);
        dup2(pi[1], 1);
        close(pi[1]);
        
        execute(argc, argv);

        exit(0);
    }
    else {
        if(k) {
            close(pi[1]);
            dup2(pi[0], 0);
            close(pi[0]);

            argc -= k+1;
            argv += k+1;

            waitpid(pid, 0, 0);
        }

        tmp = redir(argc, argv);
        if(tmp < 0)     exit(-1);
        else            argc = tmp;

        if(!strcmp(argv[0], "pwd"))         my_pwd(argc, argv);
        else if(!strcmp(argv[0], "rm"))     my_rm(argc, argv);
        else if(!strcmp(argv[0], "mv"))     my_mv(argc, argv);
        else if(!strcmp(argv[0], "cp"))     my_cp(argc, argv);
        else if(!strcmp(argv[0], "cat"))    my_cat(argc, argv);
        else if(!strcmp(argv[0], "head"))   my_head(argc, argv);
        else if(!strcmp(argv[0], "tail"))   my_tail(argc, argv);
        else if(execvp(argv[0], argv) < 0) {
            fprintf(stderr, "swsh: exec failed\n");
            exit(-1);
        }

        exit(0);
    }
}

/* REDIR - check if the command line contains redirection */
int redir(int argc, char ** argv) {
    int fd, i, j;

    for(i=1; i<argc; i++) {
        if(!strcmp(argv[i], "<")) {
            if(i < argc - 1) {
                if((fd = open(argv[i+1], O_RDONLY)) < 0) {
                    fprintf(stderr, "swsh: No such file\n");
                    return -1;
                }
                dup2(fd, 0);
                close(fd);
            }
            else {
                fprintf(stderr, "swsh: Missing argument after '<'\n");
                return -1;
            }
        }

        if(!strcmp(argv[i], ">")) {
            if(i < argc - 1) {
                if((fd = open(argv[i+1], O_WRONLY | O_CREAT | O_TRUNC, 0755)) < 0) {
                    fprintf(stderr, "swsh: %s: Cannot open file\n");
                    return -1;
                }
                dup2(fd, 1);
                close(fd);
            }
            else {
                fprintf(stderr, "swsh: Missing argument after '>'\n");
                return -1;
            }
        }

        if(!strcmp(argv[i], ">>")) {
            if(i < argc - 1) {
                if((fd = open(argv[i+1], O_WRONLY | O_CREAT | O_APPEND, 0755)) < 0) {
                    fprintf(stderr, "swsh: %s: Cannot open file\n");
                    return -1;
                }
                dup2(fd, 1);
                close(fd);
            }
            else {
                fprintf(stderr, "swsh: Missing argument after '>>'\n");
                return -1;
            }
        }
    }

    for(i=0; i<argc-1; ) {
        if(!strcmp(argv[i], ">") || !strcmp(argv[i], "<") || !strcmp(argv[i], ">>")) {
            for(j=i; j<argc-2; j++)     argv[j] = argv[j+2];
            argv[argc-1] = NULL;
            argv[argc-2] = NULL;
            argc -= 2;
        }
        else    i++;
    }

    return argc;
}

/* FINDPIPE - check if there are pipe specified by | */
int findPipe(int argc, char ** argv) {
    int i;

    for(i=argc-1; i>=0; i--) {
        if(!strcmp(argv[i], "|")) {
            argv[i] = NULL;
            return i;
        }
    }

    return -1;
}


/* FUNCTIONS */

void my_exit(int argc, char ** argv) {
    fprintf(stderr, "exit\n");
    if(argc < 2)    exit(0);
    else            exit(atoi(argv[1]));
}

void my_cd(int argc, char ** argv) {
    if(argc < 2)                return;
    if(chdir(argv[1]) < 0)      fprintf(stderr, "swsh: %s: No such file or directory\n", argv[1]);
    return;
}

void my_pwd(int argc, char ** argv) {
    char * path;
    char buf[MAXLINE] = { 0 , };

    path = getcwd(buf, MAXLINE);
    if(path == NULL) {
        fprintf(stderr, "swsh: current working directory get error\n");
        return;
    }
    fprintf(stdout, "%s\n", path);

    return;
}

void my_rm(int argc, char ** argv) {
    int i;

    if(argc < 2) {
        fprintf(stderr, "swsh: missing operand\n");
        return;
    }
    for(i=1; i<argc; i++) {
        if(unlink(argv[i]) < 0)     fprintf(stderr, "swsh: cannot remove '%s'\n", argv[i]);
    }

    return;
}

void my_mv(int argc, char ** argv) {
    char * dirName, * fileName;
    int i;

    if(argc < 2) {
        fprintf(stderr, "swsh: missing file operand\n");
        return;
    }
    else if(argc < 3) {
        fprintf(stderr, "swsh: missing destination file operand after '%s'\n", argv[1]);
        return;
    }
    else if(argc > 3) {
        for(i=1; i<argc-1; i++) {
            dirName = strdup(argv[argc - 1]);
            if(dirName[strlen(dirName - 1)] != '/')
                dirName = strcat(dirName, '/');
            
            fileName = strrchr(argv[i], '/');
            if(!fileName)   fileName = argv[i];
            else            fileName++;

            if(rename(argv[i], strcat(dirName, fileName)) < 0) {
                fprintf(stderr, "swsh: cannot move '%s'\n", argv[1]);
                return;
            }

            free(dirName);
        }
    }
    else {
        if(rename(argv[1], argv[2]) < 0) {
            fprintf(stderr, "swsh: cannot move '%s'\n", argv[1]);
            return;
        }
    }
}

void my_cp(int argc, char ** argv) {
    char * dirName, * fileName;
    char buf[MAXLINE + 1] = { 0 , };
    int fd_src, fd_des;
    int c, i;

    if(argc < 2) {
        fprintf(stderr, "swsh: missing file operand\n");
        return;
    }
    else if(argc < 3) {
        fprintf(stderr, "swsh: missing destination file operand after '%s'\n", argv[1]);
        return;
    }
    else if(argc > 3) {
        for(i=1; i<argc-1; i++) {
            dirName = strdup(argv[argc - 1]);
            if(dirName[strlen(dirName - 1)] != '/')
                dirName = strcat(dirName, '/');
            
            fileName = strrchr(argv[i], '/');
            if(!fileName)   fileName = argv[i];
            else            fileName++;
            
            if((fd_src = open(fileName, O_RDONLY)) < 0) {
                fprintf(stderr, "swsh: No such file\n");
                return;
            }
            if((fd_des = open(strcat(dirName, fileName), O_WRONLY | O_CREAT, 0644)) < 0) {
                fprintf(stderr, "swsh: cannot copy to file %s\n", dirName);
                return;
            }

            while((c = read(fd_src, buf, MAXLINE)) > 0) {
                buf[c] = 0;
                write(fd_des, buf, c);
            }

            close(fd_src);
            close(fd_des);
            free(dirName);
        }
    }
    else {
        if((fd_src = open(argv[1], O_RDONLY)) < 0) {
            fprintf(stderr, "swsh: No such file\n");
            return;
        }
        if((fd_des = open(argv[2], O_WRONLY | O_CREAT, 0644)) < 0) {
            fprintf(stderr, "swsh: cannot copy to file %s\n", argv[2]);
            return;
        }
        
        while((c = read(fd_src, buf, MAXLINE)) > 0) {
            buf[c] = 0;
            write(fd_des, buf, c);
        }

        close(fd_src);
        close(fd_des);
    }

    return;
}

void my_cat(int argc, char ** argv) {
    char buf[MAXLINE + 1] = { 0 , };
    int fd, c, out = 1;
    int i;

    if(argc < 2) {      // stdin -> stdout
        while((c = read(0, buf, MAXLINE)) > 0) {
            buf[c] = 0;
            write(out, buf, c);
        }
    }
    else {              // file -> stdout
        for(i=1; i<argc; i++) {
            if((fd = open(argv[i], O_RDONLY)) < 0) {
                fprintf(stderr, "swsh: No such file\n");
                return;
            }
            while((c = read(fd, buf, MAXLINE)) > 0) {
                buf[c] = 0;
                write(out, buf, c);
            }
            close(fd);
        }
    }

    return;
}

void my_head(int argc, char ** argv) {
    int * flag = (int *)calloc(argc, sizeof(int));
    int check = 0, pos = 0, first = 0, out = 1;
    int i, j, k = 10;
    char buf[MAXLINE] = { 0 , };
    char fd, c, ch;

    flag[0] = 1;
    for(i=1; i<argc; i++) {
        if(strstr(argv[i], '-n') != NULL) {
            if(!strcmp("-n", argv[i])) {
                flag[i] = 1;
                flag[i+1] = 1;

                if(i < argc - 1) {
                    for(j=0; j<strlen(argv[i+1]); j++) {
                        if(argv[i+1][j] < '0' || argv[i+1][j] > '9') {
                            fprintf(stderr, "swsh: invalid number of lines: '%s'\n", argv[i]);
                            return;
                        }
                    }
                    k = atoi(argv[i+1]);
                }
                else {
                    fprintf(stderr, "swsh: option requires an argument -- 'n'\n");
                    return;
                }
            }

            else {
                flag[i] = 1;
                for(j=2; j<strlen(argv[i]); j++) {
                    if(argv[i][j]<'0' || argv[i][j] > '9') {
                        fprintf(stderr, "swsh: invalid number of lines: '%s'\n", argv[i]);
                        return;
                    }
                }
                k = atoi(argv[i]+2);
            }
        }
        if(flag[i] == 0)    check += 1;
    }

    if(argc < 2 || check == 0) {
        j = 0;
        while((j < k) && ((c = read(0, &ch, 1)) > 0)) {
            buf[pos++] = ch;
            if(ch == '\n') {
                j++;
                write(out, buf, pos);
                pos = 0;
            }
        }
        write(out, buf, pos);
    }

    for(i=1; i<argc; i++) {
        if(flag[i] == 0 && ((fd = open(argv[i], O_RDONLY)) < 0)) {
            fprintf(stderr, "swsh: No such file\n");
            return;
        }
        if(flag[i] == 1)    continue;

        pos = 0;
        memset(buf, '\0', sizeof(buf));

        if(check > 1 && k > 0) {
            if(first != 0)      write(out, "\n", 1);
            sprintf(buf, "==> %s <==\n", argv[i]);
            write(out, buf, sizeof(buf));
            first = 1;
        }

        j = 0;
        while((j < k) && ((c = read(fd, &ch, 1)) > 0)) {
            buf[pos++] = ch;
            if(ch == '\n') {
                j++;
                write(out, buf, pos);
                pos = 0;
            }
        }

        buf[pos++] = '\0';
        write(out, buf, pos);
        close(fd);
    }

    return;
}

void my_tail(int argc, char ** argv) {
    int * flag = (int *)calloc(argc, sizeof(int));
    int check = 0, pos = 0, cnt=0, first = 0, out = 1;
    int i, j, k = 10;
    char ** lines = NULL;
    char buf[MAXLINE] = { 0 , };
    char fd, c, ch;

    flag[0] = 1;
    for(i=1; i<argc; i++) {
        if(strstr(argv[i], '-n') != NULL) {
            if(!strcmp("-n", argv[i])) {
                flag[i] = 1;
                flag[i+1] = 1;

                if(i < argc - 1) {
                    for(j=0; j<strlen(argv[i+1]); j++) {
                        if(argv[i+1][j] < '0' || argv[i+1][j] > '9') {
                            fprintf(stderr, "swsh: invalid number of lines: '%s'\n", argv[i]);
                            return;
                        }
                    }
                    k = atoi(argv[i+1]);
                }
                else {
                    fprintf(stderr, "swsh: option requires an argument -- 'n'\n");
                    return;
                }
            }

            else {
                flag[i] = 1;
                for(j=2; j<strlen(argv[i]); j++) {
                    if(argv[i][j]<'0' || argv[i][j] > '9') {
                        fprintf(stderr, "swsh: invalid number of lines: '%s'\n", argv[i]);
                        return;
                    }
                }
                k = atoi(argv[i]+2);
            }
        }
        if(flag[i] == 0)    check += 1;
    }

    lines = (char **)calloc(k, sizeof(char *));

    if(argc < 2 || check == 0) {
        while((c = read(0, &ch, 1)) > 0) {
            buf[pos++] = ch;
            if(ch == '\n' || ch == EOF) {
                buf[pos++] = '\0';
                if(cnt < k)     lines[cnt++] = strdup(buf);
                else {
                    free(lines[0]);
                    for(i=0;i<cnt-1;i++)    lines[i] = lines[i+1];
                    lines[cnt-1] = strdup(buf);
                }
                pos = 0;
            }
        }

        if(pos > 0) {
            buf[pos++] = '\0';
            if(cnt < k)     lines[cnt++] = strdup(buf);
            else {
                free(lines[0]);
                for(j=0; j<cnt-1; j++)      lines[j] = lines[j+1];
                lines[cnt-1] = strdup(buf);
            }
            pos = 0;
        }

        for(i=0;i<cnt;i++)      write(out, lines[i], strlen(lines[i]));
    }

    for(i=1; i<argc; i++) {
        if(flag[i] == 0 && ((fd = open(argv[i], O_RDONLY)) < 0)) {
            fprintf(stderr, "swsh: No such file\n");
            return;
        }
        if(flag[i] == 1)    continue;

        pos = 0;
        memset(buf, '\0', sizeof(buf));

        if(check > 1 && k > 0) {
            if(first != 0)      write(out, "\n", 1);
            sprintf(buf, "==> %s <==\n", argv[i]);
            write(out, buf, sizeof(buf));
            first = 1;
        }

        cnt = 0;
        while((c = read(fd, &ch, 1)) > 0) {
            buf[pos++] = ch;
            if(ch == '\n' || ch == EOF) {
                buf[pos++] = '\0';
                if(cnt < k)     lines[cnt++] = strdup(buf);
                else {
                    free(lines[0]);
                    for(j=0; j<cnt-1; j++)        lines[j] = lines[j+1];
                    lines[cnt-1] = strdup(buf);
                }
                pos = 0;
            }
        }

        if(pos > 0) {
            buf[pos++] = '\0';
            if(cnt < k)
                lines[cnt++] = strdup(buf);
            else {
                free(lines[0]);
                for(j=0; j<cnt-1; j++)    lines[j] = lines[j+1];
                lines[cnt-1] = strdup(buf);
            }
            pos = 0;
        }
        for(j=0;j<cnt;j++)
            write(out, lines[j], strlen(lines[j]));
        close(fd);
    }

    return;
}
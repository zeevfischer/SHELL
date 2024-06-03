#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include "stdio.h"
#include "errno.h"
#include "stdlib.h"
#include "unistd.h"
#include <string.h>

#define HISTORY_SIZE 20

typedef struct variable {
    char key[1024];
    char value[1024];
} variable;

volatile sig_atomic_t ctrl_c_pressed = 0;
void handle_sigint(int sig) {
    printf("\nYou typed Control-C!\n");
    ctrl_c_pressed = 1;
}

int main() {
    ///////////////////////////////// sig handling
    struct sigaction sa;
    sa.sa_handler = handle_sigint;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    /////////////////////////////////

    variable variables[20];
    int variableCount = 0;

    char history[HISTORY_SIZE][1024] = {""};
    int history_index = 0;
    int history_count = 0;

    char command[1024];
    char last_command[1024] = "";  // Variable to store the last command
    char *token;
    int i;
    char *outfile;
    int fd, amper, redirect, redirect_stderr, redirect_append, piping, retid, status;
    int argc1[10];
    int fildes[2];
    char *argv1[10][10];
    char *argv2[10];
    char prompt[50] = "hello:";  // Default prompt

    while (1) {




        if (ctrl_c_pressed) {
            ctrl_c_pressed = 0;
            continue;
        }

        printf("%s ", prompt);
        if (fgets(command, 1024, stdin) == NULL) continue;
        command[strlen(command) - 1] = '\0';

        /* Check if the command is quit */
        if (strcmp(command, "quit") == 0)
        {
            printf("Exiting the shell...\n");
            exit(0);  // Exit the shell
        }

        /* Check if the command is !! */
        if (strcmp(command, "!!") == 0)
        {
            if (strlen(last_command) == 0)
            {
                printf("No commands in history\n");
                continue;
            } 
            else
            {
                strcpy(command, last_command);
                printf("%s\n", command); // Print the last command
            }
        } 
        else
        {
            strcpy(last_command, command); // Save the current command as the last command

            if (history_count < HISTORY_SIZE)
            {
                strcpy(history[history_index], command);
                history_index = (history_index + 1) % HISTORY_SIZE;
                history_count++;
            } 
            else
            {
                // Shift the history to make space for the new command
                for (i = 0; i < HISTORY_SIZE - 1; i++)
                {
                    strcpy(history[i], history[i + 1]);
                }
                strcpy(history[HISTORY_SIZE - 1], command);
            }
        }

        piping = 0;
        redirect = 0;
        redirect_stderr = 0;
        redirect_append = 0;

        /* Check if the command is to change the prompt */
        if (strncmp(command, "prompt = ", 9) == 0)
        {
            strcpy(prompt, command + 9);
            continue;
        }

        /* parse command line */
        i = 0;
        int num_commands = 0;
        char command2[1024];
        strcpy(command2, command);
        token = strtok(command, " ");
        while (token != NULL)
        {
            argv1[num_commands][i] = token;
            token = strtok (NULL, " ");
            i++;
            if (token && ! strcmp(token, "|")) {
                piping++;
                token = strtok (NULL, " ");

                argv1[num_commands][i] = NULL;
                argc1[num_commands] = i;
                i = 0;
                num_commands++;
                // break;
            }
        }
        argv1[num_commands][i] = NULL;
        argc1[num_commands] = i;
        num_commands++;
        if (piping) {
            if (fork()==0) {
                execvp("bash", (char *[]){"bash", "-c", command2, NULL});
            } 
            wait(&status);
            continue;
        }

        /* Is command empty */
        if (argv1[0][0] == NULL) continue;

        /* Check for built-in cd command */
        if (strcmp(argv1[0][0], "cd") == 0) {
            if (argc1[0] < 2) {
                fprintf(stderr, "cd: missing argument\n");
            } else {
                if (chdir(argv1[0][1]) != 0) {
                    perror("cd");
                }
            }
            continue;
        }

        if (strcmp(argv1[0][0], "echo") == 0) {
            if (argv1[0][1] != NULL && strcmp(argv1[0][1], "$?") == 0) {
                printf("%d\n", WEXITSTATUS(status));
            } else {
                for (i = 1; argv1[0][i] != NULL; i++) {
                    if (argv1[0][i][0] == '$') {
                        char *var_name = argv1[0][i];
                        int found = 0;
                        for (int j = 0; j < variableCount; j++) {
                            if (strcmp(variables[j].key, var_name) == 0) {
                                printf("%s", variables[j].value);
                                found = 1;
                                break;
                            }
                        }
                        if (!found) {
                            printf("Variable %s not found", var_name);
                        }
                    } else {
                        printf("%s", argv1[0][i]);
                    }
                    if (argv1[0][i + 1] != NULL) printf(" ");
                }
                printf("\n");
            }
            continue;
        }

        /* Does command contain pipe */
        if (piping) {
            i = 0;
            while ((token = strtok(NULL, " ")) != NULL) {
                argv2[i] = token;
                i++;
            }
            argv2[i] = NULL;
        }

        if (argv1[0][0][0] == '$' && argv1[0][1] && !strcmp(argv1[0][1], "=")) {
            if (argv1[0][2] == NULL)
                printf("missing variable value\n");
            else if (variableCount < 20) {
                strcpy(variables[variableCount].key ,argv1[0][0]);
                strcpy(variables[variableCount].value, argv1[0][2]);
                variableCount++;
            } else {
                printf("Too many variables\n");
            }
            continue;
        }
        /* Is command read */ 
        else if (!strcmp(argv1[0][0], "read")) {
            if (argv1[0][1] == NULL) {
                printf("missing variable name\n");
            } else {
                char input[1024];
                fgets(input, 1024, stdin);
                input[strlen(input) - 1] = '\0';
                if (variableCount < 20) {
                    char str[1024];
                    strcpy(str, "$");
                    strcat(str, argv1[0][1]);
                    strcpy(variables[variableCount].key , str);
                    strcpy(variables[variableCount].value, input);
                    variableCount++;
                } else {
                    printf("Too many variables\n");
                }

            }
        continue;
        }

        /* Does command line end with & */
        if (!strcmp(argv1[0][argc1[0] - 1], "&")) {
            amper = 1;
            argv1[0][argc1[0] - 1] = NULL;
        } else {
            amper = 0;
        }

        /* Check for stderr redirection */
        if (argc1[0] > 1 && !strcmp(argv1[0][argc1[0] - 2], "2>")) {
            redirect_stderr = 1;
            argv1[0][argc1[0] - 2] = NULL;
            outfile = argv1[0][argc1[0] - 1];
        }
        // Check for stdout append redirection
        else if (argc1[0] > 1 && !strcmp(argv1[0][argc1[0] - 2], ">>")) {
            redirect_append = 1;
            argv1[0][argc1[0] - 2] = NULL;
            outfile = argv1[0][argc1[0] - 1];
        }
        // Check for stdout overwrite redirection
        else if (argc1[0] > 1 && !strcmp(argv1[0][argc1[0] - 2], ">")) {
            redirect = 1;
            argv1[0][argc1[0] - 2] = NULL;
            outfile = argv1[0][argc1[0] - 1];
        } else {
            redirect_append = 0;
            redirect_stderr = 0;
            redirect = 0;
        }

        /* for commands not part of the shell command language */
        int pid = fork();
        if (pid == 0) {
            /* redirection of IO */
            if (redirect) {
                fd = creat(outfile, 0660);
                if (fd < 0) { perror("creat"); exit(1); }
                close(STDOUT_FILENO);
                dup(fd);
                close(fd);
                /* stdout is now redirected */
            }
            if (redirect_append) {
                fd = open(outfile, O_WRONLY | O_CREAT | O_APPEND, 0660);
                if (fd < 0) { perror("open"); exit(1); }
                close(STDOUT_FILENO);
                dup(fd);
                close(fd);
                /* stdout is now redirected to append */
            }
            if (redirect_stderr) {
                fd = creat(outfile, 0660);
                if (fd < 0) { perror("creat"); exit(1); }
                close(STDERR_FILENO);
                dup(fd);
                close(fd);
                  /* stderr is now redirected */
            }
            if (piping) {
                if (pipe(fildes) < 0) { perror("pipe"); exit(1); }
                int pid2 = fork();
                if (pid2 == 0) {
                    /* first component of command line */
                    close(STDOUT_FILENO);
                    dup(fildes[1]);
                    close(fildes[1]);
                    close(fildes[0]);
                    /* stdout now goes to pipe */
                    /* child process does command */
                    execvp(argv1[0][0], argv1[0]);
                    perror("execvp"); exit(1);
                }
                /* 2nd command component of command line */
                close(STDIN_FILENO);
                dup(fildes[0]);
                close(fildes[0]);
                close(fildes[1]);
                /* standard input now comes from pipe */
                execvp(argv2[0], argv2);
                perror("execvp"); exit(1);
            } else {
                execvp(argv1[0][0], argv1[0]);
                perror("execvp"); exit(1);
            }
        } else if (pid < 0) {
            perror("fork");
        }

        /* parent continues over here... */
        /* waits for child to exit if required */
        if (amper == 0) {
            retid = wait(&status);
            if (retid < 0) perror("wait");
        }
    }
}
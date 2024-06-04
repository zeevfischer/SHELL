#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <termios.h>
#include <signal.h>

#define HISTORY_SIZE 20

typedef struct variable {
    char key[1024];
    char value[1024];
} variable;

volatile sig_atomic_t ctrl_c_pressed = 0;

void handle_sigint(int sig)
{
    printf("\nYou typed Control-C!\n");
    ctrl_c_pressed = 1;
}

void enableRawMode()
{
    struct termios term;
    tcgetattr(STDIN_FILENO, &term);
    term.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &term);
}

void disableRawMode()
{
    struct termios term;
    tcgetattr(STDIN_FILENO, &term);
    term.c_lflag |= (ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &term);
}

void printPrompt(const char* prompt)
{
    printf("%s ", prompt);
    fflush(stdout);
}

void clearLine()
{
    printf("\33[2K\r");
}


int main() {
    ////////////// sig handling /////////////////// 
    struct sigaction sa;
    sa.sa_handler = handle_sigint;
    // sa.sa_flags = SA_RESTART;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    ////////////// sig handling ////////////// 

    ////////////// variables //////////////
    variable variables[20];
    int variableCount = 0;

    char history[HISTORY_SIZE][1024] = {""};
    int history_index = 0;
    int history_count = 0;
    int history_position = -1;

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
    int line_down = 1;
    int isRunning;
    int is_redo_last_command = 0;
    ////////////// variables //////////////
    while (1) {
        enableRawMode();  // Enable raw mode
        for(int i = 0;i<1024;i++)
        {
            command[i] = '\0';
        }
        printPrompt(prompt);
        line_down = 1;
        is_redo_last_command = 0;
        
        ///////////// arrow key handling /////////////
        // Reading character by character to detect arrow keys and handle backspace
        char c;
        int pos = 0;
        history_position = -1;
        while (1 && ctrl_c_pressed == 0) {
            if (read(STDIN_FILENO, &c, 1) == -1) {
                if (errno == EINTR) {
                    // Ctrl-C was pressed, ignore the interruption
                    continue;
                } else {
                    perror("read");
                    break;
                }
            }
            if (c == '\n' && pos == 0) { // Check if Enter is pressed and command is empty
                printf("\n"); // Move to the next line
                break;
            } else if (c == '\n') {
                command[pos] = '\0';
                break;
            } else if (c == 27) {  // Escape character
                char seq[3];
                if (read(STDIN_FILENO, &seq[0], 1) == -1) break;
                if (read(STDIN_FILENO, &seq[1], 1) == -1) break;
                if (seq[0] == '[') {
                    if (seq[1] == 'A') {  // Up arrow
                    // printf("history count = %d\n",history_count);
                    // fflush(stdout);
                        if (history_count > 0) {
                            // printf("history index = %d\n",history_index);
                            // fflush(stdout);
                            if (history_position == -1)
                            {
                                history_position = history_index;
                            }
                            if(history_position - 1 < 0)
                            {
                                history_position = 0;
                            }
                            else
                            {
                                history_position = history_position - 1;
                            }
                            clearLine();
                            printPrompt(prompt);
                            printf("%s", history[history_position]);
                            fflush(stdout);
                            strcpy(command, history[history_position]);
                            pos = strlen(command);
                        }
                    } else if (seq[1] == 'B') {  // Down arrow
                        if (history_count > 0 && history_position != -1) {
                            history_position = history_position + 1;
                            if (history_position >= history_count) {
                                history_position = 20;
                                // printf("\nin if");
                                // printf("\nhistory pos = %d\n ",history_position);
                                // printf("history count = %d\n ",history_count);
                                fflush(stdout);
                                clearLine();
                                printPrompt(prompt);
                                // history_position = -1;
                                pos = 0;
                                command[0] = '\0';
                            } else {
                                // printf("\nin else");
                                // printf("\nhistory pos = %d\n ",history_position);
                                // printf("history count = %d\n ",history_count);
                                fflush(stdout);
                                clearLine();
                                printPrompt(prompt);
                                printf("%s", history[history_position]);
                                fflush(stdout);
                                strcpy(command, history[history_position]);
                                pos = strlen(command);
                            }
                        }
                    }
                }
                continue;
            } else if (c == 127 || c == 8) {  // Handle backspace
                if (pos > 0) {
                    pos--;
                    command[pos] = '\0';  // Remove the last character
                    printf("\b \b");  // Move back, print space, move back again
                    fflush(stdout);
                }
            } else {
                command[pos++] = c;
                write(STDOUT_FILENO, &c, 1);  // Echo the character
            }
        }
        ///////////// arrow key handling /////////////
        if (ctrl_c_pressed)
        {
            ctrl_c_pressed = 0;
            continue;
        }

        /* Check if the command is quit */
        if (strcmp(command, "quit") == 0)
        {
            line_down = 0;
            printf("\nExiting the shell...\n");
            break;
        }

        /* Check if the command is !! */
        // this command dose not go in the history as a command if executed 
        if (strcmp(command, "!!") == 0)
        {
            is_redo_last_command = 1;
            line_down = 0;
            printf("\n");
            if (strlen(last_command) == 0)
            {
                printf("No commands in history\n");
                continue;
            }
            else
            {
                strcpy(command, last_command);
                printf("%s", command); // Print the last command
                fflush(stdout); // Flush the output buffer
            }
        } 
        else // update history with the command not "!!" as a command 
        {
            strcpy(last_command, command); // Save the current command as the last command

            if (history_count < HISTORY_SIZE) {
                strcpy(history[history_index], command);
                history_index = (history_index + 1) % HISTORY_SIZE;
                history_count++;
                if(history_index == 0)
                {
                    history_index = 20;
                }
            } else {
                // Shift the history to make space for the new command
                for (i = 0; i < HISTORY_SIZE - 1; i++) {
                    strcpy(history[i], history[i + 1]);
                }
                strcpy(history[HISTORY_SIZE - 1], command);
            }
        }

        piping = 0;
        redirect = 0;
        redirect_stderr = 0;
        redirect_append = 0;
        amper = 0;

        /* Check if the command ends with '&' */
        int len = strlen(command);
        if (len > 0 && command[len - 1] == '&') {
            line_down = 0;
            amper = 1;
            command[len - 1] = '\0';  // Remove '&' from the command
            printf("\n");
            continue;
        }

        /* Check if the command is to change the prompt */
        if (strncmp(command, "prompt = ", 9) == 0) {
            line_down = 0;
            strcpy(prompt, command + 9);
            printf("\n");
            continue;
        }
        /* Is command IF/ELSE bash flow */
        if (command[0] == 'i' && command[1] == 'f')
        {
            line_down = 0;
            disableRawMode();
            // printf(" enter if ");
            // printf("\ncommand = %s " ,command);
            command[strlen(command)] = '\n';
            // printf("\ncommand = %s " ,command);
            int first = 1;
            while (1) {
                // printf(" command = %s " ,command);
                if (first == 1)
                {
                    printf("\n");
                    first = 0;    
                }
                // printf(" enter while ");
                char currentCodeLine[1024];
                // printf("\n");
                fgets(currentCodeLine, 1024, stdin);
                // printf("passed");
                strcat(command, currentCodeLine);
                // command[strlen(command) - 1] = '\n';
                if (!strcmp(currentCodeLine, "fi\n"))
                    break;
            }
            isRunning = 1;
            if (fork()==0) {
                execvp("bash", (char *[]){"bash", "-c", command, NULL});
                perror("execvp"); exit(1);
            } 
            wait(&status);
            isRunning = 0;
            enableRawMode();
            continue;
        }

        /* parse command line */
        i = 0;
        int num_commands = 0;
        char command2[1024];
        strcpy(command2, command);
        token = strtok(command, " ");
        while (token != NULL) {
            argv1[num_commands][i] = token;
            token = strtok(NULL, " ");
            i++;
            if (token && !strcmp(token, "|")) {
                piping++;
                token = strtok(NULL, " ");

                argv1[num_commands][i] = NULL;
                argc1[num_commands] = i;
                i = 0;
                num_commands++;
            }
        }
        argv1[num_commands][i] = NULL;
        argc1[num_commands] = i;
        num_commands++;

        if (piping) {
            line_down = 0;
            printf("\n");
            if (fork() == 0) {
                execvp("bash", (char *[]){"bash", "-c", command2, NULL});
            }
            wait(&status);
            continue;
        }

        /* Is command empty */
        if (argv1[0][0] == NULL) continue;

        /* Check for built-in cd command */
        if (strcmp(argv1[0][0], "cd") == 0) {
            line_down = 0;
            printf("\n");
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
            line_down = 0;
            printf("\n");
            if (argv1[0][1] != NULL && strcmp(argv1[0][1], "$?") == 0)
            {
                printf("%d\n", WEXITSTATUS(status));
            }
            else
            {
                for (i = 1; argv1[0][i] != NULL; i++)
                {
                    if (argv1[0][i][0] == '$')
                    {
                        char *var_name = argv1[0][i];
                        int found = 0;
                        for (int j = 0; j < variableCount; j++)
                        {
                            if (strcmp(variables[j].key, var_name) == 0)
                            {
                                printf("%s", variables[j].value);
                                found = 1;
                                break;
                            }
                        }
                        if (!found)
                        {
                            printf("Variable %s not found", var_name);
                        }
                    }
                    else
                    {
                        printf("%s", argv1[0][i]);
                    }
                    if (argv1[0][i + 1] != NULL) printf(" ");
                }
                printf("\n");
            }
            continue;
        }

        // /* Does command contain pipe */
        // if (piping) {
        //     i = 0;
        //     while ((token = strtok(NULL, " ")) != NULL) {
        //         argv2[i] = token;
        //         i++;
        //     }
        //     argv2[i] = NULL;
        // }

        if (argv1[0][0][0] == '$' && argv1[0][1] && !strcmp(argv1[0][1], "=")) {
            line_down = 0;
            if (argv1[0][2] == NULL) {
                printf("\nmissing variable value\n");
            } else if (variableCount < 20) {
                printf("\n");
                strcpy(variables[variableCount].key, argv1[0][0]);
                strcpy(variables[variableCount].value, argv1[0][2]);
                variableCount++;
            } else {
                printf("\nToo many variables\n");
            }
            continue;
        }
        
        /* Is command read */
        else if (!strcmp(argv1[0][0], "read")) {
            line_down = 0;
            if (argv1[0][1] == NULL) {
                printf("\nNo variable specified\n");
            } else if (variableCount < 20) {
                printf("\nEnter value: ");
                disableRawMode();
                fflush(stdout);
                fflush(stdin);
                fgets(variables[variableCount].value, sizeof(variables[variableCount].value), stdin);
                variables[variableCount].value[strcspn(variables[variableCount].value, "\n")] = 0;
                sprintf(variables[variableCount].key, "$%s", argv1[0][1]);
                variableCount++;
            } else {
                printf("Too many variables\n");
            }
            enableRawMode();
            continue;
        }
        
        /* find out if there is any input/output redirection or piping on the command line */
        if (argc1[0] > 1 && !strcmp(argv1[0][argc1[0] - 2], "2>")) {
            line_down = 0;
            redirect_stderr = 1;
            argv1[0][argc1[0] - 2] = NULL;
            outfile = argv1[0][argc1[0] - 1];
            printf("\n");
        }
        // Check for stdout append redirection
        else if (argc1[0] > 1 && !strcmp(argv1[0][argc1[0] - 2], ">>")) {
            line_down = 0;
            redirect_append = 1;
            argv1[0][argc1[0] - 2] = NULL;
            outfile = argv1[0][argc1[0] - 1];
            printf("\n");

        }
        // Check for stdout overwrite redirection
        else if (argc1[0] > 1 && !strcmp(argv1[0][argc1[0] - 2], ">")) {
            line_down = 0;
            redirect = 1;
            argv1[0][argc1[0] - 2] = NULL;
            outfile = argv1[0][argc1[0] - 1];
            printf("\n");
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
            }
            else 
            {
                disableRawMode();
                if(is_redo_last_command == 1)
                {
                    printf("\n");
                }
                fflush(stdout);
                execvp(argv1[0][0], argv1[0]);
                perror("execvp"); exit(1);
            }
            
        } else if (pid < 0) {
            perror("fork");
        }
        if(line_down == 1)
        {
            printf("\n");
        }

        /* parent continues over here... */
        /* waits for child to exit if required */
        if (!amper) {
            retid = wait(&status);
            if (retid < 0) perror("wait");
        }
        for(int i = 0;i<1024;i++)
        {
            command[i] = '\0';
        }
        
    }

    disableRawMode();  // Disable raw mode before exiting
    return 0;
}


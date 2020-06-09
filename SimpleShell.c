#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/wait.h>
#define MAX_CHAR 100
#define MAX_WORD 10
#define HISTORY_SIZE 10
#define DEL " "
#define NOT_SPECIAL_COMMAND 0
#define IS_HISTORY 1
#define IS_EXCLAMATION_MARK 2

int input_redirection_flag = 0;
int output_redirection_flag = 0;
int append_redirection_flag = 0;
int piping_flag = 0;

int is_ampersand = 0;
int current_size_of_history = 0;
char *input_file = NULL;
char *output_file = NULL;

char *history[HISTORY_SIZE];
char *previous_command = NULL;

/*********************************************************************************/
// delete \n
void remove_endOfcommand(char command[])
{
    strtok(command, "\n");
}

// input command[] from terminal
char *input_command(char command[])
{
    char *line = fgets(command, MAX_CHAR, stdin);
    if (strcmp(command, "\n") == 0)
    {
        return NULL;
    }
    remove_endOfcommand(command);
    return line;
}

/*********************************************************************************/

void show_history()
{
    int i = 0;

    if (history[i] == NULL)
    {
        printf("NO COMMANDS IN HISTORY\n");
        return;
    }
    while (history[i] != NULL)
    {
        printf("[%d] ", i);
        printf("%s\n", history[i]);
        i++;
    }
}

void insert_history(char command[])
{
    if (strcmp(command, "history") == 0 || command[0] == '!')
    {
        return;
    }
    int i = 0;
    while (history[i] != NULL)
    {
        i++;
    }
    if (i == HISTORY_SIZE)
        return;
    current_size_of_history++;
    history[i] = strdup(command);
}

/*********************************************************************************/
/* sum of input_redirection_cnt,output_redirection_cnt,pipe_cnt
if sum>1 printf(error) and temp[0]=NULL*/

void check_ampersand(char *temp[])
{
    int i = 0;
    while (temp[i] != NULL)
        i++;

    if (strcmp(temp[i - 1], "&") == 0)
    {
        temp[i - 1] = NULL;
        is_ampersand = 1;
    }
}

void sum_of_redirection_and_pipe(char *temp[], int input_redirection_cnt,
                                 int output_redirection_cnt, int pipe_cnt)
{
    int total_cnt = input_redirection_cnt + output_redirection_cnt + pipe_cnt;
    if (total_cnt > 1)
    {
        printf("ERROR: Can't handle this case\n");
        temp[0] = NULL;
    }
}

/* if in command include both 2 char | or > or <.
then count the number of | or > or <*/
void count_redirection_and_pipe(char *temp[])
{
    int pipe_cnt = 0;
    int output_redirection_cnt = 0;
    int input_redirection_cnt = 0;
    int i = 0;

    while (temp[i] != NULL)
    {
        if (strcmp(temp[i], ">") == 0)
            output_redirection_cnt++;
        if (strcmp(temp[i], "<") == 0)
            input_redirection_cnt++;
        if (strcmp(temp[i], "|") == 0)
            pipe_cnt++;
        i++;
    }
    sum_of_redirection_and_pipe(temp, input_redirection_cnt, output_redirection_cnt, pipe_cnt);
}

// divide command into temp[0]=conda  temp[1]=list  temp[2]=|  temp[3]=grep temp[4]=tensor
void strtok_command(char command[], char *temp[])
{
    int i = 0;
    temp[i] = strtok(command, DEL);

    while (temp[i] != NULL)
    {
        i++;
        temp[i] = strtok(NULL, DEL);
    }
}

/* e.g: conda list | grep tensor
temp[0]=conda  temp[1]=list  temp[2]=|  temp[3]=grep temp[4]=tensor
and check error*/
void handle_command(char command[], char *temp[])
{
    strtok_command(command, temp);
    count_redirection_and_pipe(temp);
    check_ampersand(temp);
}

/*********************************************************************************/
// return position of ">" or "<" or "|"
int get_position_pipe_or_redirection(char *temp[])
{
    int i = 0;
    while (temp[i] != NULL)
    {
        if (strcmp(temp[i], ">") == 0)
        {
            output_redirection_flag = 1;
            output_file = temp[i + 1];
            return i;
        }
        if (strcmp(temp[i], ">>") == 0)
        {
            append_redirection_flag = 1;
            output_file = temp[i + 1];
            return i;
        }
        if (strcmp(temp[i], "<") == 0)
        {
            input_redirection_flag = 1;
            input_file = temp[i + 1];
            return i;
        }
        if (strcmp(temp[i], "|") == 0)
        {
            piping_flag = 1;
            return i;
        }
        i++;
    }
    return i;
}

// setup args and piping_args from command
// command = args + "|" + piping_args (with pipe)
// or command = args (without pipe)
void setup(char *args[], char command[], char *piping_args[])
{
    //e.g: conda list | grep tensor
    char *temp[MAX_WORD];
    handle_command(command, temp);

    // temp[0]=conda  temp[1]=list  temp[2]=|  temp[3]=grep temp[4]=tensor
    // piping_flag = 1;

    int pos = get_position_pipe_or_redirection(temp);
    // pos = 2

    int i = 0;
    while (i < pos)
    {
        args[i] = temp[i];
        i++;
    }
    // args[0]=conda args[1]=list i=2

    args[pos] = NULL;
    // args[2]=NULL

    i++;
    // i=3

    if (piping_flag == 1)
    {
        int j = 0;
        while (temp[i] != NULL)
        {
            piping_args[j] = temp[i];
            // piping_args[0]=grep piping_args[1]=tensor
            i++;
            j++;
        }
    }
}

/*********************************************************************************/
/*Inside Parent Process : We firstly close the reading end of first pipe (fd1[0]) 
then write the string though writing end of the pipe (fd1[1]). 
Now parent will wait until child process is finished. 
After the child process, parent will close the writing end of second pipe(fd2[1]) 
and read the string through reading end of pipe (fd2[0]).

Inside Child Process : Child reads the first string sent by parent process by closing the writing end of pipe (fd1[1]) 
and after reading concatenate both string and passes the string to parent process via fd2 pipe and will exit.*/
void handle_piping(char *args[], char *piping_args[])
{
    // args[0]=conda args[1]=list
    // piping_args[0]=grep piping_args[1]=tensor
    int pipefd[2];
    pipe(pipefd); //bytes written on PIPEDES[1] can be read from PIPEDES[0].

    int pid1 = fork();
    if (pid1 == 0)
    {
        int pid2 = fork();
        if (pid2 == 0)
        {
            dup2(pipefd[0], STDIN_FILENO);
            close(pipefd[1]);
            if (execvp(piping_args[0], piping_args))
            {
                perror(piping_args[0]);
                exit(EXIT_FAILURE);
            }
        }
        else
        {
            dup2(pipefd[1], STDOUT_FILENO);
            close(pipefd[0]);
            if (execvp(args[0], args))
            {
                perror(args[0]);
                exit(EXIT_FAILURE);
            }
        }
    }
    else
    {
        if (!is_ampersand)
            waitpid(pid1, NULL, 0);
    }
}

void handle_input_redirection(char *args[])
{
    pid_t pid = fork();

    if (pid == 0)
    {
        int fd = open(input_file, O_RDWR | O_CREAT, 0777);
        if (fd < 0)
        {
            perror("open file failed");
            return;
        }
        if (dup2(fd, STDIN_FILENO) < 0)
        {
            perror("dup2 failed");
            return;
        }
        close(fd);
        if (execvp(args[0], args))
        {
            perror(args[0]);
            exit(EXIT_FAILURE);
        }
    }
    else
    {
        if (!is_ampersand)
            waitpid(pid, NULL, 0);
    }
}

void handle_output_redirection(char *args[])
{
    pid_t pid = fork();

    if (pid < 0)
    {
        perror("fork failed");
        return;
    }
    if (pid == 0)
    {
        int fd = open(output_file, O_RDWR | O_CREAT, 0777);
        if (fd < 0)
        {
            perror("open file failed");
            return;
        }
        if (dup2(fd, STDOUT_FILENO) < 0)
        {
            perror("dup2 failed");
            return;
        }
        close(fd);
        if (execvp(args[0], args))
        {
            perror(args[0]);
            exit(EXIT_FAILURE);
        }
    }
    else
    {
        if (!is_ampersand)
            waitpid(pid, NULL, 0);
    }
}

void handle_append_redirection(char *args[])
{
    pid_t pid = fork();

    if (pid < 0)
    {
        perror("fork failed");
        return;
    }
    if (pid == 0)
    {
        int fd = open(output_file, O_RDWR | O_APPEND, 0777);
        if (fd < 0)
        {
            perror("open file failed");
            return;
        }
        if (dup2(fd, STDOUT_FILENO) < 0)
        {
            perror("dup2 failed");
            return;
        }
        close(fd);
        if (execvp(args[0], args))
        {
            perror(args[0]);
            exit(EXIT_FAILURE);
        }
    }
    else
    {
        if (!is_ampersand)
            waitpid(pid, NULL, 0);
    }
}

void handle_simple_command(char *args[])
{
    pid_t pid = fork();
    if (pid < 0)
    {
        perror("fork failed");
        exit(EXIT_FAILURE);
    }
    if (pid == 0)
    {
        execvp(args[0], args);
        perror(args[0]);
        exit(EXIT_FAILURE);
    }
    else
    {
        if (!is_ampersand)
            waitpid(pid, NULL, 0);
    }
}

void handle_redirection_piping_simple_command(char *args[], char *piping_args[])
{
    if (input_redirection_flag == 1 && input_file != NULL)
    {
        handle_input_redirection(args);
    }
    if (output_redirection_flag == 1 && output_file != NULL)
    {
        handle_output_redirection(args);
    }
    if (append_redirection_flag == 1 && output_file != NULL)
    {
        handle_append_redirection(args);
    }
    if (piping_flag == 1)
    {
        handle_piping(args, piping_args);
    }
    if (input_redirection_flag == 0 && output_redirection_flag == 0 && append_redirection_flag == 0 && piping_flag == 0)
    {
        handle_simple_command(args);
    }
    // reset variables
    input_redirection_flag = 0;
    output_redirection_flag = 0;
    append_redirection_flag = 0;
    piping_flag = 0;
    is_ampersand = 0;
    input_file = NULL;
    output_file = NULL;
}

/*********************************************************************************/

char *get_command_from_history(int number_of_command)
{
    if (history[number_of_command] == NULL)
    {
        printf("NO COMMANDS IN HISTORY\n");
        return NULL;
    }
    char *temp_command = strdup(history[number_of_command]);

    return temp_command;
}

void create_previous_command(char command[])
{
    int number_of_command = (int)command[1] - 48;
    if (command[1] == '!')
    {
        previous_command = get_command_from_history(current_size_of_history - 1);
    }
    if (number_of_command >= 0 && number_of_command <= current_size_of_history - 1)
    {
        previous_command = get_command_from_history(number_of_command);
    }
    if (previous_command == NULL)
    {
        printf("SOMETHING WRONG WITH HISTORY\n");
        previous_command = NULL;
    }
}

// check "exit", "history", "!!", "!2"
int check_special_command(char command[])
{
    if (strcmp(command, "exit") == 0)
        exit(0);
    if (strcmp(command, "history") == 0)
    {
        return IS_HISTORY;
    }
    if (command[0] == '!') // includes : !!, !2, !3 ,...
    {
        return IS_EXCLAMATION_MARK;
    }
    return NOT_SPECIAL_COMMAND;
}

void handle_special_command(char command[], char *args[], char *piping_args[], int mystery_command)
{
    if (mystery_command == IS_HISTORY)
    {
        show_history();
    }
    if (mystery_command == IS_EXCLAMATION_MARK)
    {
        create_previous_command(command);

        setup(args, previous_command, piping_args);
        handle_redirection_piping_simple_command(args, piping_args);

        // reset previous_command
        previous_command = NULL;
    }
}

/*********************************************************************************/

int main()
{
    char *args[MAX_WORD];
    char command[MAX_CHAR];
    char *piping_args[MAX_WORD];

    while (1)
    {
        printf("osh:~$ ");

        char *line = input_command(command);

        if (line == NULL)
        {
            printf("COMMAND NOT FOUND\n");
        }
        else
        {
            insert_history(command);

            int mystery_command = check_special_command(command);

            if (mystery_command == NOT_SPECIAL_COMMAND)
            {

                setup(args, command, piping_args);
                handle_redirection_piping_simple_command(args, piping_args);
            }
            else
            {
                handle_special_command(command, args, piping_args, mystery_command);
            }
        }
    }
    return EXIT_SUCCESS;
}
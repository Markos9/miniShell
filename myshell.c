#include <stdio.h>
#include <stdlib.h>
#include "parser.h"
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>

char buf[1024];
tline *line;
char *Commands[5] = {"cd", "jobs", "bg", "umask", "exit"};

typedef enum { RUNNING, STOPPED } job_status;

typedef struct {
    int job_id;         
    pid_t pgid;          
    char *cmdline;       
    job_status status;   
} job_t;

int job_capacity = 10;
job_t *job_list = NULL;
int job_count = 0;


void add_job(pid_t pgid, const char *cmdline, job_status status) {
    if (job_count == job_capacity) {
		if (job_capacity == 0) {
			job_capacity = 4;
		} else {
			job_capacity = job_capacity * 2;
		}

        job_list = realloc(job_list, job_capacity * sizeof(job_t));
        if (!job_list) {
            perror("realloc");
            exit(1);
        }
    }

    job_list[job_count].job_id = job_count + 1;
    job_list[job_count].pgid = pgid;
    job_list[job_count].cmdline = strdup(cmdline); // copiar línea
    job_list[job_count].status = status;
    job_count++;
}

void cmd_jobs() {
    for (int i = 0; i < job_count; i++) {
        if (job_list[i].status == RUNNING || job_list[i].status == STOPPED) {
            const char *status_str;

            if (job_list[i].status == RUNNING) {
                status_str = "Running";
            } else {
                status_str = "Stopped";
            }

            printf("[%d]  %s\t%s\n",
                   job_list[i].job_id,
                   status_str,
                   job_list[i].cmdline);
        }
    }
}

void clean_finished_jobs() {
    int i = 0;

    while (i < job_count) {
        pid_t result = waitpid(job_list[i].pgid, NULL, WNOHANG);

        if (result > 0) {
            // Liberar memoria de la línea de comando
            free(job_list[i].cmdline);

            // Mover el último trabajo a esta posición para compactar
            job_list[i] = job_list[job_count - 1];
            job_count--;
        } else {
            i++; // solo avanzar si no eliminaste el trabajo
        }
    }
}

int string_in_list(const char *str) {
    for (int i = 0; i < 5; i++) {
        if (strcmp(str, Commands[i]) == 0) {
            return 1;  // Encontrado
        }
    }
    return 0;  // No encontrado
}

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

void cmd_bg(int job_id) {
    job_t *job_to_resume = NULL;

    if (job_id == 0) {
        // Buscar el último job detenido
        for (int i = job_count - 1; i >= 0; i--) {
            if (job_list[i].status == STOPPED) {
                job_to_resume = &job_list[i];
                break;
            }
        }
        if (!job_to_resume) {
            return;
        }
    } else {
        // Buscar job por job_id
        for (int i = 0; i < job_count; i++) {
            if (job_list[i].job_id == job_id) {
                job_to_resume = &job_list[i];
                break;
            }
        }
        if (!job_to_resume) {
            return;
        }
        if (job_to_resume->status != STOPPED) {
            return;
        }
    }

    // Enviar señal SIGCONT para reanudar
    if (kill(job_to_resume->pgid, SIGCONT) < 0) {
        perror("kill (SIGCONT)");
        return;
    }

    // Cambiar estado a RUNNING
    job_to_resume->status = RUNNING;

    printf("[%d] %s &\n", job_to_resume->job_id, job_to_resume->cmdline);
}



void sigint_handler(int sig)
{
	(void)sig; // evitar warning
	write(STDOUT_FILENO, "\nmsh> ", 6);
}
void sigtstp_handler(int sig)
{
	(void)sig;
}

int command_exists(const char *cmd)
{
	char *path = getenv("PATH");

	char *path_dup = strdup(path);
	char *dir = strtok(path_dup, ":");

	while (dir)
	{
		char cmdpath[1024];
		snprintf(cmdpath, sizeof(cmdpath), "%s/%s", dir, cmd);

		if (access(cmdpath, X_OK) == 0)
		{
			// Existe el comando
			free(path_dup);
			return 1;
		}
		dir = strtok(NULL, ":");
	}

	free(path_dup);
	// No existe
	return 0;
}

void cmd_umask(tline *line){
	if (line->commands[0].argc > 1) {
		char *arg = line->commands[0].argv[1];
		char *endptr;
		mode_t mask = strtol(arg, &endptr, 8);

		if (*endptr != '\0') {
			fprintf(stderr, "umask: argumento no válido\n");
			return;
		}

		if (mask > 0777) {
			fprintf(stderr, "umask: valor fuera de rango\n");
			return;
		}

		umask(mask);
	} else {
		mode_t mask = umask(0);
		printf("%04o\n", mask);
		umask(mask);
	}
}

void pipe_commands(tline *line)
{
	int i;
	int n = line->ncommands;
	int pipes[n - 1][2];
	pid_t pgid = 0;

	for (i = 0; i < n - 1; i++)
	{
		if (pipe(pipes[i]) == -1)
		{
			perror("pipe error");
			exit(EXIT_FAILURE);
		}
	}

	for (i = 0; i < n; i++)
	{
		if (command_exists(line->commands[i].argv[0]) == 0)
		{
			fprintf(stderr, "%s: No se encuentra el mandato\n", line->commands[i].argv[0]);
			return;
		}
	}

	for (i = 0; i < n; i++)
	{
		pid_t pid = fork();
		if (pid < 0)
		{
			perror("fork");
			exit(EXIT_FAILURE);
		}
		else if (pid == 0)
		{
			// Hijo

			if (i == 0)
			{
				setpgid(0, 0); // Primer hijo crea grupo con su PID
			}

			if (!line->background)
			{
				tcsetpgrp(STDIN_FILENO, pgid); // Dar terminal al grupo del primer hijo
				signal(SIGINT, SIG_DFL);
				signal(SIGTSTP, SIG_DFL);
			}
			else
			{
				signal(SIGINT, sigint_handler);
				signal(SIGTSTP, SIG_IGN);
			}

			// Redirecciones de entrada/salida/error
			if (i == 0 && line->redirect_input != NULL)
			{
				int fd = open(line->redirect_input, O_RDONLY);
				if (fd < 0)
				{
					fprintf(stderr, "%s: Error. %s\n", line->redirect_input, strerror(errno));
					exit(EXIT_FAILURE);
				}
				dup2(fd, STDIN_FILENO);
				close(fd);
			}
			if (i == n - 1 && line->redirect_output != NULL)
			{
				int fd = open(line->redirect_output, O_WRONLY | O_CREAT | O_TRUNC, 0644);
				if (fd < 0)
				{
					fprintf(stderr, "%s: Error. %s\n", line->redirect_output, strerror(errno));
					exit(EXIT_FAILURE);
				}
				dup2(fd, STDOUT_FILENO);
				close(fd);
			}
			if (line->redirect_error != NULL)
			{
				int fd = open(line->redirect_error, O_WRONLY | O_CREAT | O_TRUNC, 0644);
				if (fd < 0)
				{
					fprintf(stderr, "%s: Error. %s\n", line->redirect_error, strerror(errno));
					exit(EXIT_FAILURE);
				}
				dup2(fd, STDERR_FILENO);
				close(fd);
			}
			if (i > 0)
			{
				dup2(pipes[i - 1][0], STDIN_FILENO);
			}
			if (i < n - 1)
			{
				dup2(pipes[i][1], STDOUT_FILENO);
			}

			for (int j = 0; j < n - 1; j++)
			{
				close(pipes[j][0]);
				close(pipes[j][1]);
			}

			// Comprobar si el comando está en la lista de comandos prohibidos
			if (string_in_list(line->commands[i].argv[0]) == 1)
			{
				exit(EXIT_FAILURE);
			}
			execvp(line->commands[i].argv[0], line->commands[i].argv);
			perror("execvp");
			exit(EXIT_FAILURE);
		}
		else
		{
			// Padre
			if (i == 0)
			{
				pgid = pid;
			}
			// Asigna grupo de procesos a todos los hijos
			setpgid(pid, pgid);
		}
	}

	// Cerrar los pipes en el padre
	for (i = 0; i < n - 1; i++)
	{
		close(pipes[i][0]);
		close(pipes[i][1]);
	}

	if (line->background) {
		// Comando en segundo plano
		add_job(pgid, line->commands[0].argv[0], RUNNING);  // solo primer comando si es 1
		printf("[%d]\n", pgid);
	} else {
		//Foreground command
		int status;
		waitpid(pgid, &status, WUNTRACED);

		if (WIFSTOPPED(status)) {
			printf("\n[%d]+  Stopped\t%s\n", job_count + 1, line->commands[0].argv[0]);
			add_job(pgid, line->commands[0].argv[0], STOPPED);
		} else {
			// Foreground terminado 
			tcsetpgrp(STDIN_FILENO, getpid()); // Recuperar control del terminal
		}
	}
}

int main()
{

	signal(SIGINT, sigint_handler);
	signal(SIGTSTP, sigtstp_handler);
	signal(SIGTTOU, SIG_IGN);

	job_list = malloc(job_capacity * sizeof(job_t));

	printf("msh> ");
	while (fgets(buf, sizeof(buf), stdin) != NULL)
	{

		line = tokenize(buf);

		if (line->ncommands > 0)
		{
			// Comprobar que es cd
			if (strcmp(line->commands[0].argv[0], "cd") == 0)
			{
				char cwd[1024];
				if (line->commands[0].argc > 1)
				{
					chdir(line->commands[0].argv[1]);
					getcwd(cwd, sizeof(cwd));
					printf("%s\n", cwd);
				}
				else
				{
					// Reedirigir a HOME
					const char *home = getenv("HOME");
					chdir(home);
					getcwd(cwd, sizeof(cwd));
					printf("%s\n", cwd);
				}
			}
			// Comando exit
			else if (strcmp(line->commands[0].argv[0], "exit") == 0)
			{
				free(job_list);
				printf("Saliendo de msh...\n");
				exit(0);
			// Comando jobs
			} else if (strcmp(line->commands[0].argv[0], "jobs") == 0)
			{
				clean_finished_jobs();
				cmd_jobs();
			//Comando bg
			} else if (strcmp(line->commands[0].argv[0], "bg") == 0){
				clean_finished_jobs();

				if (line->commands[0].argc > 1){
					cmd_bg(atoi(line->commands[0].argv[1]));
				} else {
					// Si no se especifica un job_id, reanudar el último detenido
					cmd_bg(0);
				}

			}else if (strcmp(line->commands[0].argv[0], "umask") == 0)
			{
				cmd_umask(line);
			}

			// Resto de comandos
			else
			{
				// Comprobar existencia del comando

				if (line->ncommands > 1)
				{
					pipe_commands(line);
				}
				// Un comando
				else
				{
					if (command_exists(line->commands[0].argv[0]) == 0)
					{
						fprintf(stderr, "%s: No se encuentra el mandato\n", line->commands[0].argv[0]);
					}
					else
					{
						pid_t pid = fork();
						if (pid == 0)
						{
							// Hijo
							setpgid(0, 0);
							if (!line->background)
							{
								tcsetpgrp(STDIN_FILENO, getpid());
								signal(SIGINT, SIG_DFL);
								signal(SIGTSTP, SIG_DFL);
							}
							else
							{
								signal(SIGINT, sigint_handler);
							}

							// Reedirigir entrada de archivo
							if (line->redirect_input != NULL)
							{
								int fd = open(line->redirect_input, O_RDONLY);
								if (fd < 0)
								{
									fprintf(stderr, "%s: Error. %s\n", line->redirect_input, strerror(errno));
									exit(EXIT_FAILURE);
								}
								dup2(fd, STDIN_FILENO);
								close(fd);
							}

							// Reedirigir salida a archivo
							if (line->redirect_output != NULL)
							{
								int fd = open(line->redirect_output, O_WRONLY | O_CREAT | O_TRUNC, 0644);

								if (fd < 0)
								{
									fprintf(stderr, "%s: Error. %s\n", line->redirect_output, strerror(errno));
									exit(EXIT_FAILURE);
								}
								dup2(fd, STDOUT_FILENO);
								close(fd);
							}

							// Reedirigir error y salida estandar
							if (line->redirect_error != NULL)
							{
								int fd = open(line->redirect_error, O_WRONLY | O_CREAT | O_TRUNC, 0644);
								if (fd < 0)
								{
									fprintf(stderr, "%s: Error. %s", line->redirect_error, strerror(errno));
									exit(EXIT_FAILURE);
								}
								dup2(fd, STDERR_FILENO);
								close(fd);
							}

							execvp(line->commands[0].argv[0], line->commands[0].argv);
							perror("execvp error");
							exit(EXIT_FAILURE);
						}
						else
						{
							// Padre

							// Dar control terminal al hijo
							setpgid(pid, pid);
							if (!line->background)
							{
								tcsetpgrp(STDIN_FILENO, pid);
							}

							int status;
							if (line->background)
							{
								add_job(pid, line->commands[0].argv[0], RUNNING);
								printf("[%d]\n", pid);
							}
							else
							{
								waitpid(pid, &status, WUNTRACED);
								if (WIFSTOPPED(status)) {
									add_job(pid, line->commands[0].argv[0], STOPPED);
								}
								// recuperar control de terminal
								tcsetpgrp(STDIN_FILENO, getpid());
							}
						}
					}
				}
			}
		}

		printf("msh> ");
	}
	return 0;
}

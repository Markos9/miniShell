#include <stdio.h>
#include <stdlib.h>
#include "parser.h"
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>

char buf[1024];
tline *line;

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

	for (i = 0; i < n - 1; i++)
	{
		close(pipes[i][0]);
		close(pipes[i][1]);
	}

	if (!line->background)
	{
		tcsetpgrp(STDIN_FILENO, pgid);
		for (i = 0; i < n; i++)
		{
			wait(NULL);
		}
		// Recuperar control de la terminal
		tcsetpgrp(STDIN_FILENO, getpid());
	}
	else
	{
		// En background, no esperamos y no cambiamos control terminal
		printf("[%d]\n", pgid); // Mostrar grupo de procesos para info usuario
	}
}

int main()
{

	signal(SIGINT, sigint_handler);
	signal(SIGTSTP, sigtstp_handler);
	signal(SIGTTOU, SIG_IGN);

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
				printf("Saliendo de msh...\n");
				exit(0);
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
								printf("[%d]\n", pid);
							}
							else
							{
								waitpid(pid, &status, WUNTRACED);
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

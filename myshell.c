#include <stdio.h>
#include <stdlib.h>
#include "parser.h"
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

char buf[1024];
tline *line;

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

int main()
{
	printf("msh> ");
	while (fgets(buf, sizeof(buf), stdin) != NULL)
	{

		line = tokenize(buf);

		if (line->ncommands > 0)
		{
			// Comprobar que es cd
			if (strcmp(line->commands[0].argv[0], "cd") == 0)
			{
			}
			else
			{
				// Comprobar existencia del comando
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
						execvp(line->commands[0].argv[0], line->commands[0].argv);
						perror("execvp error");
						exit(EXIT_FAILURE);
					}
					else
					{
						int status;
						waitpid(pid, &status, 0);
					}
				}
			}
		}

		printf("msh> ");
	}
	return 0;
}

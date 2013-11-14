#include <sys/types.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#define STR_SIZE 128
#define PARAM_COUNT 4
#define BUF_SIZE 1024
#define INPUT_STR "$ "

/* Todo:
 * -New line after CTR-C, need to add general child's return status checker
 * -Check if jobs operations are successfull
 * -Commands and redirectors written together
*/

typedef enum { RET_OK, RET_ERR, RET_EOF, RET_MEMORYERR } ret_result_t;
typedef enum { ST_NONE, ST_RUNNING, ST_DONE, ST_STOPPED, ST_JUSTSTP } status_t;

typedef struct
{
	char *job;
	int pgid;
	status_t status;
} job_t;

/**
 * Checks if there is enough allocated space to add another charachter to a string and allocates new
 * memory if necessary
 * @param str 	pointer to a string
 * @param len 	new string length
 * @return		0 on success, 1 on error
 */
int checkStringLen (char **str, int len)
{
	char *ptr;

	if (len % STR_SIZE > 0)
		return 0;
	if ((ptr = realloc (*str, (len + STR_SIZE) * sizeof (char))) == NULL)
		return 1;

	*str = ptr;
	return 0;
}

/**
 * Checks if there is enough allocated space to add another string to a string array and allocates
 * new memory if necessary
 * @param params	pointer to a string array
 * @param nparams	new strings count
 * @return 			0 on success, 1 on error
 */
int checkParamCnt (char ***params, int nparams)
{
	char **ptr;

	if (nparams % PARAM_COUNT > 0)
		return 0;
	if ((ptr = realloc (*params, (nparams + PARAM_COUNT) * sizeof (char *))) == NULL)
		return 1;

	*params = ptr;
	return 0;
}

/**
 * Adds null terminator to a string and allocates new memory if necessary
 * @param str		pointer to a string
 * @param len		current string length
 * @return			0 on success, 1 on error
 */
int endString (char **str, int len)
{
	char *ptr;

	if (len % STR_SIZE == 0)
	{
		if ((ptr = realloc (*str, (len + 1) * sizeof (char))) == NULL)
			return 1;
		*str = ptr;
	}

	(*str)[len] = '\0';
	return 0;
}

/**
 * Adds character to a string, allocates new memory if necessary and increases len by 1
 * @param str	pointer to a string
 * @param len	pointer to a current string length
 * @param ch	character to add
 * @return		0 on success, 1 on error
 */
int addChar (char **str, int *len, char ch)
{
	if (checkStringLen (str, *len))
		return 1;
	(*str)[(*len)++] = ch;

	return 0;
}

/**
 * Adds string to a string array, allocates new memory if necessary and increases nparams by 1
 * @param params	pointer to a string array
 * @param nparams	pointer to a current parameters count
 * @return			0 on success, 1 on error
 */
int addParam (char ***params, int *nparams)
{
	if (*nparams)
		if (checkParamCnt (params, *nparams + 1))
			return 1;
	if (((*params)[(*nparams)++] = calloc (STR_SIZE, sizeof (char))) == NULL)
		return 1;

	return 0;
}

int addJob (job_t **jobs, int *njobs, char **command, int nparams, int pgid, status_t status)
{
	int len = nparams + 1, i;
	job_t *ptr;

	if ((ptr = realloc (*jobs, (*njobs + 1) * sizeof (job_t))) == NULL)
		return 1;

	*jobs = ptr;
	ptr = *jobs + *njobs;
	ptr->pgid = pgid;
	ptr->status = status;

	for (i = 0; i < nparams; ++i)
		len += strlen (command[i]);
	if ((ptr->job = calloc (len + 1, sizeof (char))) == NULL)
		return 1;

	len = 0;
	for (i = 0; i < nparams; ++i)
	{
		strcpy (ptr->job + len, command[i]);
		len = strlen (ptr->job);
		ptr->job[len] = ' ';
		ptr->job[++len] = '\0';
	}

	if (status == ST_RUNNING)
		printf ("[%d] %d\n", *njobs + 1, ptr->pgid);
	++(*njobs);
	return 0;
}

int deleteJob (job_t **jobs, int *njobs, int n) /* CHECK RETURN!!!!!!!!! */
{
	int i;
	job_t *ptr;

	free ((*jobs)[n].job);
	(*jobs)[n].job = NULL;
	(*jobs)[n].status = ST_NONE;
	if (n == *njobs - 1)
	{
		i = n;
		while (((*jobs)[i].status == ST_NONE) && --i >= 0);
		if ((ptr = realloc (*jobs, (i + 1) * sizeof (job_t))) == NULL && i + 1 > 0)
			return 1;
		*jobs = ptr;
		*njobs = i + 1;
	}

	return 0;
}

void clearJobs (job_t **jobs, int njobs)
{
	int i;

	if (*jobs == NULL)
		return;
	for (i = 0; i < njobs; ++i)
		if ((*jobs)[i].job != NULL)
			free ((*jobs)[i].job);
	free (*jobs);
	*jobs = NULL;
}

void showJobs (job_t *jobs, int njobs, int fullog)
{
	int i;
	char status[8];

	for (i = 0; i < njobs; ++i)
	{
		if (jobs[i].status == ST_NONE || (!fullog && (jobs[i].status == ST_RUNNING || jobs[i].status == ST_STOPPED)))
		    continue;
		switch (jobs[i].status)
		{
			case ST_NONE:	 break;
			case ST_DONE: 	 strcpy (status, "Done");	 break;
			case ST_RUNNING: strcpy (status, "Running"); break;
			case ST_JUSTSTP: jobs[i].status = ST_STOPPED;
			case ST_STOPPED: strcpy (status, "Stopped"); break;
		}
		printf ("[%d] %s\t\t%s\n", i + 1, status, jobs[i].job);
	}
}

int checkJobs (job_t **jobs, int *njobs, int fullog)
{
	int i, status;
	pid_t pid;
	job_t *job;

	for (i = 0; i < *njobs; ++i)
	{
		job = *jobs + i;
		if (job->status == ST_NONE)
			continue;
		while ((pid = waitpid (-job->pgid, &status, WNOHANG | WUNTRACED | WCONTINUED)) > 0)
			if (job->pgid == pid)
			{
				if (WIFSTOPPED (status))
					job->status = ST_JUSTSTP;
				else if (WIFCONTINUED (status))
					job->status = ST_RUNNING;
				else
					job->status = ST_DONE;
			}
	}
	showJobs (*jobs, *njobs, fullog);
	for (i = 0; i < *njobs; ++i)
		if ((*jobs)[i].status == ST_DONE)
			if (deleteJob (jobs, njobs, i))
				return 1;

	return 0;
}

/**
 * Reads character string from stdin and divides it into substrings, separated by space characters,
 * considering quotes, backslash-escaping and comments
 * @param params	pointer to a string array to store substrings
 * @param nparams	pointer to an integer to return substring count
 * @return			RET_OK - command is correct
 *					RET_ERR - wrong command format
 *					RET_EOF - EOF found
 *					RET_MEMORYERR - memory allocation error
 */
ret_result_t readCommand (char ***params, int *nparams)
{
	enum { IN_INITIAL, IN_WORD, IN_BETWEEN, IN_SCREEN, IN_QUOTES, IN_COMMENT, IN_ERROR } state = IN_INITIAL, previous;
	int ch, len = 0;

	*nparams = 0;
	if ((*params = calloc (PARAM_COUNT, sizeof (char *))) == NULL)
		return RET_MEMORYERR;

	while (1)
	{
		ch = getchar ();
		if (ch == EOF && state != IN_INITIAL)
			continue;
		switch (state)
		{
			case IN_INITIAL:
				if (ch == '\n' || ch == '#' || ch == EOF)
				{
					if (addParam (params, nparams))
						return RET_MEMORYERR;
					(*params)[0][0] = '\0';
					if (ch == EOF)
						return RET_EOF;
				}

			case IN_BETWEEN:
				if (ch == '\n')
					return RET_OK;
				else if (ch == '\\')
				{
					previous = IN_WORD;
					state = IN_SCREEN;
					if (addParam (params, nparams))
						return RET_MEMORYERR;
					len = 0;
				}
				else if (ch == '#')
					state = IN_COMMENT;
				else if (ch == '"')
				{
					state = IN_QUOTES;
					if (addParam (params, nparams))
						return RET_MEMORYERR;
					len = 0;
				}
				else if (isspace (ch))
					;
				else
				{
					state = IN_WORD;
					if (addParam (params, nparams))
						return RET_MEMORYERR;
					len = 1;
					(*params)[*nparams - 1][0] = ch;
				}
				break;

			case IN_WORD:
				if (ch == '\n')
				{
					if (endString (*params + *nparams - 1, len))
						return RET_MEMORYERR;
					return RET_OK;
				}
				else if (ch == '\\')
				{
					previous = IN_WORD;
					state = IN_SCREEN;
				}
				else if (ch == '#')
				{
					state = IN_COMMENT;
					if (endString (*params + *nparams - 1, len))
						return RET_MEMORYERR;
				}
				else if (ch == '"')
					state = IN_ERROR;
				else if (isspace (ch))
				{
					state = IN_BETWEEN;
					if (endString (*params + *nparams - 1, len))
						return RET_MEMORYERR;
				}
				else
					if (addChar (*params + *nparams - 1, &len, ch))
						return RET_MEMORYERR;
				break;

			case IN_SCREEN:
				if (ch == '\n')
					return RET_ERR;
				else if (ch == '\\' || ch == '#' || ch == '"')
				{
					state = previous;
					if (addChar (*params + *nparams - 1, &len, ch))
						return RET_MEMORYERR;
				}
				else
					state = IN_ERROR;
				break;

			case IN_QUOTES:
				if (ch == '\n')
					return RET_ERR;
				else if (ch == '\\')
				{
					previous = IN_QUOTES;
					state = IN_SCREEN;
				}
				else if (ch == '"')
				{
					state = IN_WORD;
					if (endString (*params + *nparams - 1, len))
						return RET_MEMORYERR;
				}
				else
					if (addChar (*params + *nparams - 1, &len, ch))
						return RET_MEMORYERR;
				break;

			case IN_COMMENT:
				if (ch == '\n')
					return RET_OK;
				break;

			case IN_ERROR:
				if (ch == '\n')
					return RET_ERR;
				break;
		}
	}
}

/**
 * Frees allocated string array
 * @param params	pointer to a string array
 * @param nparams	strings count
 */
void clearStrings (char ***params, int nparams)
{
	int i;

	if (params == NULL)
		return;
	for (i = 0; i < nparams; ++i)
		free ((*params)[i]);
	free (*params);

	*params = NULL;
}

/**
 * Changes environmental variable names prefixed with $ to their values
 * @param params	string array
 * @param nparams	strings count
 * @return			0 on success, 1 on error
 */
int placeEnv (char **params, int nparams)
{
	int i;
	for (i = 0; i < nparams; ++i)
	{
		int len = strlen (params[i]), posp = 0, poss = 0, ch;
		char *s;

		if (len < 2 || !strchr (params[i], '$'))
			continue;
		if ((s = calloc (STR_SIZE, sizeof (char))) == NULL)
			return 1;

		while (posp < len)
		{
			char *env;
			int start;

			while (posp < len && params[i][posp] != '$')
			{
				if (checkStringLen (&s, poss))
					return 1;
				s[poss++] = params[i][posp++];
			}
			if (posp == len)
				break;
			start = posp + 1;

			while (++posp < len && isalpha ((int)params[i][posp]));

			ch = params[i][posp];
			params[i][posp] = '\0';
			if ((env = getenv (params[i] + start)) != NULL)
			{
				if ((poss + strlen (env) + 1) / STR_SIZE > poss / STR_SIZE)
				{
					char *ptr;
					if ((ptr = realloc (s, ((poss + strlen (env) + 1) / STR_SIZE + 1) * STR_SIZE * sizeof (char))) == NULL)
						return 1;
					s = ptr;
				}
				strcpy (s + poss, env);
				poss += strlen (env);
			}
			params[i][posp] = ch;
		}

		if (endString (&s, poss))
			return 1;
		free (params[i]);
		params[i] = s;
	}
	return 0;
}

/**
 * Executes one shell command given in the string array and exits
 * @param command	string array
 * @param nparams	strings count
 */
void executeCommand (char **command, int nparams, job_t *jobs, int njobs)
{
	signal (SIGINT,  SIG_DFL);
	signal (SIGTTOU, SIG_DFL);
	signal (SIGTSTP, SIG_DFL);

	if (!nparams || command[0][0] == '\0')
		exit (0);
	if (!strcmp (command[0], "cd") || !strcmp (command[0], "exit") || !strcmp (command[0], "fg") || !strcmp (command[0], "bg"))
		exit (0);
	if (!strcmp (command[0], "jobs"))
	{
		showJobs (jobs, njobs, 1);
		exit (0);
	}
	if (!strcmp (command[0], "pwd"))
	{
		char *s = getcwd (NULL, 0);
		if (s == NULL)
		{
			perror ("Error getting current directory");
			exit (0);
		}
		puts (s);
		free (s);
		exit (0);
	}

	command[nparams] = NULL;
	execvp (command[0], command);
	perror (command[0]);
	exit (1);
}

/**
 * Checks if a string is a special character
 * @param s			string to check
 * @return			1 if string it is, 0 otherwise
 */
int checkString (char *s)
{
	if (strlen (s) > 2)
		return 0;
	if (strlen (s) == 1)
		return s[0] == '>' || s[0] == '<' || s[0] == '&' || s[0] == ';' || s[0] == '|';
	return !strcmp (s, "<<") || !strcmp (s, ">>") || !strcmp (s, "&&") || !strcmp (s, "||");
}

/**
 * Checks if the syntax given in the command is correct (e.g. there is always a parameter for file
 * input/output redirection)
 * @param params	string array
 * @param nparams	string count
 * @return			0 on correct syntax, 1 on incorrect
 */
int checkSyntax (char **params, int nparams)
{
	int i;

	if (checkString (params[0]) || checkString (params[nparams - 1]))
		return 1;
	for (i = 0; i < nparams - 1; ++i)
		if (checkString (params[i]) && checkString (params[i + 1]))
		{
			printf ("Syntax error near %s\n", params[i]);
			return 1;
		}

	return 0;
}

int isInternal (char **command, int nparams) /* Too many strcmps!! */
{
	int i;

	if (strcmp (command[0], "cd") && strcmp (command[0], "exit") && strcmp (command[0], "jobs") && strcmp (command[0], "fg")
	    && strcmp (command[0], "bg"))
		return 0;
	for (i = 1; i < nparams; ++i)
		if (command[0][0] == '|')
			return 0;

	return 1;
}

int waitProcessGroup (pid_t pgid)
{
	int status, ret = 0;

	tcsetpgrp (STDIN_FILENO, pgid);
	kill (-pgid, SIGCONT);
	while (waitpid (-pgid, &status, WUNTRACED) != -1)
		if (WIFSTOPPED (status))
		{
			ret = 1;
			break; /* Why the hell this break is neccesary??? */
		}
	tcsetpgrp (STDIN_FILENO, getpid ());
	if (ret)
		putchar ('\n');
	return ret;
}

/**
 * Checks if a command given is an internal command, that needs to be executed
 * in the main process, rather than in fork (), and executes it if needed
 * @param command	string array
 * @param nparams	string count
 * @return			1 on exit, 0 on non-exit
 */
int internalCommand (char **command, int nparams, job_t **jobs, int *njobs)
{
	if (!strcmp (command[0], "exit"))
		return 1;
	else if (!strcmp (command[0], "cd"))
	{
		if (nparams == 1)
		{
			if (chdir (getenv ("HOME")))
				perror ("Error changing directory");
		}
		else
			if (chdir (command[1]))
				perror ("Error changing directory");
	}
	else if (!strcmp (command[0], "jobs"))
		checkJobs (jobs, njobs, 1);
	else if (!strcmp (command[0], "fg"))
	{
		int n;
		if (nparams == 1)
			n = *njobs - 1;
		else
			n = atoi (command[1]) - 1;
		if (n < 0 || n >= *njobs)
		{
			puts ("No such job!");
			return 0;
		}
		if (waitProcessGroup ((*jobs)[n].pgid))
			(*jobs)[n].status = ST_JUSTSTP;
		else
			deleteJob (jobs, njobs, n);
	}
	else if (!strcmp (command[0], "bg"))
	{
		int n;
		if (nparams == 1)
		{
			n = *njobs;
			while (--n >= 0 && (*jobs)[n].status != ST_STOPPED);
		}
		else
			n = atoi (command[1]) - 1;
		if (n < 0 || n >= *njobs)
		{
			puts ("No such job!");
			return 0;
		}
		kill (-(*jobs)[n].pgid, SIGCONT);
		printf ("[%d] %s\n", n + 1, (*jobs)[n].job);
	}

	return 0;
}

/**
 * Executes all the commands in the line, redirects input/output if needed and sets childs
 * group id to that of a first process
 * @param params	string array
 * @param nparams	string count
 * @return			process group id
 */
pid_t doCommands (char **params, int nparams, job_t *jobs, int njobs)
{
	pid_t pid, pgid;
	int i = 0, j, conv, cnt, pipes[2][2]={{0}}, fd;
	char **command;

	if (checkSyntax (params, nparams))
		return 0;

	while (i < nparams)
	{
		conv = i;
		cnt = 0;
		while (++conv < nparams && strcmp (params[conv], "|")); /* Change this later! */
		if (i > 0)
		{
			if (pipes[0][0])
				close (pipes[0][0]);
			close (pipes[1][1]);
			memcpy (pipes[0], pipes[1], sizeof (pipes[1]));
		}

		if (conv < nparams)
			pipe (pipes[1]);

		if ((command = calloc (conv - i + 1, sizeof (char *))) == NULL)
			return 0;

		if ((pid = fork ()) < 0)
		{
			perror ("Error creating child process");
			clearStrings (&command, cnt);
			return 0;
		}

		if (i == 0) /* If it is was first fork */
			pgid = pid;

		if (!pid)
		{
			if (i > 0)
			{
				dup2 (pipes[0][0], 0);
				close (pipes[0][0]);
			}
			if (conv < nparams)
			{
				dup2 (pipes[1][1], 1);
				close (pipes[1][0]);
				close (pipes[1][1]);
			}

			for (j = i; j < conv; ++j) /* Cut this a bit? */
				if (!strcmp (params[j], "<") || !strcmp (params[j], "<<"))
				{
					fd = open (params[j + 1], O_RDONLY);
					dup2 (fd, 0);
					close (fd);
					j++;
				}
				else if (!strcmp (params[j], ">"))
				{
					fd = open (params[j + 1], O_WRONLY | O_CREAT | O_TRUNC, 0666);
					dup2 (fd, 1);
					close (fd);
					j++;
				}
				else if (!strcmp (params[j], ">>"))
				{
					fd = open (params[j + 1], O_WRONLY | O_CREAT | O_APPEND, 0666);
					dup2 (fd, 1);
					close (fd);
					j++;
				}
				else
				{
					command[cnt++] = calloc (strlen (params[j]) + 1, sizeof (char));
					strcpy (command[cnt - 1], params[j]);
				}

			command[cnt] = NULL;
			setpgid (0, pgid);
			executeCommand (command, cnt, jobs, njobs);
		}

		setpgid (pid, pgid);
		clearStrings (&command, cnt);
		i = conv + 1;
	}

	if (pipes[0][0])
		close (pipes[0][0]);

	return pgid;
}

int doJobs (char **params, int nparams, job_t **jobs, int *njobs) /* Also MEMORY ERRORS! */
{
	int i = 0, divider = 0;
	pid_t pgid;
	while (i < nparams)
	{
		while (++divider < nparams && strcmp (params[divider], "&")); /* Change this too! */

		/* Internal command */
		if (divider == nparams && isInternal (params + i, divider - i))
		{
			if (internalCommand (params + i, divider - i, jobs, njobs))
				return 1;
			return 0;
		}

		pgid = doCommands (params + i, divider - i, *jobs, *njobs);

		/* Foreground command */
		if (divider == nparams)
		{
			if (waitProcessGroup (pgid))
				if (addJob (jobs, njobs, params + i, divider - i, pgid, ST_JUSTSTP))
					return 0;
		}
		/* Background command */
		else
			if (addJob (jobs, njobs, params + i, divider - i, pgid, ST_RUNNING))
				return 0;
		i = ++divider;
	}

	return 0;
}

/**
 * Initializes some environmental variables
 * @return		0 on success, 1 on error
 */
int setEnvVars ()
{
	char buf[BUF_SIZE];
	int len;

	if ((len = readlink ("/proc/self/exe", buf, BUF_SIZE - 1)) == -1)
		return 1;
	buf[len] = '\0';
	if (setenv ("SHELL", buf, 1) == -1)
		return 1;

	sprintf (buf, "%d", geteuid ());
	if (setenv ("EUID", buf, 1) == -1)
		return 1;

	if (getlogin_r (buf, BUF_SIZE) || setenv ("USER", buf, 1) == -1)
		return 1;

	return 0;
}

int main ()
{
	int nparams, njobs = 0;
	char **params = NULL;
	job_t *jobs = NULL;
	ret_result_t ret;

	signal (SIGINT, SIG_IGN);
	signal (SIGTTOU, SIG_IGN);
	signal (SIGTSTP, SIG_IGN);

	if (setEnvVars ())
	{
		perror ("Initialization error");
		return 1;
	}

	printf (INPUT_STR);

	while ((ret = readCommand (&params, &nparams)) < RET_EOF)
	{
		if (ret == RET_ERR)
			puts ("Wrong format!");
		else
		{
			if (placeEnv (params, nparams))
				continue;
			if (doJobs (params, nparams, &jobs, &njobs))
				break;
		}

		clearStrings (&params, nparams);
		if (checkJobs (&jobs, &njobs, 0))
		{
			ret = RET_MEMORYERR;
			break;
		}
		printf (INPUT_STR);
	}

	if (ret == RET_MEMORYERR)
	{
		putchar ('\n');
		puts ("Memory allocation error!");
	}

	clearStrings (&params, nparams);
	clearJobs (&jobs, njobs);

	return 0;
}

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <pwd.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>

/* Tamaño máximo que puede tener la entrada */
#define MAX_INPUT 1024

/* fichero de entrada salida y error estandar */
#define std_in 0
#define std_out 1
#define std_err 2

/* Almacenamos el directorio actual y el anterior accedido */
char *current_directory = NULL;
char *old_directory = NULL;

static struct option long_options[] = {
	{ .name = "help",
	  .has_arg = no_argument,
	  .flag = NULL,
	  .val = 0},
	{0, 0, 0, 0}
};

static void usage(int i)
{
	printf(
		"Usage:  minishell\n"
		"Pequeño shell\n"
		"Opciones:\n"
		"  -h, --help: muestra esta ayuda\n\n"
	);
	exit(i);
}

static void handle_long_options(struct option option, char *arg)
{
	if (!strcmp(option.name, "help"))
		usage(0);
}

static int handle_options(int argc, char **argv)
{
	while (true) {
		int c;
		int option_index = 0;

		c = getopt_long (argc, argv, "lhRsairS",
				 long_options, &option_index);
		if (c == -1)
			break;

		switch (c) {
		case 0:
			handle_long_options(long_options[option_index],
				optarg);
			break;

		case '?':
		case 'h':
			usage(0);
			break;

		default:
			printf ("?? getopt returned character code 0%o ??\n", c);
			usage(-1);
		}
	}
	return 0;
}

int echo_handler(char *args[])
{/* display a line of text */
   int i = 0;
   while (args[i] != NULL) {
      printf("%s ", args[i]);
      i++;
   }
   printf("\n");
   
	return 0;
}

int exit_handler(char *args[])
{/* cause normal process termination */
   if (current_directory != NULL)
      free(current_directory);
   if (old_directory != NULL)
      free(old_directory);
	printf("exiting minishell\n");
	printf("goodbye\n");
	exit(EXIT_SUCCESS);
	return 0;
}

int pwd_handler(char *args[])
{/* print name of current/working directory */
   printf("%s\n", current_directory);
   
   return 0;
}

/* tipo de datos de los comandos builtin */
struct builtin_command {
	char *name;
	int (*handler)(char **);
};

void update_directory(char **old, char **current, char *new)
{/* update directory's variables */
   char *aux = strdup(new);
   if (*old != NULL)
      free(*old);
   
   *old = *current;
   *current = aux;
}

int cd_home()
{/* change current directory to home directory */
   uid_t uid = getuid();
   struct passwd *passwd = getpwuid(uid);
   if (passwd == NULL) {
      perror("getpwuid");
      return -1;
   }

   if (chdir(passwd->pw_dir) != 0){
      perror("cd_home");
      return -1;
   }
   
   update_directory(&old_directory, &current_directory, passwd->pw_dir);
   
   return 0;
}

int cd_previous()
{/* change current directory to previous directory */
   if (old_directory == NULL) {
      printf("bash: cd: OLDPWD not set\n");
      return -1;
   }
   
   if (chdir(old_directory)!=0) {
      perror("cd_previous");
      return -1;
   }
   
   update_directory(&old_directory, &current_directory, old_directory);
   return 0;
}

int cd_commun(char *directory)
{/* change current directory to 'directory' */
   if (chdir(directory) != 0) {
      perror("cd_commun");
      return -1;
   }
   
   char *tmp = realpath(directory, NULL);
   update_directory(&old_directory, &current_directory, tmp);
   free(tmp);
   
   return 0;
}

int cd_handler(char *args[])
{/* handler change directory */
   if (args[0] == NULL) {//cd
      return cd_home();
   }   

   if (!strcmp("-", args[0])) {//cd -
      return cd_previous();
   }
   
   return cd_commun(args[0]);//cd directory
}

char *concatenate(char *s1, char *s2, char *s3)
{/* return s1+s2+s3 */
/* FREE MEMORY IS NEEDED */
   size_t size = strlen(s1) + strlen(s2) + strlen(s3);
   char *aux = malloc(sizeof(char) * (size + 1));
   if (aux == NULL) {
      perror("concatenate");
      return NULL;
   }
   
   strcpy(aux, s1);
   strcat(aux, s2);
   return strcat(aux, s3);
}

char *search_path(char *file)
{/*try to find path of file */
/* FREE MEMORY IS NEEDED */
   char *path = strdup(getenv("PATH"));/* creamos una copia del path para no modificar el original */

   char *dir = strtok(path, ":");/* cortamos la copia del path para coger un directorio uno a uno */
   char *aux;
   struct stat stat;
   while (dir != NULL) {
      aux = concatenate(dir, "/", file);
      if (aux == NULL)/* without memory */
         return NULL;

      if (lstat(aux, &stat) == 0){ /*i find it*/
         free(path);
         return aux;
      }
      
      free(aux);
      dir = strtok(NULL, ":");/* and... the next? */
   }
   
   return NULL;
}

char *catch_name(char *args[], char *s)
{/* search 's' in 'args'. return the next elemment */
   int i;
   for (i = 0; args[i] != NULL; i++) {
      if (!strcmp(s, args[i]))
         return (args[i+1]);
   }
   
   return NULL;
}

void quit_args(char *args[])
{/* unnecessary arguments is quitted */
   int i;
   
   for (i = 0; args[i] != NULL; i++) {
      if ((!strcmp(args[i], ">")) || (!strcmp(args[i], "<")) || (!strcmp(args[i], "#"))) {
         args[i] = NULL;
         return;
      }
   }
}

int duplicate_std(char *file, int fd_std)
{/* create a copy of the 'fd_std' */
	int fd;
	int flags;
	
	switch (fd_std) {
		case std_in: flags = O_RDONLY;
					break;
		case std_out:
		case std_err: flags = O_WRONLY | O_CREAT;
					 break;
		 default:
		 	flags = O_RDWR | O_CREAT; /* why not? */
	}
	
	if ((fd = open (file, flags, 0644))==-1) {
		perror("open");
		return -1;
	}
	if (close(fd_std) == -1) {
		perror("close");
		return -1; /* Cierra entrada/salida/error (0/1/2) */
	}
	if (dup(fd) == -1) {
		perror("dup");
		return -1;   /* duplica la que se halla cerrado */
	}
	return 0;
}

void duplicate(char *input, char *output, char *error)
{/* handler duplicate */
   if (output != NULL)
      duplicate_std(output, std_out);
   if (input != NULL)
      duplicate_std(input, std_in);
   if (error != NULL)
      duplicate_std(error, std_err);
}

int exec_handler(char *args[], bool bg)
{/* execute 'args[0]' */
/* bg = true execute in background otherwise in foreground */
   char *path = search_path(args[0]);
   if (path == NULL)
      return -1;
   
   pid_t pid = fork();
   if (pid == -1) {
      perror("fork");
      return -1;
   }
   
   if (pid == 0){ /* son */
      char *input, *output, *error;
      input = output = error = NULL;
      input = catch_name(args, "<");
      output = catch_name(args, ">");
      error = catch_name(args, "#");
      quit_args(args);
      duplicate(input, output, error);
      exit(execv(path, args));
   } else /* father */
      if (!bg)
         waitpid(pid, NULL, 0);
      
   free(path);
   
   return 0;
}

/* builtin_command */
struct builtin_command builtin[] = {
	{"echo",echo_handler,},
	{"exit",exit_handler,},
	{"pwd", pwd_handler,},
	{"cd", cd_handler,},
	
	{NULL, NULL}
};

bool is_background(char *args[])
{/* 'args' contains "&" */
   int i;
   
   for (i = 0; args[i] != NULL; i++)
   {
      if (strcmp("&", args[i])==0) {
         args[i] = NULL;
         return true;
      }
   }
   
   return false;
}

int handle_command(char *commandline)
{
	int i;
	bool bg = false;
	char *args[MAX_INPUT/4];
   const char separate[] = {" \t\n"};
   
   /* Separamos los argumentos */
   args[0] = strtok(commandline, separate);
   for (i = 1; ((args[i] = strtok(NULL, separate)) != NULL); i++)
      ;
   
   if (args[0] == NULL)
      return 0;
   
	for (i = 0; builtin[i].name != NULL; i++) {
	   if (!strcmp(args[0], builtin[i].name))
	      return builtin[i].handler(args+1);
	}
	
	/* No es ningun 'builtin' que tiene nuestro shell, pues ejecutarlo del path */
	bg = is_background(args);
	int ret = exec_handler(args, bg);
	if (ret != 0) {
	   printf("%s: command not found\n", args[0]);
	}
	
	return ret;
}

char *pwd()
{/* return current directory */
/* FREE MEMORY IS NEEDED */
   return realpath(".", NULL);
}

int main(int argc, char *argv[])
{
	int result = handle_options(argc, argv);
	pid_t shell_pgid;

	if (result != 0)
		exit(result);

	/* Loop until we are in the foreground.  */
	while (tcgetpgrp(STDIN_FILENO) != (shell_pgid = getpgrp()))
		kill(-shell_pgid, SIGTTIN);	

	/* Ignore interactive and job-control signals.  */
	signal(SIGINT, SIG_IGN);
	signal(SIGQUIT, SIG_IGN);
	signal(SIGTSTP, SIG_IGN);
	signal(SIGTTIN, SIG_IGN);
	signal(SIGTTOU, SIG_IGN);

	/* Put ourselves in own our process group */
	printf("pid %d pgid %d\n", getpid(), getpgrp());

	printf("minishell v0.1\n");

   current_directory = pwd(); /* save current directory */

	while (true) {
		char string[MAX_INPUT];
		char *p;

		printf("prompt $ ");
		p = fgets(string, MAX_INPUT, stdin);
		if (p == NULL) {
			if (!feof(stdin)) {
				perror("error reading command");
			}
			exit(1);
		}
		handle_command(p);
	}

	exit(0);
}

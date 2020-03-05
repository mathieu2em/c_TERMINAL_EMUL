/**
 * Mathieu Perron 20076170
 * Amine   Sami   2008635
 */

#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include <pthread.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include <ctype.h>

#define true 1
#define false 0

#define MIN_SIZE 128
#define IS_SPECIAL(c) ((c) == '&' || (c) == '|')

typedef unsigned char bool;
typedef struct command_struct command;
typedef struct command_chain_head_struct command_head;

typedef enum {
    BIDON, NONE, OR, AND, ALSO, NORMAL
} operator;

struct command_struct {
    char **argv;
    int *ressources;
    int call_size;
    int count;
    operator type; // in template was named p
    //command *next; // from template
    char rnfn;
    int n;
};

struct cmdline {
    command *commands;
    bool is_background;
};

// from new template , maybe this was for a linked list , we are using an array
struct command_chain_head_struct {
    int *max_resources;
    int max_resources_count;
    command *command;
    pthread_mutex_t *mutex;
    bool background;
};

// Forward declaration
typedef struct banker_customer_struct banker_customer;

struct banker_customer_struct {
    command_head *head;
    banker_customer *next;
    banker_customer *prev;
    int *current_resources;
    int depth;
};

typedef int error_code;
#define HAS_ERROR(err) ((err) < 0)
#define HAS_NO_ERROR(err) ((err) >= 0)
#define NO_ERROR 0
#define CAST(type, src)((type)(src))

typedef struct {
    char **commands;
    int *command_caps;
    unsigned int command_count;
    unsigned int ressources_count;
    int file_system_cap;
    int network_cap;
    int system_cap;
    int any_cap;
} configuration;

int insert_char (char *, int, char, int);
char *readLine (void);
char **tokenize (char *);
struct cmdline parse (char **);
int execute_cmd (command);
int execute (struct cmdline);

const char* syntax_error_fmt = "bash: syntax error near unexpected token `%s'\n";

// ---------------------------------------------------------------------------------------------------------------------
// ---------------------------------------------------------------------------------------------------------------------
// ---------------------------------------------------------------------------------------------------------------------

// Configuration globale
configuration *conf = NULL;

int insert_char (char *str, int pos, char c, int len)
{
    char *saved;
    if (pos == len - 1) {
        len *= 2;
        str = (char*) realloc(saved = str, sizeof(char) * len);
        if (!str) {
            fprintf(stderr, "string could not be extended\n");
            free(saved);
            return -1;
        }
    }
    str[pos] = c;
    return len;
}

/* readline allocate a new char array */
char* readLine (void) {
    char *line;
    int c, p, n = 0, len = MIN_SIZE;

    line = (char*) malloc(sizeof(char) * len); /* initialize array */
    if (!line) {
        fprintf(stderr, "could not initialize string\n");
        return NULL;
    }

    while ((c = getchar()) != EOF && c != '\n') {
        if (IS_SPECIAL(c)) {
            len = insert_char(line, n++, ' ', len);
            if (len < 0)
                return NULL;

            len = insert_char(line, n++, (char) (p = c), len);
            if (len < 0)
                return NULL;

            while ((c = getchar()) == p) {
                len = insert_char(line, n++, (char) c, len);
                if (len < 0)
                    return NULL;
            }

            if (c == EOF || c == '\n')
                break;

            len = insert_char(line, n++, ' ', len);
        }

        len = insert_char(line, n++, (char) c, len);
        if (len < 0)
            return NULL;
    }

    if (n == 0) {
        free(line);
        exit(0);
    }

    line[n] = '\0'; /* null terminate string */

    return line;
}

/* tokenize arguments */
char **tokenize(char *str){
    const char *delim = " \t\n";
    char **tokens, **saved, *next_tok;
    int i = 0, len = MIN_SIZE;

    /* get the first token */
    next_tok = strtok(str, delim);

    tokens = (char**) malloc(sizeof(char*) * len);
    if (!tokens) {
        fprintf(stderr, "could not initialize tokens array\n");
        return NULL;
    }

    /* walk through other tokens */
    while (next_tok) {
        if (i == len - 1) {
            len *= 2;
            tokens = (char**) realloc(saved = tokens, sizeof(char*) * len);
            if (!tokens) {
                fprintf(stderr, "could not resize tokens array\n");
                free(saved);
                return NULL;
            }
        }
        tokens[i++] = next_tok;
        /* subsequent calls to strtok with the same string must pass NULL */
        next_tok = strtok(NULL, delim);
    }

    tokens[i] = NULL;

    return tokens;
}

/* creates the cmdline structure that containes every commands with their
   types and other particularities */
struct cmdline parse (char **tokens) {
    int i, j, k, n = 1;
    char *cp;
    struct cmdline cmd_line;
    cmd_line.is_background = false;

    /* count commands */
    for (i = 0; tokens[i]; i++) {
        if (tokens[i][0] == '&') {
            if (!tokens[i][1]) {
                if (tokens[i+1]) {
                    fprintf(stderr, syntax_error_fmt, tokens[i+1]);
                    cmd_line.commands = NULL;
                    return cmd_line;
                }
            } else if (tokens[i][1] == '&' && !tokens[i][2]) {
                n++;
            } else {
                /*
                 *  && is the only accepted elem starting with &
                 *  if we aren't at the end
                 */
                fprintf(stderr, syntax_error_fmt, tokens[i]);
                cmd_line.commands = NULL;
                return cmd_line;
            }
        } else if (tokens[i][0] == '|') {
            if (tokens[i][1] == '|' && !tokens[i][2]) {
                n++;
            } else {
                /* || is the only accepted elem starting with | */
                fprintf(stderr, syntax_error_fmt, tokens[i]);
                cmd_line.commands = NULL;
                return cmd_line;
            }
        }
    }

    /* now allocate our array of commands */
    cmd_line.commands = malloc(sizeof(command) * (n + 1));
    if (!cmd_line.commands) {
        fprintf(stderr, "lack of memory");
        cmd_line.commands = NULL;
        return cmd_line;
    }

    for (i=0; i<n; i++) {
        cmd_line.commands[i].type = NORMAL;
        cmd_line.commands[i].rnfn = '0';
        cmd_line.commands[i].n = 0;
    }

    /* now create the right structures for commands */
    for (i = j = n = 0; tokens[i]; i++) {
        /* removes parenthesis if rn or fn */
        if ((cmd_line.commands[n].rnfn == 'f' ||
             cmd_line.commands[n].rnfn == 'r') &&
            (cp = strchr(tokens[i], ')'))) {
            *cp = '\0';
        } else if (tokens[i][0] == '&') {
            /* if the argument is a and get arguments before the &&
               and set type to AND */
            if (tokens[i][1])
                cmd_line.commands[n].type = AND;
            else
                cmd_line.is_background = true;
            cmd_line.commands[n++].argv = tokens + j;
            j = i;
            tokens[j++] = NULL;
        } else if (tokens[i][0] == '|' && tokens[i][1]) {
            /* same for || */
            cmd_line.commands[n].type = OR;
            cmd_line.commands[n++].argv = tokens + j;
            j = i;
            tokens[j++] = NULL;
        } else if ((i == 0 || !tokens[i-1]) &&
                   (tokens[i][0]=='r' || tokens[i][0]=='f')) {
            k = 1;
            while(tokens[i][k] && isdigit(tokens[i][k]))
                k++;
            if(isdigit(tokens[i][k-1]) && tokens[i][k] == '(') {
                cmd_line.commands[n].rnfn = tokens[i][0];
                tokens[i][k] = '\0';
                cmd_line.commands[n].n = atoi(tokens[i] + 1);
                if (!tokens[i][k+1]) {
                    tokens[i++] = NULL;
                } else {
                    tokens[i] += k + 1;
                }
                j = i;
            }
        }
    }

    /* when is background command `tokens + j' contains "&" otherwise it
       needs to be added to commands array */
    if (!cmd_line.is_background) {
        cmd_line.commands[n++].argv = tokens + j;
    }

    cmd_line.commands[n].argv = NULL; /* to know where the array ends */
    return cmd_line;
}

int execute_cmd (command cmd) {
    int child_code = 0;
    pid_t pid;

    pid = fork();
    if (pid < 0) {
        fprintf(stderr, "could not fork process\n");
        return -1;
    } else if (pid == 0) {
        child_code = execvp(cmd.argv[0], cmd.argv);
        /* execvp only returns on error */
        fprintf(stderr, "bash: %s: command not found\n", cmd.argv[0]);
    } else {
        waitpid(pid, &child_code, 0);
        child_code = WEXITSTATUS(child_code);
    }

    /* child_code should only be negative if fork or execvp failed */
    return child_code;
}

/*
  execute & fork process
*/
int execute (struct cmdline cmd_line) {
    int i, n, ret;
    pid_t pid;
    command *cmds = cmd_line.commands;

    if (cmd_line.is_background) {
        pid = fork();
        if (pid < 0) {
            /* si le fork a fail */
            fprintf(stderr, "could not fork process\n");
            return -1;
        } else if (pid != 0) {
            return 0;
        }
    }

    for (i = 0; cmds[i].argv; i++) {
        if (cmds[i].rnfn == 'r' || cmds[i].rnfn == 'f') {
            for (n = 0; n < cmds[i].n; n++) {
                ret = (cmds[i].rnfn == 'f') ? 0 : execute_cmd(cmds[i]);
                /* ret is only negative inside child_process which failed */
                if (ret < 0)
                    return -1;
            }
        } else {
            ret = execute_cmd(cmds[i]);
            if (ret < 0) {
                /* here if error in execvp or fork */
                /* exit after, we are inside child process or fork failed */
                return -1;
            }
        }

        if (ret == 0) { /* here if success */
            /* OR should eval until one success */
            if (cmds[i].type == OR) {
                /* skip until && */
                while (cmds[i].argv && cmds[i].type != AND)
                    i++;
                if (!cmds[i].argv)
                    break;
            }
        } else { /* here if failure */
            /* AND should eval until one failure */
            if (cmds[i].type == AND) {
                /* skip until || */
                while (cmds[i].argv && cmds[i].type != OR)
                    i++;
                if (!cmds[i].argv)
                    break;
            }
        }
    }

    return 0;
}

/**
 * Cette fonction analyse la première ligne et remplie la configuration
 * @param line la première ligne du shell
 * @param conf pointeur vers le pointeur de la configuration
 * @return un code d'erreur (ou rien si correct)
 */
error_code parse_first_line(char *line) {
    return NO_ERROR;
}

#define FS_CMDS_COUNT 10
#define FS_CMD_TYPE 0
#define NETWORK_CMDS_COUNT 6
#define NET_CMD_TYPE 1
#define SYS_CMD_COUNTS 3
#define SYS_CMD_TYPE 2

const char *FILE_SYSTEM_CMDS[FS_CMDS_COUNT] = {
        "ls",
        "cat",
        "find",
        "grep",
        "tail",
        "head",
        "mkdir",
        "touch",
        "rm",
        "whereis"
};

const char *NETWORK_CMDS[NETWORK_CMDS_COUNT] = {
        "ping",
        "netstat",
        "wget",
        "curl",
        "dnsmasq",
        "route"
};

const char *SYSTEM_CMDS[SYS_CMD_COUNTS] = {
        "uname",
        "whoami",
        "exec"
};

/**
 * Cette fonction prend en paramètre un nom de ressource et retourne
 * le numéro de la catégorie de ressource
 * @param res_name le nom
 * @param config la configuration du shell
 * @return le numéro de la catégorie (ou une erreur)
 */
error_code resource_no(char *res_name) {
    return NO_ERROR;
}

/**
 * Cette fonction prend en paramètre un numéro de ressource et retourne
 * la quantitée disponible de cette ressource
 * @param resource_no le numéro de ressource
 * @param conf la configuration du shell
 * @return la quantité de la ressource disponible
 */
int resource_count(int resource_no) {
    return 0;
}

// Forward declaration
error_code evaluate_whole_chain(command_head *head);

/**
 * Créer une chaîne de commande qui correspond à une ligne de commandes
 * @param config la configuration
 * @param line la ligne de texte à parser
 * @param result le résultat de la chaîne de commande
 * @return un code d'erreur
 */
error_code create_command_chain(const char *line, command_head **result) {
    return NO_ERROR;
}

/**
 * Cette fonction compte les ressources utilisées par un block
 * La valeur interne du block est mise à jour
 * @param command_block le block de commande
 * @return un code d'erreur
 */
error_code count_ressources(command_head *head, command *command_block) {
    return NO_ERROR;
}

/**
 * Cette fonction parcours la chaîne et met à jour la liste des commandes
 * @param head la tête de la chaîne
 * @return un code d'erreur
 */
error_code evaluate_whole_chain(command_head *head) {
    return NO_ERROR;
}

// ---------------------------------------------------------------------------------------------------------------------
//                                              BANKER'S FUNCTIONS
// ---------------------------------------------------------------------------------------------------------------------

static banker_customer *first;
static pthread_mutex_t *register_mutex = NULL;
static pthread_mutex_t *available_mutex = NULL;
// Do not access directly!
// TODO use mutexes when changing or reading _available!
int *_available = NULL;


/**
 * Cette fonction enregistre une chaîne de commande pour être exécutée
 * Elle retourne NULL si la chaîne de commande est déjà enregistrée ou
 * si une erreur se produit pendant l'exécution.
 * @param head la tête de la chaîne de commande
 * @return le pointeur vers le compte client retourné
 */
banker_customer *register_command(command_head *head) {
    return NULL;
}

/**
 * Cette fonction enlève un client de la liste
 * de client de l'algorithme du banquier.
 * Elle libère les ressources associées au client.
 * @param customer le client à enlever
 * @return un code d'erreur
 */
error_code unregister_command(banker_customer *customer) {
    return NO_ERROR;
}

/**
 * Exécute l'algo du banquier sur work et finish.
 *
 * @param work
 * @param finish
 * @return
 */
int bankers(int *work, int *finish) {
    return 0;
}

/**
 * Prépare l'algo. du banquier.
 *
 * Doit utiliser des mutex pour se synchroniser. Doit allour des structures en mémoire. Doit finalement faire "comme si"
 * la requête avait passé, pour défaire l'allocation au besoin...
 *
 * @param customer
 */
void call_bankers(banker_customer *customer) {
}

/**
 * Parcours la liste de clients en boucle. Lorsqu'on en trouve un ayant fait une requête, on l'évalue et recommence
 * l'exécution du client, au besoin
 *
 * @return
 */
void *banker_thread_run() {
    return NULL;
}

// ---------------------------------------------------------------------------------------------------------------------
// ---------------------------------------------------------------------------------------------------------------------
// ---------------------------------------------------------------------------------------------------------------------

/**
 * Cette fonction effectue une requête des ressources au banquier. Utilisez les mutex de la bonne façon, pour ne pas
 * avoir de busy waiting...
 *
 * @param customer le ticket client
 * @param cmd_depth la profondeur de la commande a exécuter
 * @return un code d'erreur
 */
error_code request_resource(banker_customer *customer, int cmd_depth) {
    return NO_ERROR;
}

/**
 * Utilisez cette fonction pour initialiser votre shell
 * Cette fonction est appelée uniquement au début de l'exécution
 * des tests (et de votre programme).
 */
error_code init_shell() {
    error_code err = NO_ERROR;
    return err;
}

/**
 * Utilisez cette fonction pour nettoyer les ressources de votre
 * shell. Cette fonction est appelée uniquement à la fin des tests
 * et de votre programme.
 */
void close_shell() {
}

/**
 * Utilisez cette fonction pour y placer la boucle d'exécution (REPL)
 * de votre shell. Vous devez aussi y créer le thread banquier
 */
void run_shell() {
    int ret;
    char *line, **tokens;
    struct cmdline cmd_ln;

    while (1) {
        line = readLine();
        if (line) {
            tokens = tokenize(line);
            cmd_ln = parse(tokens);
            if (!cmd_ln.commands) {
                /* means lack of memory */
                free(tokens);
                free(line);
                exit(1);
            } else {
                ret = execute(cmd_ln);

                free(cmd_ln.commands);
                free(tokens);
                free(line);

                if (ret < 0)
                    exit(1);
            }
        }
        line = NULL;
    }
}

/**
 * Vous ne devez pas modifier le main!
 * Il contient la structure que vous devez utiliser. Lors des tests,
 * le main sera complètement enlevé!
 */
int main(void) {
    if (HAS_NO_ERROR(init_shell())) {
        run_shell();
        close_shell();
    } else {
        printf("Error while executing the shell.");
    }
}

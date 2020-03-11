/**
 * Mathieu Perron 20076170
 * Amine   Sami   20086365
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
    BIDON, NONE, OR, AND, ALSO
} operator;

struct command_struct {
    char **call;
    int *ressources; 
    int call_size; // feels useless...
    int count;
    operator op;
    command *next;
};

/*
 * from new template
 */
struct command_chain_head_struct {
    int *max_resources;
    int max_resources_count;
    command *command;
    pthread_mutex_t *mutex;
    bool background;
};

/* Forward declaration */
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
    char **commands; // name of commands with special values
    int *command_caps; // available ressources for special commands
    unsigned int command_count; // number of commands with special values
    unsigned int ressources_count; // total number of ressources
    int file_system_cap;
    int network_cap;
    int system_cap;
    int any_cap;
    int no_banquers; 
} configuration;

/* this is a prototype residual pointer struct to free lines and tokens at exit */
typedef struct residual_ptrs_struct residual_ptrs;
struct residual_ptrs_struct {
    char *line;
    char **tokens;
    residual_ptrs *next;
};
    

int insert_char (char *, int, char, int);
char *readLine (void);
char **tokenize (const char *, char *);
error_code parse (char **, command_head *);
int execute_cmd (command);
int execute (command_head);
command *make_command_node (char **, operator, int, command *);
void free_command_list (command *);
void free_residual_ptr (residual_ptrs *);

const char* syntax_error_fmt = "bash: syntax error near unexpected token `%s'\n";

// -----------------------------------------------------------------------------
// -----------------------------------------------------------------------------
// -----------------------------------------------------------------------------

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


command *make_command_node (char **call, operator op, int count, command *next) {
    command *node = malloc(sizeof(command));
    if (!node) {
        fprintf(stderr, "command node could not be allocated\n");
        return NULL;
    }
    node->call = call;
    node->op = op;
    node->count = count;
    node->next = next;
    return node;
}

void free_command_list (command *cmd) {
    command *next;
    while (cmd) {
        next = cmd->next;
        /* no need to free call because no allocation was made */
        free(cmd->ressources);
        free(cmd);
        cmd = next;
    }
}

void free_residual_ptr (residual_ptrs *res) {
    residual_ptrs *next;
    while (res) {
        next = res->next;
        free(res->line);
        free(res->tokens);
        free(res);
        res = next;
    }
}

/**
 * readline allocate a new char array
 * @return a string
 * @exception out of memory
 * @author mathieu2em, aminesami
 */
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

/**
 * tokenize arguments
 * @param str : string to tokenize
 * @param delim : the delimiters of the tokens
 * @return string array
 * @exception out of memory
 * @author mathieu2em, aminesami
 */
char **tokenize(const char *str, char *delim) {
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

/**
 * Adds a node and initialize default values
 * @param
 * @return
 * @exception
 * @author mathieu2em, aminesami
 *
error_code init_next(command *parent) {
    command *kid = malloc(sizeof(command));
    if (!kid) {
        fprintf(stderr, "jai fourrer ta mere\n");
        return -1;
    }
    parent->next = kid;
    kid->op = NONE;
    kid->rnfn = '0';
    kid->n = 0;
    kid->next = NULL;
    return 0;
}

/**
 * creates the cmdline structure that containes every commands with their
 * types and other particularities
 * @param tokens an array of string
 * @return return the command_line structure containing commands
 * @exception
 * @author mathieu2em, aminesami
 */
error_code parse (char **tokens, command_head *cmd_head) {
    int i, j, k = 1;
    char *cp;
    operator op;
    int count;
    command *current, *cmd;
    bool rnfn;

    cmd_head->background = false;
    // TODO : check for child node allocation and free everything if fails
    /* now create the right structures for commands */
    for (i = j = 0; tokens[i]; i++) {
        op = BIDON;
        /* removes parenthesis if rn or fn */
        if (rnfn && (cp = strchr(tokens[i], ')'))) {
            *cp = '\0';
            rnfn = false;
        }
        if (tokens[i][0] == '&') {
            /* if the argument is a and get arguments before the &&
               and set type to AND */
            if (!tokens[i][1])
                op = ALSO;
            else if (tokens[i][1] == '&')
                op = AND;
            else {
                fprintf(stderr, syntax_error_fmt, tokens[i]);
                goto parse_error;
            }
        } else if (tokens[i][0] == '|') {
            if (tokens[i][1] == '|')
                op = OR;
            else {
                fprintf(stderr, syntax_error_fmt, tokens[i]);
                goto parse_error;
            }
        } else if ((i == 0 || !tokens[i-1]) &&
                   /* i think testing previous token nullity is now obsolete */
                   (tokens[i][0]=='r' || tokens[i][0]=='f')) {
            k = 1;
            while(tokens[i][k] && isdigit(tokens[i][k]))
                k++;
            if(isdigit(tokens[i][k-1]) && tokens[i][k] == '(') {
                rnfn = true;
                tokens[i][k] = '\0';
                count = (tokens[i][0] == 'r' ? 1 : -1) * atoi(tokens[i] + 1);
                
                if (!tokens[i][k+1])
                    j++;
                else
                    tokens[i] += k + 1;
                /* j = i;*/
            } else {
                fprintf(stderr, syntax_error_fmt, tokens[i]);
                goto parse_error;
            }
        } else if (op != ALSO && !tokens[i+1]) {
            op = NONE;
        }

        if (op != BIDON) {
            if (op != NONE)
                tokens[i] = NULL;
            cmd = make_command_node(tokens + j, op, count, NULL);
            if (!cmd)
                goto parse_error;
            j = i + 1;
            count = 1;
            if (!cmd_head->command)
                cmd_head->command = current = cmd;
            else
                current = current->next = cmd;
        }
    }

    return 0;

 parse_error:
    free_command_list(cmd_head->command);
    return -1;
}

/**
 * does the fork and execute the command
 * @param a single command structure
 * @return the resulting value returned by exec process
 * @exception
 * @author mathieu2em, aminesami
 */
int execute_cmd (command cmd) {
    int child_code = 0;
    pid_t pid;

    pid = fork();
    if (pid < 0) {
        fprintf(stderr, "could not fork process\n");
        return -1;
    } else if (pid == 0) {
        child_code = execvp(cmd.call[0], cmd.call);
        /* execvp only returns on error */
        fprintf(stderr, "bash: %s: command not found\n", cmd.call[0]);
    } else {
        waitpid(pid, &child_code, 0);
        child_code = WEXITSTATUS(child_code);
    }

    /* child_code should only be negative if fork or execvp failed */
    return child_code;
}

/*****************************************
 * WARNING execute DOES NOT WORK FOR NOW *
 *****************************************/
/**
 * execute & fork process
 * @param command_line the whole parsed command line structure
 * @return an integer representing the value of return code of exec fork
 * @exception
 * @author mathieu2em, aminesami
 */
int execute (command_head cmd_head) {
    int i, n, ret;
    pid_t pid;
    command *cmds = cmd_head.command;

    if (cmd_head.background) {
        pid = fork();
        if (pid < 0) {
            /* si le fork a fail */
            fprintf(stderr, "could not fork process\n");
            return -1;
        } else if (pid != 0) {
            return 0;
        }
    }

    for (i = 0; cmds[i].call; i++) {
    /*
        if (cmds[i].rnfn == 'r' || cmds[i].rnfn == 'f') {
            for (n = 0; n < cmds[i].n; n++) {
                ret = (cmds[i].rnfn == 'f') ? 0 : execute_cmd(cmds[i]);
                /* ret is only negative inside child_process which failed 
                if (ret < 0)
                    return -1;
            }
        } else {
            ret = execute_cmd(cmds[i]);
            if (ret < 0) {
                /* here if error in execvp or fork */
                /* exit after, we are inside child process or fork failed 
                return -1;
            }
        }*/

        if (ret == 0) { /* here if success */
            /* OR should eval until one success */
            if (cmds[i].op == OR) {
                /* skip until && */
                while (cmds[i].call && cmds[i].op != AND)
                    i++;
                if (!cmds[i].call)
                    break;
            }
        } else { /* here if failure */
            /* AND should eval until one failure */
            if (cmds[i].op == AND) {
                /* skip until || */
                while (cmds[i].call && cmds[i].op != OR)
                    i++;
                if (!cmds[i].call)
                    break;
            }
        }
    }

    return 0;
}

/**
 * Cette fonction analyse la première ligne et remplie la configuration.
 *
 * si une commande faisant partie des commandes de base est détectée dans
 * les commandes speciales, elle est ajoutee aux commandes speciales
 * mais n'est pas enlevée des commandes de base. le fonctionnement des
 * fonction afférentes s'assurera de choisir son bon numero d'identification
 * en priorisant la recherche parmis les commandes speciales
 *
 * @param line la première ligne du shell
 * @param conf pointeur vers le pointeur de la configuration
 * @return un code d'erreur (ou rien si correct)
 */
error_code parse_first_line(char *line) {
    char **parsed_and; /* to free */
    char **special_funcs_caps;
    int n = 0;
    int i = 0;

    conf = malloc(sizeof(configuration));
    parsed_and = tokenize(line, "&");
    /* verify if there is special commands capacities
     * so we need to count how many elements are in
     * parsed_and ; i.e. if > 4 yes else no
     */
    while(parsed_and[n++])
        ;
    /* nbr of commands , do not erase */
    if(n>4) {
        /* set configuration commands */
        conf->commands = tokenize(parsed_and[0], " ,");
        /*
         * set configuration commands capacities
         * tokenize give a string array so we will have to
         * convert it to an int array
         */
        n=0;
        /* gets capacities */
        special_funcs_caps = tokenize(parsed_and[1], " ,");
        /* count commands */
        while (special_funcs_caps[n++])
            ;
        /* set it in conf */
        conf->command_count = n-1;
        /* show values */
        printf("command cound = %d\n", n-1);
        /* init conf command caps */
        conf->command_caps = malloc(sizeof(int)*(n));
        /* use atoi to convert string to int */
        for(i=0; i<n-1; i++) {
            conf->command_caps[i] = atoi(special_funcs_caps[i]);
        }
        /* the last segment is for null */
        conf->command_caps[n-1] = '\0';
        /* unused after that */
        free(special_funcs_caps);
        special_funcs_caps = NULL; // TODO not sure if necessary
    }
    /* testing */
    i=0;
    while(conf->command_caps[i]) printf("%d\n", conf->command_caps[i++]);

    /* setting config values for different elems */
    /* file_system capacity */
    conf->file_system_cap = atoi(parsed_and[i++]);
    /* network capacity */
    conf->network_cap = atoi(parsed_and[i++]);
    /* system capacity */
    conf->system_cap = atoi(parsed_and[i++]);
    /* any capacity */
    conf->any_cap = atoi(parsed_and[i]);

    /* count ressources for ressources_count value of config */
    i=0;n=0;
    while(conf->command_caps[i]) n+=conf->command_caps[i++];
    n += (conf->file_system_cap + conf->network_cap + conf->system_cap + conf->any_cap);
    conf->ressources_count = n;

    /* testing */
    printf("%d,%d,%d,%d\n", conf->file_system_cap, conf->network_cap, conf->system_cap, conf->any_cap);
    printf("ressources count : %d\n", conf->ressources_count);

    return NO_ERROR;
}

#define FS_CMDS_COUNT 10
#define FS_CMD_TYPE 0
#define NETWORK_CMDS_COUNT 6
#define NET_CMD_TYPE 1
#define SYS_CMD_COUNTS 3
#define SYS_CMD_TYPE 2
#define MISC_CMD_TYPE 3 // for commands not in special commands or in basic commands

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
 * le numéro de la catégorie de ressource les ressources spéciales
 * sont numérotées a partir de 4
 * @param res_name le nom
 * @param config la configuration du shell
 * @return le numéro de la catégorie (ou une erreur)
 */
error_code resource_no(char *res_name) {
    int i;
    /* verify if in special commands */
    for(i=0; i<((int)(conf->command_count)); i++)
        if(!strcmp(res_name,conf->commands[i])) return i+4;
    /* verify if in system, reseau or filesystem */
    /* system */
    for(i=0; i<SYS_CMD_COUNTS; i++)
        if(!strcmp(res_name,SYSTEM_CMDS[i])) return SYS_CMD_TYPE;
    /* reseau */
    for(i=0; i<NETWORK_CMDS_COUNT; i++)
        if(!strcmp(res_name,NETWORK_CMDS[i])) return NET_CMD_TYPE;
    /* filesystem */
    for(i=0; i<FS_CMDS_COUNT; i++)
        if(!strcmp(res_name,FILE_SYSTEM_CMDS[i])) return FS_CMD_TYPE;
    /* misc */
    return MISC_CMD_TYPE;
}

/**
 * Cette fonction prend en paramètre un numéro de ressource et retourne
 * la quantitée disponible de cette ressource
 * file_sys=0, network=1, sys=2, misc=3, else.
 * if do not exist then -1
 * @param resource_no le numéro de ressource
 * @param conf la configuration du shell
 * @return la quantité de la ressource disponible
 */
int resource_count(int resource_no) {
    if(resource_no>3) return conf->command_caps[resource_no-3];
    else if(resource_no==0) return conf->file_system_cap;
    else if(resource_no==1) return conf->network_cap;
    else if(resource_no==2) return conf->system_cap;
    else if(resource_no==3) return conf->any_cap;
    else return -1;
}

// Forward declaration
error_code evaluate_whole_chain(command_head *head);

/**
 * Créer une chaîne de commande qui correspond à une ligne de commandes
 * @param line la ligne de texte à parser
 * @param result le résultat de la chaîne de commande
 * @return un code d'erreur
 */
error_code create_command_chain(const char *line, command_head **result) {
    char **tokens;
    command_head *cmd_head;
    tokens = tokenize(line, " \t");
    /* initialize command chain head */
    cmd_head = malloc(sizeof(command_head));
    if (!cmd_head) {
        fprintf(stderr, "could not allocate command chain header\n");
        return -1;
    }
    if (HAS_ERROR(parse(tokens, cmd_head))) {
        free(cmd_head);
        return -1;
    }

    *result = cmd_head; /* insert command chain at pointed location */
    return NO_ERROR;
}

/**
 * Cette fonction compte les ressources utilisées par un block
 * La valeur interne du block est mise à jour
 * @param command_block le block de commande
 * @return un code d'erreur
 */
error_code count_ressources(command_head *head, command *command_block) {
    command *current = command_block;

    while (current) {
        /* using part 1 functions we can count ressources */
        current = current->next;
    }
    
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
    char *line;

    /* extract first line */
    line = readLine();

    /* send it to first line analyser */
    err = parse_first_line(line);

    return err;
}

/**
 * Utilisez cette fonction pour nettoyer les ressources de votre
 * shell. Cette fonction est appelée uniquement à la fin des tests
 * et de votre programme.
 */
void close_shell() {
    // WE SHOULD ONLY FREE ALLOCATED LINES & TOKENS ARRAY HERE
}


/*****************************************
 * WARNING run_shell DOES NOT WORK FOR NOW *
 *****************************************/
/**
 * Utilisez cette fonction pour y placer la boucle d'exécution (REPL)
 * de votre shell. Vous devez aussi y créer le thread banquier
 */
void run_shell() {
    int ret;
    char *line, **tokens;
    command_head cmd_ln;

    while (1) {
        line = readLine();
        if (line) {
            //tokens = tokenize(line, " \t");
            //cmd_ln = parse(tokens);
            if (!cmd_ln.command) {
                /* means lack of memory */
                free(tokens);
                free(line);
                exit(1);
            } else {
                ret = execute(cmd_ln);

                free(cmd_ln.command);
                free(tokens);
                free(line);

                if (ret < 0)
                    exit(1);
            }
        }
        line = NULL;
    }
}


/****************************************************
 * WARNING THIS VERSION OF main IS FOR TESTING ONLY *
 ****************************************************/
int main (void)
{
    char *line, **tokens, **t;
    command *c;
    command_head *cmd_head;
    residual_ptrs *residuals;

    residuals = malloc(sizeof(residual_ptrs));
    if (!residuals)
        return 1;
    
    line = readLine();
    tokens = tokenize(line, " ");
    residuals->line = line;
    cmd_head = malloc(sizeof(command_head));
    
    if (!cmd_head) {
        free(residuals);
        free(line);
        free(tokens);
        return 1;
    }
    
    if(HAS_ERROR(parse(tokens, cmd_head))) {
        free(residuals);
        free(line);
        free(tokens);
        free(cmd_head);
        return 1;
    }

    residuals->tokens = tokens;
    puts("op values -- BIDON: 0, NONE: 1, OR: 2, AND: 3, ALSO: 4");
    c = cmd_head->command;
    while (c) {
        t = c->call;
        printf("op=%d, count=%d: ", c->op, c->count);
        while (*t) {
            printf("%s ", *t);
            t++;
        }
        puts("");
        c = c->next;
    }

    free_command_list(cmd_head->command);
    free(cmd_head);
    free_residual_ptr(residuals);
    return 0;
}

/**
 * Vous ne devez pas modifier le main!
 * Il contient la structure que vous devez utiliser. Lors des tests,
 * le main sera complètement enlevé!
 *
int main(void) {
    if (HAS_NO_ERROR(init_shell())) {
        run_shell();
        close_shell();
    } else {
        printf("Error while executing the shell.");
    }
}
*/

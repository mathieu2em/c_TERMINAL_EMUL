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
    int *ressources; // contains quantity of used ressources by the command
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
    command_head *head;     // tête de la commande
    banker_customer *next;  // client suivant
    banker_customer *prev;  // client précédent
    int *current_resources; // ressources utilisées en ce moment
    int depth;              // profondeur de commande courante du client
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
    int no_banquers;// unused
} configuration;

// listes chainees de threads
typedef struct pthreads_list tlist;
struct pthreads_list {
    pthread_t t;
    tlist *next;
};

int insert_char (char *, int, char, int);
char *readLine (void);
char **tokenize (char *, char *);
error_code parse (char **, command_head *);
int execute_cmd (command*);
error_code execute (command *);
command *make_command_node (char **, operator, int, command *);
void free_command_list (command *);

tlist *make_tlist_node (tlist *);
void free_tlist (tlist *);

void close_shell (void);
void run_shell (void);

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
    // char **c = NULL, **saved;
    int i;
    command *node = malloc(sizeof(command));
    if (!node) {
        fprintf(stderr, "command node could not be allocated\n");
        return NULL;
    }

    /* compute call_size and allocate according array in node */
    for (i = 0; call[i]; i++)
        ;
    node->call = malloc(sizeof(char*) * (i+1));
    if (!node->call) {
        fprintf(stderr, "call array could not be allocated\n");
        free(node);
        return NULL;
    }
    node->call_size = i;

    /* copy call into node */
    for (i = 0; call[i]; i++) {
        node->call[i] = malloc(sizeof(char) * (strlen(call[i]) + 1));
        if (!node->call[i]) {
            for (; i >= 0; i--)
                free(node->call[i]);
            free(node->call);
            free(node);
            return NULL;
        }
        strcpy(node->call[i], call[i]);
    }
    node->call[i] = NULL;

    node->ressources = NULL;
    node->op = op;
    node->count = count;
    node->next = next;
    return node;
}

void free_command_list (command *cmd) {
    int i;
    command *next;
    while (cmd) {
        next = cmd->next;
        for (i = 0; i < cmd->call_size; i++)
            free(cmd->call[i]);
        free(cmd->call);
        free(cmd->ressources);
        free(cmd);
        cmd = next;
    }
}

tlist *make_tlist_node (tlist *next) {
    tlist *ls = malloc(sizeof(tlist));
    ls->next = next;
    return ls;
}

// TODO : ask charlie about this
void free_tlist (tlist *ls) {
    tlist *next;
    while (ls) {
        next = ls->next;
        // maybe should be pthread_cancel
        if (pthread_join(ls->t, NULL)) {
            fprintf(stderr, "could not join thread\n");
        }
        free(ls);
        ls = next;
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
        close_shell();
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
char **tokenize(char *str, char *delim) {
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
 * creates the cmdline structure that containes every commands with their
 * types and other particularities
 * @param tokens an array of string
 * @return return the command_line structure containing commands
 * @exception
 * @author mathieu2em, aminesami
 */
error_code parse (char **tokens, command_head *cmd_head) {
    int i, j, k = 1;
    char *cp = NULL;
    operator op = BIDON;
    int count = 1;
    command *current = NULL, *cmd = NULL;
    bool rnfn = false;

    cmd_head->command = NULL;
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
            if (!tokens[i][1]) {
                op = ALSO;
            } else if (tokens[i][1] == '&') {
                op = AND;
                cmd_head->background = true;
            } else {
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
                tokens[i] = NULL; // TODO dafuck is happening here
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
 * @param a ptr to a command block
 * @return 1 on success, 0 on exec failure, negative on fork failure
 * @exception
 * @author mathieu2em, aminesami
 */
int execute_cmd (command *cmd) {
    int i, exit_code = -1;
    pid_t pid = 0;

    if (cmd->count <= 0)
        return 1;

    for (i = 0; i < cmd->count; i++) {
        pid = fork();
        if (pid < 0) {
            fprintf(stderr, "could not fork process\n");
            return pid;
        } else if (pid == 0) {
            execvp(cmd->call[0], cmd->call);
            /* execvp only returns on error */
            fprintf(stderr, "bash: %s: command not found\n", cmd->call[0]);
            return -1;
        }
    }

    waitpid(pid, &exit_code, 0);
    i = WIFEXITED(exit_code);
    if (!i)
        return 0;
    i = WEXITSTATUS(exit_code);
    if (i)
        return 0;
    return 1;
}

/**
 * executes a command block
 * @param a ptr to a command block
 * @return 0 on succes or -1 on error
 * @exception
 * @author mathieu2em, aminesami
 */
error_code execute (command *cmd) {
    int ret;
    operator op;
    command *next;

    if (!cmd || !cmd->call)
        return 0;

    ret = execute_cmd(cmd);
    if (ret < 0)
        return -1;

    op = cmd->op;
    next = cmd->next;

    switch (op) {
        case BIDON: case NONE:
            return 0;
        case AND:
            if (ret)
                return execute(next);
            else
                return 0;
        case OR:
            if (ret) {
                next = next->next;
                while (next && (next->op == OR || next->op == NONE))
                    next = next->next;
                if (next && next->op == AND)
                    next = next->next;
            }
            return execute(next);
        default:
            return 0;
    }
}

void *execute_background (void *arg) {
    command_head *head = (command_head *)arg;
    long ret = execute(head->command);

    // free used memory
    free_command_list(head->command);
    free(head);

    if (ret < 0)
        exit(1);
    return (void *)ret; // TODO not sure
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
error_code parse_first_line (char *line) {
    char *fields[6];
    char *c, **saved;
    int i;
    int n = 0;//count number of fields

    conf = malloc(sizeof(configuration));

    //cas possible d'aucuns elements dans les premiers champs
    for (i = 0; i < 6; i++) {
        fields[i] = strtok(i == 0 ? line : NULL, "&");
        if (fields[i]&&strcmp(fields[i]," ")!=0) n++; // count number of fields
        if (!fields[i]&&(i<4||i>5)) {
            fprintf(stderr, "invalid first line syntax: "
                            "not enough fields were given\n");
            free(conf);
            return -1;
        }
    }

    if (n<4) {
        fprintf(stderr, "invalid first line syntax: "
                        "not enough fields were given\n");
        free(conf);
        return -1;
    }

    // allocate special commands array
    conf->commands = malloc(sizeof(char*));
    if (!conf->commands) {
        free(conf);
        return -1;
    }

    if(n>4){
        // test
        printf("n>4\n");

        // duplicate special commands substring
        c = strdup(fields[0]);
        if (!c) {
            free(conf->commands);
            free(conf);
            return -1;
        }

        // this is the only string that needs freeing
        conf->commands[0] = c;

        for (i = 1; (c = strchr(c, ',')); i++) {
            *c = '\0'; // separate command strings
            conf->commands = realloc(saved = conf->commands, sizeof(char*) * (i + 1));
            if (!conf->commands) {
                free(saved[0]);
                free(saved);
                free(conf);
                return -1;
            }
            conf->commands[i] = ++c; // register next command
            conf->command_count = i+1; // register array size
            conf->ressources_count = (conf->command_count) + 4;

            conf->command_caps = malloc(sizeof(int) * i);
            if (!conf->command_caps) {
                free(conf->commands[0]);
                free(conf->commands);
                free(conf);
                return -1;
            }
        }

        for (i = 0; i < conf->command_count; i++) {
            c = strtok(i == 0 ? fields[1] : NULL, ",");
            if (!c) {
                fprintf(stderr, "invalid first line syntax: "
                                "no matching amount to command '%s'\n", conf->commands[i]);
                free(conf->command_caps);
                free(conf->commands[0]);
                free(conf->commands);
                free(conf);
                return -1;
            }
            conf->command_caps[i] = atoi(c); // XXX : no error check but whatever
        }
    } else {
        conf->commands[0] = "";
        conf->command_count = 0;
    }

    if(n>4){
        conf->file_system_cap = atoi(fields[2]);
        conf->network_cap = atoi(fields[3]);
        conf->system_cap = atoi(fields[4]);
        conf->any_cap = atoi(fields[5]);
    } else {
        conf->file_system_cap = atoi(fields[1]);
        conf->network_cap = atoi(fields[2]);
        conf->system_cap = atoi(fields[3]);
        conf->any_cap = atoi(fields[4]);
    }

    printf("%d,%d,%d,%d\n", conf->file_system_cap,conf->network_cap, conf->system_cap, conf->any_cap);

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
error_code resource_no (char *res_name) {
    int i;

    /* dynamic category */
    for(i = 0; i < conf->command_count; i++)
        if(strcmp(res_name,conf->commands[i]) == 0)
            return i+4;

    /* system */
    for(i = 0; i < SYS_CMD_COUNTS; i++)
        if(strcmp(res_name,SYSTEM_CMDS[i]) == 0)
            return SYS_CMD_TYPE;

    /* network */
    for(i = 0; i < NETWORK_CMDS_COUNT; i++)
        if(strcmp(res_name,NETWORK_CMDS[i]) == 0)
            return NET_CMD_TYPE;

    /* filesystem */
    for(i = 0; i < FS_CMDS_COUNT; i++)
        if(strcmp(res_name,FILE_SYSTEM_CMDS[i]) == 0)
            return FS_CMD_TYPE;

    /* misc */
    return MISC_CMD_TYPE;
}

/**
 * Cette fonction prend en paramètre un numéro de ressource et retourne
 * la quantitée disponible de cette ressource
 * file_sys=0, network=1, sys=2, misc=3, else.
 * if do not exist then -1
 * @param resource_no le numéro de ressource
 * @return la quantité de la ressource disponible
 */
int resource_count (int resource_no) {
    switch (resource_no) {
        case FS_CMD_TYPE:
            return conf->file_system_cap;
        case NET_CMD_TYPE:
            return conf->network_cap;
        case SYS_CMD_TYPE:
            return conf->system_cap;
        case MISC_CMD_TYPE:
            return conf->any_cap;
        default:
            if (resource_no - 4 < conf->command_count)
                return conf->command_caps[resource_no - 4];
            else
                return -1;
    }
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
    char *l;
    char **tokens;
    command_head *cmd_head;

    // duplicate line to save const qualifier
    l = strdup(line);
    if (!l) {
        fprintf(stderr, "could not duplicate line\n");
        return -1;
    }

    tokens = tokenize(l, " \t");
    // initialize command chain head
    cmd_head = malloc(sizeof(command_head));
    // store max_resources_count future size (will be added in evaluate_whole_chain)
    cmd_head->max_resources_count=conf->ressources_count;

    if (!cmd_head) {
        fprintf(stderr, "could not allocate command chain header\n");
        free(tokens);
        free(l);
        return -1;
    }

    if (HAS_ERROR(parse(tokens, cmd_head))) {
        free(cmd_head);
        free(tokens);
        free(l);
        return -1;
    }

    // at this point the duplicated string l is no longer needed
    free(tokens);
    free(l);

    // maybe merge assignment with return statement
    // but for now keep seperate for clarity
    *result = cmd_head;
    return evaluate_whole_chain(cmd_head);
}

/**
 * Cette fonction compte les ressources utilisées par un block
 * La valeur interne du block est mise à jour
 * @param command_block le block de commande
 * @return un code d'erreur
 */
error_code count_ressources(command_head *head, command *command_block) {
    // allocated according to number of ressources configured in first line
    command_block->ressources = malloc(sizeof(int)*conf->command_count+4);
    command_block->ressources[resource_no(*command_block->call)]=command_block->count;
    return NO_ERROR;
}

/**
 * Cette fonction parcours la chaîne et met à jour la liste des commandes
 * @param head la tête de la chaîne
 * @return un code d'erreur
 */
error_code evaluate_whole_chain(command_head *head) {
    command *current = head->command;
    // allocate max_resources
    head->max_resources = malloc(sizeof(int)*head->max_resources_count);

    while (current) {
        if (HAS_ERROR(count_ressources(head, current))) {
            fprintf(stderr, "error occured while counting resources\n");
            free_command_list(head->command);
            return -1;
        }

        // here we must compute maximum concurent resources before iterating
        head->max_resources[resource_no(*current->call)] += current->count;

        current = current->next;
    }

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
 * si une erreur se produit pendant l'exécution
 * @param head la tête de la chaîne de commande
 * @return le pointeur vers le compte client retourné
 */
banker_customer *register_command(command_head *head) {

    banker_customer *current;
    int i;

    // if there is still no banker create it
    if(!first){
        first = malloc(sizeof(banker_customer));
        current = first;
    }
    // else go to last existing node and add a new one
    else {
        current = first;
        // get last elem
        while(current->next) current=current->next;
        // add the new one
        current->next = malloc(sizeof(banker_customer));
        if(!current->next){
            fprintf(stderr, "command node could not be allocated\n");
            return NULL;
        }

        current->next->prev = current;
        current = current->next;
    }

    current->head = head;

    //create current_resources table and fill it with 0s
    current->current_resources = malloc(sizeof(int)*conf->ressources_count);
    for(i=0;i<(int)conf->ressources_count;i++) current->current_resources[i] = 0;

    current->depth=-1;

    // TODO call request_ressource ?
    return current;
}

/**
 * Cette fonction enlève un client de la liste
 * de client de l'algorithme du banquier.
 * Elle libère les ressources associées au client.
 * @param customer le client à enlever
 * @return un code d'erreur
 */
error_code unregister_command(banker_customer *customer) {
    int i;
    int n = conf->ressources_count;
    // on verifie si cest first
    if(customer == first){
        // si c'est le cas on assigne first au suivant
        first = customer->next;
        // on assigne le precedent de first a NULL
        first->prev = NULL;
        // on assigne le precedent de celui qui suit first a first
        first->next->prev = first;
    }
    // sinon on ajuste normalement
    else {
        customer->prev->next = customer->next;
        customer->next->prev = customer->prev;
    }
    if(pthread_mutex_lock(available_mutex)) return -1;
    // liberer les ressources
    for(i = 0; i < n; i++){
        _available[i] += customer->current_resources[i];
    }
    if(pthread_mutex_unlock(available_mutex)) return -1;
    // liberation de la memoire
    free(customer->current_resources);
    free_command_list(customer->head->command);
    free(customer->head);

    return NO_ERROR;
}

bool all_good(int *);
/**
 * Exécute l'algo du banquier sur work et finish.
 * TODO s'assurer que finish et work sont bien alloués
 *
 * @param work
 * @param finish
 * @return
 */
int bankers(int *work, int *finish) {
    int *max = first->head->max_resources; // known in command head
    int *allocated = first->current_resources; // known in banquer customer
    int i,j;
    bool is_good = false;
    int n = conf->ressources_count; // number of ressources
    banker_customer *current = first;

    j=0;
    while(!all_good(finish)){
        if(!finish[j]){
            max = current->head->max_resources; // known in command head
            allocated = current->current_resources; // known in banquer customer
            // max - allocation = needed
            is_good=true;
            for(i = 0; i < n; i++) {
                if(max[i]-allocated[i] > work[i]){
                    is_good = false;
                    break;
                }
            }

            if(is_good){
                // is good so : work = work + alloc and finish[j] = true
                for(i=0; i<n; i++){
                    work[i] += allocated[i];
                }
                // this element is finished
                finish[j] = true;
                // we go back to start
                current = first;
                j = 0;
            }
            // we passed all the elements without being able to finish one so game over
            else if(!current->next){
                return false;
            } else {
                current = current->next;
                j++;
            }
        } else {
            // go to next element
            if(current->next){
                current = current->next;
                j++;
            }
        }
    }
    // we got out of the while loop and everything finished rightly
    return true;
}

// Verifie si on est a gagne dans le banquier
bool all_good(int *finish){
    int i = 0;
    while(finish[i]) if(!finish[i++]) return false;
    return true;
}

/**
 * Prépare l'algo. du banquier.
 *
 * Doit utiliser des mutex pour se synchroniser. Doit allour des structures en mémoire. Doit finalement faire "comme si"
 * la requête avait passé, pour défaire l'allocation au besoin...
 * TODO : tester conditions de deadlock évidentes
 *
 * @param customer
 */
void call_bankers(banker_customer *customer) {

    // 1. wait for mutex
    if(pthread_mutex_lock(available_mutex)) exit(1);

    int *work;
    int *finish;
    int i = 1;
    int j;
    int n = (int)(conf->ressources_count);
    banker_customer *current = first;
    command *c = customer->head->command;

    // ---allocation of finish table---
    // 1. count number of banker elements in chained list
    while(current->next){
        i++;
        current = current->next;
    }
    // 2. malloc memory for table with one more field for end flag
    finish = malloc(sizeof(int)*(i+1));
    if(!finish) exit(1);

    // 3. fill with falses
    for(j = 0; j < i; j++){
        finish[j]=false;
    }
    finish[i] = -1;// end flag

    // modify available
    // 1. get command at depth
    for(i = 0; i < customer->depth; i++){
        if(c->next) c = c->next;
        else fprintf(stderr, "WHAT THE FUCK BRUH\n");
    }
    // 2. modify available
    for(i = 0; i < n; i++){
        _available[i] -= c->ressources[i];
    }
    // copy available to work
    work = malloc(sizeof(int) * n);
    if(!work){
        exit(1);
    }
    for(i = 0; i < n; i++)
        work[i] = _available[i];

    // si banker chi on restore available.
    if(!bankers(work, finish)){
        for(i = 0; i < n; i++){
            _available[i] += c->ressources[i];
        }
    } else {
        // si banker chi pas on actualise les ressources du customer
        for(i = 0; i < n; i++){
            customer->current_resources[i] += c->ressources[i];
        }
    }

    // liberate the mutex
    if(pthread_mutex_unlock(available_mutex)) exit(1);
}

/**
 * Parcours la liste de clients en boucle. Lorsqu'on en trouve un ayant fait une requête, on l'évalue et recommence
 * l'exécution du client, au besoin
 *
 * @return
 */
void *banker_thread_run() {
    banker_customer *current;
    // execute sans fin
    while(true){
        // 1. Acquerir le mutex d'enregistrement
        pthread_mutex_lock(register_mutex);
        // 2. Parcourir tous les clients enregistres
        current = first;
        while(current){
            // 3. En trouver un dont le depth n'est pas -1
            // ( si on n'en trouve pas, mutex unlock et passe a prochain tour de boucle)
            if(current->depth >= 0){
                // 4. Appelle call_bankers sur ce client
                call_bankers(current);
                break;
            }
            current=current->next;
        }
        // 5. deverouiller le mutex d'enregistrement
        pthread_mutex_unlock(register_mutex);
    }
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
    pthread_mutex_t *mut = customer->head->mutex;
    // lock le mutex de head pour faire ses shit yo
    if(pthread_mutex_lock(mut)) return -1;
    // attend que le banquier finisse ses shit
    if(pthread_mutex_lock(register_mutex)) return -1;
    // changer le depth a la position d'execution de la ligne ou on est rendu
    customer->depth = cmd_depth;
    // on peut debarrer
    if(pthread_mutex_unlock(register_mutex)) return -1;
    if(pthread_mutex_unlock(mut)) return -1;
    return NO_ERROR;
}

/**
 * Utilisez cette fonction pour initialiser votre shell
 * Cette fonction est appelée uniquement au début de l'exécution
 * des tests (et de votre programme).
 */
error_code init_shell() {
    char *line;
    int i;

    // extract first line
    line = readLine();

    // send it to first line analyser
    if(HAS_ERROR(parse_first_line(line))){
        fprintf(stderr, " AAAAAAAH\n");
        return -1;
    }
    if(pthread_mutex_lock(available_mutex)) return -1;

    // set available ressources to the right thing yeah
    _available = malloc(sizeof(int)*conf->ressources_count);
    if(!_available) return -1;

    _available[FS_CMD_TYPE] = conf->file_system_cap;
    _available[NET_CMD_TYPE] = conf->network_cap;
    _available[SYS_CMD_TYPE] = conf->system_cap;
    _available[MISC_CMD_TYPE] = conf->any_cap;
    for(i = 0; i < (int)(conf->command_count); i++){
        _available[i + 4] = conf->command_caps[i];
    }

    // test
    if(conf->command_count){
        for(int i = 0; i < (int)(conf->command_count); i++){
            printf("%s, %d\n",conf->commands[i],conf->command_caps[i]);
        }
    }

    free(line);

    return NO_ERROR;
}

/**
 * Utilisez cette fonction pour nettoyer les ressources de votre
 * shell. Cette fonction est appelée uniquement à la fin des tests
 * et de votre programme.
 */
void close_shell() {
    if(conf->command_count){
        free(conf->command_caps);
        free(conf->commands[0]);
        free(conf->commands);
    }
    free(conf);
    exit(0);
}


/**
 * Utilisez cette fonction pour y placer la boucle d'exécution (REPL)
 * de votre shell. Vous devez aussi y créer le thread banquier
 */
void run_shell() {
    char *line, **tokens;
    command *c;
    command_head *cmd_head;
    int ret = 0;
    // pid_t pid;
    tlist *thread_list = NULL;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    while (1) {
        line = readLine();
        if (!line)
            exit(1);

        tokens = tokenize(line, " \t");
        if (!tokens) {
            free(line);
            exit(1);
        }

        cmd_head = malloc(sizeof(command_head));

        if (!cmd_head) {
            free(line);
            free(tokens);
            exit(1);
        }

        if(HAS_ERROR(parse(tokens, cmd_head))) {
            free(line);
            free(tokens);
            free(cmd_head);
            exit(1);
        }

        // free line and tokens
        free(tokens);
        free(line);

        if (cmd_head->background) {
            thread_list = make_tlist_node(thread_list);
            pthread_create(&(thread_list->t), &attr, execute_background, cmd_head);
        } else {
            execute(cmd_head->command);

            // free used memory TODO modify
            free_command_list(cmd_head->command);
            free(cmd_head);
        }
    }
    pthread_attr_destroy(&attr);
    free_tlist(thread_list);
}


/****************************************************
 * WARNING THIS VERSION OF main IS FOR TESTING ONLY *
 ****************************************************
int main (void)
{
    run_shell();
    close_shell();
    return 0;
}
*/

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

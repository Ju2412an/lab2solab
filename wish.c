#define _GNU_SOURCE      // Necesario para exponer strsep() y getline() en glibc.

#include <stdio.h>      // Para printf, fopen, fclose, fflush, getline, FILE.
#include <stdlib.h>     // Para malloc, free, exit.
#include <string.h>     // Para strsep, strcmp, strlen, strdup.
#include <unistd.h>     // Para fork, execv, access, chdir, dup2, close, write, STDERR_FILENO, STDOUT_FILENO.
#include <sys/wait.h>   // Para waitpid.
#include <fcntl.h>      // Para open, O_WRONLY, O_CREAT, O_TRUNC.

/*
    wish — Wisconsin Shell
    Intérprete de comandos simple para la práctica No. 2 del curso de
    Sistemas Operativos. Soporta modo interactivo y modo batch, los
    comandos integrados exit/cd/path, redirección de salida y error
    estándar, y ejecución de comandos en paralelo con el operador &.
*/

#define MAX_TOKENS 1024  // Cantidad máxima de tokens por línea.
#define MAX_CMDS   256   // Cantidad máxima de comandos paralelos por línea.
#define MAX_ARGS   256   // Cantidad máxima de argumentos por comando.

// Mensaje de error único exigido por el enunciado.
char error_message[30] = "An error has occurred\n";

// Prototipos de funciones.
void print_error(void);
void inicializar_path(void);
void liberar_path(void);
void establecer_path(char **dirs, int n);
char *buscar_ejecutable(const char *comando);
char *normalizar_operadores(const char *linea);
void procesar_linea(char *linea);
int  es_builtin(const char *comando);
void ejecutar_builtin(char **args, int nargs);
pid_t ejecutar_externo(char **args, const char *redir);
int  wish(int argc, char *argv[]);

/*
    Variables globales que mantienen el search path del shell.
    g_path es un arreglo dinámico de cadenas (char *) que contiene
    los directorios donde se buscarán los ejecutables. g_path_len
    indica cuántos directorios válidos hay actualmente.
*/
char **g_path = NULL;
int    g_path_len = 0;

/*
    main — Punto de entrada. Delega todo el trabajo a la función wish,
    siguiendo la convención de los programas del laboratorio anterior.
*/
int main(int argc, char *argv[]) {
    return wish(argc, argv);
}

/*
    print_error — Imprime el único mensaje de error permitido por el
    enunciado a la salida de error estándar (stderr) con la llamada
    write(). Se usa cada vez que ocurre cualquier tipo de error.
*/
void print_error(void) {
    write(STDERR_FILENO, error_message, strlen(error_message));
}

/*
    liberar_path — Libera la memoria asociada al search path actual,
    siguiendo el patrón reservar → verificar → usar → liberar.
    Después de liberar cada cadena y el arreglo, se asigna NULL para
    evitar punteros colgantes.
*/
void liberar_path(void) {
    if (g_path != NULL) {
        for (int i = 0; i < g_path_len; i++) {
            free(g_path[i]);
            g_path[i] = NULL;
        }
        free(g_path);
        g_path = NULL;
    }
    g_path_len = 0;
}

/*
    establecer_path — Sobrescribe el search path del shell con los
    directorios indicados. Primero libera el path anterior y luego
    reserva memoria nueva para n punteros y copia cada cadena con
    strdup(). Si n == 0 el path queda vacío (no se puede ejecutar
    ningún programa externo, como indica el enunciado).
    1. dirs: arreglo de cadenas con los directorios nuevos.
    2. n:    cantidad de directorios.
*/
void establecer_path(char **dirs, int n) {
    liberar_path();
    if (n <= 0) {
        return; // Path vacío: solo built-ins podrán ejecutarse.
    }

    g_path = malloc(n * sizeof(char *));
    if (g_path == NULL) {
        print_error();
        return;
    }

    for (int i = 0; i < n; i++) {
        g_path[i] = strdup(dirs[i]);
        if (g_path[i] == NULL) {
            // Si strdup falla, se libera lo reservado hasta el momento.
            for (int j = 0; j < i; j++) {
                free(g_path[j]);
                g_path[j] = NULL;
            }
            free(g_path);
            g_path = NULL;
            g_path_len = 0;
            print_error();
            return;
        }
    }
    g_path_len = n;
}

/*
    inicializar_path — Establece el search path inicial del shell.
    El enunciado exige que el path arranque conteniendo únicamente
    el directorio /bin.
*/
void inicializar_path(void) {
    char *inicial[] = { "/bin" };
    establecer_path(inicial, 1);
}

/*
    buscar_ejecutable — Recorre el search path actual y verifica con
    la llamada access(..., X_OK) si el comando existe como archivo
    ejecutable en alguno de esos directorios. Si lo encuentra, devuelve
    una cadena recién reservada con la ruta completa (el llamador debe
    liberarla con free). Si no lo encuentra, devuelve NULL.
    1. comando: nombre del ejecutable a localizar.
*/
char *buscar_ejecutable(const char *comando) {
    for (int i = 0; i < g_path_len; i++) {
        // Reserva: dir + '/' + comando + '\0'
        size_t len = strlen(g_path[i]) + strlen(comando) + 2;
        char *ruta = malloc(len);
        if (ruta == NULL) {
            return NULL;
        }
        snprintf(ruta, len, "%s/%s", g_path[i], comando);
        if (access(ruta, X_OK) == 0) {
            return ruta; // Encontrado: el llamador libera.
        }
        free(ruta);
        ruta = NULL;
    }
    return NULL;
}

/*
    normalizar_operadores — Inserta espacios antes y después de los
    operadores > y & para simplificar el tokenizado posterior. Así
    el usuario puede escribirlos con o sin espacios alrededor, como
    pide el enunciado. Devuelve una cadena recién reservada (el
    llamador debe liberarla con free).
    1. linea: cadena original leída del usuario.
*/
char *normalizar_operadores(const char *linea) {
    size_t len = strlen(linea);
    // Caso peor: cada carácter se expande a 3 (" > ").
    char *nueva = malloc(len * 3 + 1);
    if (nueva == NULL) {
        return NULL;
    }
    size_t j = 0;
    for (size_t i = 0; i < len; i++) {
        if (linea[i] == '>' || linea[i] == '&') {
            nueva[j++] = ' ';
            nueva[j++] = linea[i];
            nueva[j++] = ' ';
        } else {
            nueva[j++] = linea[i];
        }
    }
    nueva[j] = '\0';
    return nueva;
}

/*
    es_builtin — Devuelve 1 si el comando corresponde a uno de los
    built-ins implementados, o 0 en caso contrario. Se aceptan tanto
    los nombres estándar (cd, path) como los que el profesor utilizó
    durante la clase y que aparecen en la lista numerada del enunciado
    (chd, route), para ser robustos ante la inconsistencia del PDF.
    1. comando: nombre del comando a evaluar.
*/
int es_builtin(const char *comando) {
    return (strcmp(comando, "exit")  == 0 ||
            strcmp(comando, "cd")    == 0 ||
            strcmp(comando, "chd")   == 0 ||
            strcmp(comando, "path")  == 0 ||
            strcmp(comando, "route") == 0);
}

/*
    ejecutar_builtin — Ejecuta un comando integrado en el mismo
    proceso del shell (sin fork/execv, como explica el enunciado).
    Valida la cantidad de argumentos según cada built-in:
      - exit: exactamente 0 argumentos adicionales.
      - cd:   exactamente 1 argumento.
      - path: 0 o más argumentos (sobrescribe el search path).
    1. args:  arreglo de argumentos, args[0] es el nombre del built-in.
    2. nargs: cantidad total de tokens en args.
*/
void ejecutar_builtin(char **args, int nargs) {
    if (strcmp(args[0], "exit") == 0) {
        if (nargs != 1) {
            print_error();
            return;
        }
        liberar_path();
        exit(0);
    }
    else if (strcmp(args[0], "cd") == 0 || strcmp(args[0], "chd") == 0) {
        // El profesor en clase lo llamó "chd"; el texto introductorio
        // del PDF usa "cd". Se acepta ambas variantes.
        if (nargs != 2) {
            print_error();
            return;
        }
        if (chdir(args[1]) != 0) {
            print_error();
        }
    }
    else if (strcmp(args[0], "path") == 0 || strcmp(args[0], "route") == 0) {
        // El profesor en clase lo llamó "route"; el texto introductorio
        // del PDF usa "path". Se acepta ambas variantes.
        // args[0] es el built-in; los directorios empiezan en args[1].
        establecer_path(&args[1], nargs - 1);
    }
}

/*
    ejecutar_externo — Busca el ejecutable en el search path, crea un
    proceso hijo con fork() y en el hijo, aplica redirección si la
    solicitó el usuario y llama a execv(). En el padre devuelve el pid
    del hijo para que el llamador espere más adelante.
    Retorna el pid del hijo, o -1 si no se pudo lanzar el comando.
    1. args:  argumentos del comando, args[0] es el nombre y debe
              terminar en NULL (lo exige execv).
    2. redir: nombre del archivo de redirección o NULL si no hay.
*/
pid_t ejecutar_externo(char **args, const char *redir) {
    char *ruta = buscar_ejecutable(args[0]);
    if (ruta == NULL) {
        print_error();
        return -1;
    }

    pid_t pid = fork();
    if (pid < 0) {
        print_error();
        free(ruta);
        ruta = NULL;
        return -1;
    }

    if (pid == 0) {
        // Proceso hijo.
        if (redir != NULL) {
            int fd = open(redir, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd < 0) {
                print_error();
                exit(1);
            }
            // Redirigir stdout y stderr al archivo (el "giro" del enunciado).
            if (dup2(fd, STDOUT_FILENO) < 0 || dup2(fd, STDERR_FILENO) < 0) {
                print_error();
                exit(1);
            }
            close(fd);
        }
        execv(ruta, args);
        // Si execv retorna, es error.
        print_error();
        exit(1);
    }

    // Proceso padre.
    free(ruta);
    ruta = NULL;
    return pid;
}

/*
    procesar_linea — Orquesta el análisis y la ejecución de una línea
    completa ingresada por el usuario. Los pasos son:
      1. Normalizar los operadores > y & insertando espacios alrededor.
      2. Tokenizar la línea usando strsep() con espacios y tabulaciones.
      3. Dividir la lista de tokens en segmentos separados por &
         (comandos paralelos).
      4. Para cada segmento, extraer la redirección (si hay) y validar.
      5. Ejecutar built-ins directamente o lanzar procesos externos con
         fork/execv.
      6. Esperar con waitpid() a todos los hijos paralelos antes de
         devolver el control al usuario.
    1. linea: cadena leída con getline() (puede incluir '\n' final).
*/
void procesar_linea(char *linea) {
    char *normalizada = normalizar_operadores(linea);
    if (normalizada == NULL) {
        print_error();
        return;
    }

    // Tokenizar por espacios, tabulaciones y saltos de línea usando strsep.
    char *tokens[MAX_TOKENS];
    int   ntok = 0;
    char *cursor = normalizada;
    char *tok;
    while ((tok = strsep(&cursor, " \t\n\r")) != NULL) {
        if (*tok == '\0') {
            continue; // strsep genera cadenas vacías entre separadores consecutivos.
        }
        if (ntok >= MAX_TOKENS) {
            print_error();
            free(normalizada);
            return;
        }
        tokens[ntok++] = tok;
    }

    // Dividir los tokens en comandos separados por &.
    pid_t pids[MAX_CMDS];
    int   npids = 0;

    int i = 0;
    while (i < ntok) {
        int start = i;
        while (i < ntok && strcmp(tokens[i], "&") != 0) {
            i++;
        }
        int end = i; // end es exclusivo.

        // Saltar segmentos vacíos (por ejemplo "  &  &  ").
        if (start < end) {
            char *args[MAX_ARGS];
            int   nargs = 0;
            const char *redir = NULL;
            int   err = 0;

            for (int j = start; j < end; j++) {
                if (strcmp(tokens[j], ">") == 0) {
                    // Validaciones de la redirección de salida.
                    if (nargs == 0)              { err = 1; break; } // falta comando
                    if (redir != NULL)           { err = 1; break; } // múltiples >
                    if (j + 1 >= end)            { err = 1; break; } // falta archivo
                    if (j + 2 < end)             { err = 1; break; } // tokens extra
                    redir = tokens[j + 1];
                    j = end; // salir del for.
                    break;
                } else {
                    if (nargs >= MAX_ARGS - 1) { err = 1; break; }
                    args[nargs++] = tokens[j];
                }
            }
            args[nargs] = NULL; // execv exige NULL al final.

            if (err) {
                print_error();
            } else if (nargs > 0) {
                if (es_builtin(args[0])) {
                    // El enunciado aclara que no se prueba la redirección
                    // de los built-ins, así que simplemente se ignora aquí.
                    ejecutar_builtin(args, nargs);
                } else {
                    pid_t pid = ejecutar_externo(args, redir);
                    if (pid > 0 && npids < MAX_CMDS) {
                        pids[npids++] = pid;
                    }
                }
            }
        }

        // Saltar el token "&" que delimita el siguiente comando.
        if (i < ntok && strcmp(tokens[i], "&") == 0) {
            i++;
        }
    }

    // Esperar a todos los procesos externos lanzados en paralelo.
    for (int k = 0; k < npids; k++) {
        waitpid(pids[k], NULL, 0);
    }

    free(normalizada);
    normalizada = NULL;
}

/*
    wish — Función principal del intérprete. Determina si el shell se
    ejecuta en modo interactivo (sin argumentos) o en modo batch (con
    un archivo como único argumento). Más de un argumento es un error
    fatal (exit(1)). En cada iteración lee una línea con getline() y
    llama a procesar_linea(); al encontrar EOF invoca exit(0).
    1. argc, argv: argumentos recibidos por main.
*/
int wish(int argc, char *argv[]) {
    FILE *entrada   = stdin;
    int   interactivo = 1;

    if (argc == 2) {
        entrada = fopen(argv[1], "r");
        if (entrada == NULL) {
            print_error();
            exit(1);
        }
        interactivo = 0;
    } else if (argc > 2) {
        print_error();
        exit(1);
    }

    inicializar_path();

    char   *linea = NULL;  // getline reservará la memoria dinámicamente.
    size_t  cap   = 0;
    ssize_t nleidos;

    while (1) {
        if (interactivo) {
            printf("wish> ");
            fflush(stdout); // Forzar impresión del prompt sin esperar newline.
        }

        nleidos = getline(&linea, &cap, entrada);
        if (nleidos == -1) {
            // EOF en cualquiera de los dos modos.
            free(linea);
            linea = NULL;
            liberar_path();
            if (!interactivo) {
                fclose(entrada);
            }
            exit(0);
        }

        procesar_linea(linea);
    }

    // Nunca se llega aquí.
    return 0;
}

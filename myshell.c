//Librerias para el uso de entradas, salidas y strings
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//Liberarias para el uso de signals, waits y forks
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>

//Librerias para el manejo y creacion de ficheros
#include <fcntl.h>

//Incluimos la libreria parser.h
#include "parser.h"

//Definimos constantes para escribir o leer informacion al usar pipes
#define READ_END 0
#define WRITE_END 1
#define ERROR_END 2
#define MAX_JOBS 250
#define MAX_LENGTH_LINE 1024

//Creacion de estructura para almacenar los procesos
struct pidData{
    pid_t pid;
    char state[8];
    char line[MAX_LENGTH_LINE];
};

//Funcion para presentar el mensaje inicial con la ruta actual correspondiente
void presentMsh(){
    //Declaracion de variables
    char currentDir[MAX_LENGTH_LINE];

    //Cambio y reseteo de colores de impresion
    printf("\033[1;32m");
    printf("%s", getcwd(currentDir, sizeof(currentDir)));
    printf("\033[1;34m");
    printf(" msh> ");
    printf("\033[0m");
}

//Funcion para manejar la ejecucion de procesos en curso hasta que especifiquemos un hijo con su pid para finalizarlo
void processAdministrator(){
    waitpid(WAIT_ANY, NULL, WNOHANG);
}

//Funcion para finalizar los procesos creados
void processFinalizer(int pidP){
    waitpid(pidP, NULL, 0);
}

//Funcion para controlar la activacion de señales
void signalActivation(){
    //Habilitacion de señales y paso a la siguiente señal
	signal(SIGINT, SIG_DFL);
	signal(SIGQUIT, SIG_DFL);
}

//Funcion para controlar la desactivacion de señales
void signalDeactivation(){
    //Deshabilitacion de señales y paso a la siguiente señal
	signal(SIGINT, SIG_IGN);
	signal(SIGQUIT, SIG_IGN);
}

//Funcion para crear pipes entre argumentos
int **pipeCreator(int **conduit, int pipeNumber){
    //Declaracion de variables
    int i;
    conduit = calloc(pipeNumber, sizeof(int*));
    //Queremos reservar arrays con posiciones de entrada y salida para tener
    //tuberias entre cada argumento para pasarle y escribir informacion
    for (i = 0; i < pipeNumber; i ++){
        conduit[i] = calloc(2, sizeof(int));
        if(pipe(conduit[i]) == -1){
            printf("Error creating the pipes\n");
            exit(1);
        };      
    }
    return conduit;
}

//Funcion para cerrar las conexiones entre argumentos por pipes
void pipeCloser(int **conduit, int pipeNumber){
    //Declaracion de variables
    int i;
    for (i = 0; i < pipeNumber; i++){
        close(conduit[i][READ_END]);
        close(conduit[i][WRITE_END]);
        free(conduit[i]);
    }
    free(conduit);
}

//Funcion para ejecutar los pipes 
void pipeExecution(int **conduit, int currentCommand, int pipeNumber){
    
    //Si nuestro comando actual es el primero READ_END ES 0, WRITE_END 1
    if (currentCommand == 0){
        dup2(conduit[currentCommand][WRITE_END],WRITE_END);
    }

    //Si nuestro comando actual es el ultimo
    else if (currentCommand == (pipeNumber)){
        dup2(conduit[currentCommand-1][READ_END],READ_END);
    }

    //Si no es ni el primer ni el ultimo comando
    else{
        dup2(conduit[currentCommand][WRITE_END],WRITE_END);
        dup2(conduit[currentCommand-1][READ_END],READ_END);
    }
    //Cerramos los pipes llamando a nuestra funcion
    pipeCloser(conduit, pipeNumber);
}

//Funcion para comprobar si el comando se ejecuta o no en background
int hasBackground(int check){
    //Si no tenemos background activado, activamos las señales a su valor default
    if(check == 0){
        signalActivation();
        return 0;
    }
    //Si tenemos background activado, llamamos a processAdministrator para lidiar con la ejecucion de los hijos
    else{
        signal(SIGCHLD, processAdministrator); 
        return 1;
    }
}

//Funcion para asignar las tuberias necesarias para cada caso entre mandatos
int interpreter(tline *linetoCheck, struct pidData *jobList, char *lineBuffer, int currentJobs){

    //Declaracion de variables
    int i;
    int verifyBackground;
    int argNumber;
    int pipeNumber;
    int **pipeArray;
    pid_t *processPid;
    pid_t childProcess;

    //Variables destinadas a comprobaciones con las redirecciones
    int inputRedirection;
    int outputRedirection;
    int errorRedirection;

    //Asignacion de variables
    argNumber = linetoCheck->ncommands;
    pipeNumber = argNumber - 1;
    pipeArray = 0;
    
    //Comprobamos si tenemos background
    verifyBackground = hasBackground(linetoCheck->background);
    //Si tenemos mas de un argumento creamos las pipes necesarias
    if(argNumber > 1){
        pipeArray = pipeCreator(pipeArray, pipeNumber);     
    }
    processPid = calloc(argNumber, sizeof(pid_t));

    //Recorremos cada argumento
    for (i = 0; i < argNumber; i++){
        childProcess = fork();
        //Proceso padre en ejecucion
        if(childProcess > 0){         
            //Si tenemos background lo metemos las operaciones en nuestra jobList
            if(verifyBackground == 1){
                jobList[currentJobs].pid = childProcess;
                strcpy(jobList[currentJobs].state, "Running");
                strtok(lineBuffer, "\n\r");
                strcpy(jobList[currentJobs].line, lineBuffer);
            }
            //Guardamos el pid del proceso hijo en nuestro array de procesos
            processPid[i] = childProcess;
            continue;
        }
        //Proceso hijo en ejecucion
        else if (childProcess == 0){
            //COMPROBACION DE REDIRECCIONES****************************************************************************
            //En caso de que nuestro primer argumento tenga redireccion de entrada
            if ((i == 0) && (linetoCheck->redirect_input != NULL)){
                //Abrimos solo para lectura el fichero especificado
                inputRedirection = open(linetoCheck->redirect_input, O_RDONLY);
                //Si al abrir el archivo  nos devuelve -1, entonces no habriamos encontrado el fichero
                if (inputRedirection == -1){
                    printf("No such file or directory\n");
                    exit(1);
                }else {
                    //Asignamos la entrada estandar a el fichero abierto
                    dup2(inputRedirection, READ_END);
                    close(inputRedirection);                   
                }
            }
            //En caso de que nuestro ultimo comando tenga redireccion de salida o error
            if ((i == pipeNumber) && ((linetoCheck->redirect_output != NULL) || ((linetoCheck->redirect_error != NULL)))){
                //Si tenemos redireccion de salida
                if(linetoCheck->redirect_output){
                    outputRedirection = creat(linetoCheck->redirect_output, O_WRONLY);
                    if(outputRedirection  == -1){
                        printf("No such file or directory\n");
                        exit(1);
                    }else {
                        //Asignamos la salida estandar al fichero abierto
                        dup2(outputRedirection, WRITE_END);
                        close(outputRedirection); 
                    }
                }
                //Si tenemos redireccion de error
                else{
                    errorRedirection = creat(linetoCheck->redirect_error, O_WRONLY);
                    if(errorRedirection == -1){
                        printf("No such file or directory\n");
                        exit(1);
                    }else {
                        //Asignamos el error estandar al fichero abierto
                        dup2(errorRedirection, ERROR_END);
                        close(errorRedirection); 
                    }
                }
            }
            //FIN DE COMPROBACION DE REDIRECCIONES*******************************************************************
            
            //Llamamos a la funcion para ejecutar los pipes abriendo y cerrando sus puertas de lectura y escritura
            if(argNumber > 1){
                pipeExecution(pipeArray, i, pipeNumber);
            }
            //Ejecutamos el comando actual     
            execvp(linetoCheck->commands[i].filename, linetoCheck->commands[i].argv);
            exit(1);
        }
        //Error al crear hijo y notificacion de ello al padre
        else{
            printf("Error during fork creation\n");
            exit(1);
        }
    }
    
    //Cerramos las tuberias 
    if(argNumber > 1){
        pipeCloser(pipeArray, pipeNumber);
    }
    //Si no tenemos background suspendemos la ejecucion en curso hasta que un hijo haya acabado
    if(linetoCheck->background == 0){
        for(i=0; i < linetoCheck->ncommands;i++){
			processFinalizer(processPid[i]);
		}
    }
    //Liberamos la memoria de los processPid
    free(processPid);
    //Imprimimos el pid del proceso actual
    if(verifyBackground == 1){
        printf("[%d]  %d \n", currentJobs+1, jobList[currentJobs].pid);
        currentJobs++;
    }
    return currentJobs;
}

//Funcion cd
void cdMan(tline *path){
    //Declaracion de variables
    char *dirPath;
    int dir;
    //Asignamos a nuestro puntero a char directory un path en funcion 
    //de si tiene o no un directorio especificado 
    if(path->commands[0].argv[1] == NULL){
        dirPath = getenv("HOME");
    }
    else{
        dirPath = path->commands[0].argv[1];
    }
    dir = chdir(dirPath);
    //Si hay algun problema para cambiar el directorio se muestra por pantalla
    if(dir < 0){
        printf("%s: no such file or directory\n", dirPath);
    }
}

//Funcion para comprobar los procesos activos, eliminar los terminados y actualizar la lista 
int checkRunning(struct pidData *jobList, int currentJobs){
    //Declaracion de variables
    int i;
    int somethingEnded;
    //Asignacion de variables
    somethingEnded = 1;
    for (i = 0; i <= currentJobs; i ++){
        if(kill(jobList[i].pid, 0) != 0 && strcmp(jobList[i].state, "Ended+") != 0){
            strcpy(jobList[i].state, "Ended-");     
            somethingEnded = 0;          
        }
    }   
    return somethingEnded;
}

//Funcion jobs para imprimir los procesos actuales
void jobsMan(struct pidData *jobList, int currentJobs){
    //Declaracion de variables
    int i;
    checkRunning(jobList, currentJobs);
    for (i = 0; i < currentJobs; i ++){
        if(strcmp(jobList[i].state, "Ended-") != 0 && strcmp(jobList[i].state, "Ended+") != 0){
            printf("[%d] Process -> %d [+] %s :  %s \n", i+1, jobList[i].pid, jobList[i].state, jobList[i].line);
        }
    }
}

//Nos muestra los procesos terminados
int printEnded(struct pidData *jobList, int currentJobs){
    //Declaracion de variables
    int i;
    int somethingEnded;
    //Asignacion de variables
    somethingEnded = 1;
    //Comprobacion de procesos activos
    if ( checkRunning(jobList, currentJobs) == 0 ) {
        for (i = 0; i < currentJobs; i ++){
            if(strcmp(jobList[i].state, "Ended-") == 0){
                printf("\n[%d]  %d    %s \n", i+1, jobList[i].pid, jobList[i].state);
                strcpy(jobList[i].state, "Ended+");
                somethingEnded = 0;
            }
        }
    }
    return somethingEnded;
}

//Funcion fg
void fgMan(struct pidData *jobList, int currentJobs, tline *path){
    //Declaracion de variables
    int wantedPid;
    int done;
    int exit;
    int i;
    //Asignacion de variables
    done = 1;
    checkRunning(jobList, currentJobs);
    //Si no se nos especifica un PID cogeremos el ultimo que este running
    if(path->commands[0].argv[1] == NULL){
        i = currentJobs-1;
        exit = 1; 
        while(exit != 0 && i > 0 && done != 0){
            if(strcmp(jobList[i].state, "Running") == 0){
                signalActivation();
                while(!kill(jobList[i].pid, 0)){
                }
                done = 0;
            }
            i--;
        }
    }
    //Si se nos especifica un PID
    else{
        wantedPid = (int) strtol(path->commands[0].argv[1], NULL, 10);
        exit = 1;
        i = 0;
        while(exit != 0 && i < currentJobs){
            if(jobList[i].pid == wantedPid && strcmp(jobList[i].state, "Running") == 0){
                signalActivation();
                while(!kill(wantedPid, 0)){
                }
                done = 0;
            }
            i++;
        }
    }
    if(done != 0){
        printf("Process couldn't be found or has ended already\n");
    }
}

//Funcion umask
void umaskMan(tline *path){
    //Leer o convertir a octal para los permisos
    //Declaracion de variables
    int fd;
    int decimalNewmask;
    int octalNewmask;
    int aux;
    mode_t oldmask;

    //Si no tenemos argumento tras el umask nos muestra nuestra mascara actual
    if(path->commands[0].argv[1] == NULL){
    printf("Your current user mask is:\n");
    oldmask = umask(S_IRWXG);
    if ((fd = creat("umask.file", S_IRWXU|S_IRWXG)) < 0){
        perror("creat() error");
    }
    else{
        system("ls -l umask.file");
        close(fd);
        unlink("umask.file");
    }
    umask(oldmask);
    }
    //En caso de tener argumento, asignaremos nuestra umask a la especificada por el usuario
    else{
        decimalNewmask = atoi(path->commands[0].argv[1]);
        octalNewmask = 0;
        while(decimalNewmask){
            octalNewmask = octalNewmask + (decimalNewmask % 8 ) * aux;
            decimalNewmask = decimalNewmask / 8;
            aux = aux * 10;
        }
        umask(octalNewmask);
    }
    presentMsh();
}

//Funcion exit
void exitMan(struct pidData *jobList, int currentJobs){
    //Declaracion de variables
    int i;
    //Comprobamos los procesos que esten running 
    checkRunning(jobList, currentJobs);
    for (i = 0; i < currentJobs; i ++){
        if(strcmp(jobList[i].state, "Ended-") != 0 || strcmp(jobList[i].state, "Ended+") != 0){
            kill(jobList[i].pid, SIGKILL);
        }
    }
    exit(0);
}

//Funcion principal ++++++++++++ MAIN ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
int main(void){

    //Declaracion de variables
    char lineBuffer[MAX_LENGTH_LINE];
    tline *linetoCheck;
    struct pidData jobList[MAX_JOBS];
    int currentJobs;

    //Asignacion de variables
    currentJobs = 0;

    //Presentamos el mensaje inicial
    presentMsh();

    //Se deshabilitan las señales nada más empezar
    signalDeactivation();
    
    //Bucle que nos repite el prompt y nos selecciona la funcion a llamar en base al input
    while(fgets(lineBuffer, MAX_LENGTH_LINE, stdin) != NULL){
        //Comprobamos el salto de linea
        if(strcmp(lineBuffer, "\n") == 0){
            presentMsh();
            if( printEnded(jobList, currentJobs) == 0 ){
                presentMsh();
            };    
        }else{
            //Usamos el tokenize para tener todos los apartados de la linea que introducimos
            linetoCheck = tokenize(lineBuffer);
            if(linetoCheck == NULL){
                continue;
            }
            if(strcmp(linetoCheck->commands[0].argv[0], "cd") == 0){
                cdMan(linetoCheck);        
            }
            if(strcmp(linetoCheck->commands[0].argv[0], "exit") == 0){
                exitMan(jobList, currentJobs);
            }
            if(strcmp(linetoCheck->commands[0].argv[0], "jobs") == 0){
                jobsMan(jobList, currentJobs); 
            }
            if(strcmp(linetoCheck->commands[0].argv[0], "fg") == 0){
                fgMan(jobList, currentJobs, linetoCheck); 
            }
            if(strcmp(linetoCheck->commands[0].argv[0], "umask") == 0){
                umaskMan(linetoCheck);
            }
            else{
                currentJobs = interpreter(linetoCheck, jobList, lineBuffer, currentJobs);
                presentMsh();
                if( printEnded(jobList, currentJobs) == 0 ){
                    presentMsh();
                };  
            }   
        }
    }
    return 0;
}
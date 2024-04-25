/**
 * ==========================================================
 * @author Valentyn Vorobec
 * Datum vytvoření: 2022
 * ==========================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <semaphore.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>

typedef struct
{
    int A;
    int qO;
    int qH;
    int mol;
    int checkH;
    int checkO;
    int checkH2;
    int checkO2;
    int created;
    int secondH;
    int end;
    int inQO;
    int inQH;
    int creating;
} memory;

memory *mem = NULL;

sem_t *sem_print = NULL;
sem_t *sem_barrier_1 = NULL;
sem_t *sem_barrier_2 = NULL;
sem_t *sem_barrier_3 = NULL;
sem_t *sem_barrier_1_h = NULL;
sem_t *sem_barrier_2_h = NULL;
sem_t *sem_barrier_3_h = NULL;
sem_t *sem_check1 = NULL;
sem_t *sem_check2 = NULL;

FILE *printOut = NULL;

/**
 * ==========================================================
 * Zkontroluj zda input je cislo
 * @param checkString - input
 * @return 1 pokud input neni cislo
 * ==========================================================
 */
int isNumber(char *checkString)
{
    int length,i; 
    length = strlen(checkString);
    for (i = 0; i < length; i++)
        if (!isdigit(checkString[i]))
            return 1;
    return 0;
}

/**
 * ==========================================================
 * Zkontroluj paramenty
 * @param argc - pocet parametru
 * @param argv - parametry
 * @return 1 pokud doslo k chybe
 * ==========================================================
 */
int check_param(int argc, char *argv[])
{
    if (argc == 5)
    { 
        if (isNumber(argv[1])
        || isNumber(argv[2])
        || isNumber(argv[3])
        || isNumber(argv[4]))
        {
            fprintf(stderr, "Zadejte prosim jenom cisla\n");
            return 1;
        }

        if (atoi(argv[1]) <= 0)
        {
            fprintf(stderr, "NO musi byt vetsi nez 0!\n");
            return 1;
        }
        if (atoi(argv[2]) <= 0)
        {
            fprintf(stderr, "NH musi byt vetsi nez 0!\n");
            return 1;
        }
        if (atoi(argv[3]) < 0 || atoi(argv[3]) > 1000)
        {
            fprintf(stderr, "TI musi byt mezi 0 a 1000!\n");
            return 1;
        }
        if (atoi(argv[4]) < 0 || atoi(argv[4]) > 1000)
        {
            fprintf(stderr, "TB musi byt mezi 0 a 1000!\n");
            return 1;
        }
    }
    else
    {
        fprintf(stderr, "Spatny format!\n");
        fprintf(stderr, "Pouziti: ./proj2 NO NH TI\n");
        fprintf(stderr, "NO - Pocet kysliku\n");
        fprintf(stderr, "NH - Pocet vodiku\n");
        fprintf(stderr, "TI - Max. cas v mil. sekundach, po kterem atom ceka na zarazeni\n");
        fprintf(stderr, "   - cislo mezi 0 - 1000\n");
        fprintf(stderr, "TB - Max. cas v mil. sekundach pro vytvoreni molekuly \n");
        fprintf(stderr, "   - cislo mezi 0 - 1000\n");
        return 1;
    }
    return 0;
}

/**
 * ==========================================================
 * Pockej nejaky interval od 0 do interval
 * @param interval
 * ==========================================================
 */
void waitInterval(int interval){
    if (interval > 0)
    {
        srand(time(NULL));
        int ran = (rand() % interval) * 1000;
        usleep(ran);
    }
    else usleep(0);
}

/**
 * ==========================================================
 * Funkce na vytvoreni shared memory
 * @return 1 pokud nastala chyba
 * ==========================================================
 */
int memory_create()
{
    mem = mmap(NULL, sizeof(memory), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if(mem == MAP_FAILED)
	{
		fprintf(stderr, "Error occured while creating shared memory!\n");
		return 1;
	}

    mem->A = 1;
    mem->qH = 0;
    mem->qO = 0;
    mem->mol = 1;
    mem->checkH = 0;
    mem->checkO = 0;
    mem->checkH2 = 0;
    mem->checkO2 = 0;
    mem->created = 3;
    mem->secondH = 2;
    mem->end = 0;
    mem->inQO = 0;
    mem->inQH = 0;
    mem->creating = 0;

    return 0;
}

/**
 * ==========================================================
 * Funkce na zniceni shared memory
 * @return 1 pokud nastala chyba
 * ==========================================================
 */

int memory_destroy()
{
    int destroy = munmap(mem, sizeof(memory));
    if(destroy == -1)
	{
		fprintf(stderr, "Error occured while destroying shared memory!\n");
		return 1;
	}
    return 0;
}

/**
 * ==========================================================
 * Funkce na otevreni semaforu
 * @return 1 pokud nastala chyba
 * ==========================================================
 */
int semaphores_open(sem_t **sem, char *name, int value)
{
    sem_unlink(name);
    *sem = sem_open(name, O_CREAT | O_EXCL, 0666, value);
    if (*sem == SEM_FAILED)
	{
		fprintf(stderr, "Error occured while opening semaphore!\n");
		return 1;
	}
    return 0;
}

/**
 * ==========================================================
 * Funkce na zavreni semaforu
 * @return 1 pokud nastala chyba
 * ==========================================================
 */
int semaphores_close(sem_t **sem, char *name)
{
    int close = sem_close(*sem);
    if (close == -1)
	{
		fprintf(stderr, "Error occured while closing semaphore!\n");
		return 1;
	}

    close = sem_unlink(name);
    if (close == -1)
	{
		fprintf(stderr, "Error occured while unlinking semaphore!\n");
		return 1;
	}
    return 0;
}

/**
 * ==========================================================
 * Proces kysliku
 * @param n - pocet kysliku
 * @param TI - cas na uspani
 * @param TB - cas na uspani
 * @return 1 pokud doslo k chybe
 * 
 * Je vytvoren for loop, kde vzdy dojde ke forknuti.
 * Nejdrive se mezi semafory sep_print vypise started a po sleepu going to queue.
 * Pote se nam proces rozdeli do 3. barier.
 * Dp prvni bariery se vzdy dostanou 3 prvku (1 O, 2 H) a pote
 * se prejde do druhe, kde se zkontroluje zda neni konec.
 * Pokud ne, tak se vytvari molekula a nasledne se zkontroluje
 * zda 3 prvky se zacali vytvaret. Pokud jo, povoli se 3. bariera.
 * Ta se stara o to aby molekula byla vytvorena.
 * ==========================================================
 */
int oxygen(int n, int TI, int TB)
{
    for (int i = 1; i <= n; i++)
    {
        int id = fork();
        if (id < 0) 
        {
            fprintf(stderr, "Error occured while forking!\n");
		    return 1;
        } 
        if (id == 0)
        {
            // O started...
            sem_wait(sem_print);
            fprintf(printOut, "%d: O %d: started\n", mem->A, i);
            fflush(printOut); 
            mem->A++;
            sem_post(sem_print);

            // Pockej nejaku dobu
            waitInterval(TI);

            // O going to queue...
            sem_wait(sem_print);
            fprintf(printOut, "%d: O %d: going to queue\n", mem->A, i);
            fflush(printOut);
            mem->qO++;
            mem->A++;
            sem_post(sem_print);

            // Prnvni check
            sem_wait(sem_check1);
            if(mem->qH - mem->checkH > 1 && mem->qO - mem->checkO > 0)
            {
                mem->checkO++;
                mem->checkH += 2;
                sem_post(sem_barrier_1);
                sem_post(sem_barrier_1_h);
            }
            sem_post(sem_check1);

            // Prvni bariera
            sem_wait(sem_barrier_1);
            mem->inQO++;

            // Druha bariera
            // Pockej na signal, aby jsi mohl zaci vytvaret molekulu
            sem_wait(sem_barrier_2); 

            // Pokud end je nastaven na 1, nech zbyvajici O projit    
            if (mem->end == 1) 
            {
                sem_post(sem_barrier_1);
                sem_wait(sem_print);
                fprintf(printOut, "%d: O %d: not enough H\n", mem->A, i);
                fflush(printOut);
                mem->A++;
                sem_post(sem_print);
                if (mem->inQO > -1) 
                {
                    mem->inQO--;
                    sem_post(sem_barrier_2);
                }
                exit(0);
            }

            // Creating molecule...
            sem_wait(sem_print);
            fprintf(printOut, "%d: O %d: creating molecule %d\n", mem->A, i, mem->mol);
            fflush(printOut);
            mem->A++;
            mem->checkO2++;
            sem_post(sem_print);

            // Druhy check
            sem_wait(sem_check2);
            if (mem->checkO2 == 1 && mem->checkH2 == 2)
            {
                mem->checkO2 = 0;
                mem->checkH2 = 0;
                sem_post(sem_barrier_3);
            }
            sem_post(sem_check2);
            sem_post(sem_barrier_1);

            // Pockej nejaku dobu
            waitInterval(TB);

            // Treti bariera
            sem_wait(sem_barrier_3);

            // Molecule created...
            sem_wait(sem_print);
            fprintf(printOut, "%d: O %d: molecule %d created\n", mem->A, i, mem->mol);
            fflush(printOut); 
            mem->A++;
            mem->created--;
            mem->inQO--;
            sem_post(sem_print);

            // Povol H vytvorit molekulu
            sem_post(sem_barrier_3_h);
            sem_post(sem_barrier_3_h);

            // Zkontroluje zda byla molekula vytvorena
            if (mem->created == 0)
            {
                mem->created = 3;
                mem->secondH = 2;
                mem->mol++;
                if (mem->inQO < 1 || mem->inQH < 2) mem->end = 1;
                else mem->end = 0;
                sem_post(sem_barrier_2);
                sem_post(sem_barrier_2_h);
            }
            exit(0);
        }
    }
    
    return 0;
}

/**
 * ==========================================================
 * Proces vodiku
 * @param n - pocet O
 * @param TI - cas na uspani
 * @return 1 pokud doslo k chybe
 * ==========================================================
 *
 * Je vytvoren for loop, kde vzdy dojde ke forknuti.
 * Nejdrive se mezi semafory sep_print vypise started a po sleepu going to queue.
 * Pote se nam proces rozdeli do 3. barier.
 * Dp prvni bariery se vzdy dostanou 3 prvku (1 O, 2 H) a pote
 * se prejde do druhe, kde se zkontroluje zda neni konec.
 * Pokud ne, tak se vytvari molekula a nasledne se zkontroluje
 * zda 3 prvky se zacali vytvaret. Pokud jo, povoli se 3. bariera.
 * Ta se stara o to aby molekula byla vytvorena.
 * ==========================================================
 */
int hydrogen(int n, int TI)
{
    for (int i = 1; i <= n; i++)
    {
        int id = fork();
        if (id < 0) 
        {
            fprintf(stderr, "Error occured while forking!\n");
		    return 1;
        } 

        if (id == 0)
        {
            // H started...
            sem_wait(sem_print);
            fprintf(printOut, "%d: H %d: started\n", mem->A, i);
            fflush(printOut); 
            mem->A++;
            sem_post(sem_print);

            // Pockej nejaku dobu
            waitInterval(TI);

            // H going to queue...
            sem_wait(sem_print);
            fprintf(printOut, "%d: H %d: going to queue\n", mem->A, i);
            fflush(printOut);
            mem->qH++;
            mem->A++;
            sem_post(sem_print);

            // Prnvni check
            sem_wait(sem_check1);
            if(mem->qH - mem->checkH > 1 && mem->qO - mem->checkO > 0)
            {
                mem->checkO++;
                mem->checkH += 2;
                sem_post(sem_barrier_1);
                sem_post(sem_barrier_1_h);
            }

            // Pokud vstup je 1 1 TI TB
            else if (n == 1)
            {
                mem->end = 1;
                sem_post(sem_barrier_1);
                sem_post(sem_barrier_1_h);
            }
            sem_post(sem_check1);

            // Prvni bariera
            sem_wait(sem_barrier_1_h);
            sem_post(sem_barrier_1_h);
            mem->inQH++;
            
            // Druha bariera
            // Pockej na signal, aby jsi mohl zaci vytvaret molekulu
            sem_wait(sem_barrier_2_h);

            // Povol druhemu H vytvaret molekulu
            mem->secondH--;
            if (mem->secondH > 0) sem_post(sem_barrier_2_h);

            // Pokud end je nastaven na 1, nech zbyvajici H projit
            if (mem->end == 1) 
            {
                sem_wait(sem_print);
                fprintf(printOut, "%d: H %d: not enough O or H\n", mem->A, i);
                fflush(printOut);
                mem->A++;
                sem_post(sem_print);
                if (mem->inQH > 0) 
                {
                    mem->inQH--;
                    sem_post(sem_barrier_2_h);
                }
                exit(0);
            }

            // Creating molecule...
            sem_wait(sem_print);
            fprintf(printOut, "%d: H %d: creating molecule %d\n", mem->A, i, mem->mol);
            fflush(printOut); 
            mem->A++;
            mem->checkH2++;
            sem_post(sem_print);

            // Druhy check
            sem_wait(sem_check2);
            if (mem->checkO2 == 1 && mem->checkH2 == 2)
            {
                mem->checkO2 = 0;
                mem->checkH2 = 0;
                sem_post(sem_barrier_3);
            }
            sem_post(sem_check2);
            sem_post(sem_barrier_1_h);

            // Treti bariera
            sem_wait(sem_barrier_3_h);

            // Molecule created...
            sem_wait(sem_print);
            fprintf(printOut, "%d: H %d: molecule %d created\n", mem->A, i, mem->mol);
            fflush(printOut); 
            mem->A++;
            mem->inQH--;
            mem->created--;
            sem_post(sem_print);

            // Zkontroluje zda byla molekula vytvorena
            if (mem->created == 0)
            {
                mem->created = 3;
                mem->secondH = 2;
                mem->mol++;
                if (mem->inQO < 1 || mem->inQH < 2) mem->end = 1;
                else mem->end = 0;
                sem_post(sem_barrier_2);
                sem_post(sem_barrier_2_h);
            }
            exit(0);
        }
    }

    return 0;
}

int main(int argc, char *argv[]) 
{
    if (check_param(argc, argv) == 0)
    {
        printOut = fopen("proj2.out","w");

        int NO = atoi(argv[1]);
        int NH = atoi(argv[2]);
        int TI = atoi(argv[3]);
        int TB = atoi(argv[4]);

        // Vytvor shared memory a semafory
        if (memory_create() == 1) return 1;
        if (semaphores_open(&sem_print, "/ios.proj2.semPrint", 1) == 1) return 1;
        if (semaphores_open(&sem_barrier_1, "/ios.proj2.semBarrier1", 0) == 1) return 1;
        if (semaphores_open(&sem_barrier_2, "/ios.proj2.semBarrier2", 1) == 1) return 1;
        if (semaphores_open(&sem_barrier_3, "/ios.proj2.semBarrier3", 0) == 1) return 1;
        if (semaphores_open(&sem_barrier_1_h, "/ios.proj2.semBarrier1H", 0) == 1) return 1;
        if (semaphores_open(&sem_barrier_2_h, "/ios.proj2.semBarrier2H", 1) == 1) return 1;
        if (semaphores_open(&sem_barrier_3_h, "/ios.proj2.semBarrier3H", 0) == 1) return 1;
        if (semaphores_open(&sem_check1, "/ios.proj2.semCheck1", 1) == 1) return 1;
        if (semaphores_open(&sem_check2, "/ios.proj2.semCheck2", 1) == 1) return 1;

        if (oxygen(NO, TI, TB) == 1) return 1;
        if (hydrogen(NH, TI) == 1) return 1;

        while (wait(NULL) != -1);

        // Znic shared memory a semafory
        if (semaphores_close(&sem_print, "/ios.proj2.semPrint") == 1) return 1;
        if (semaphores_close(&sem_barrier_1, "/ios.proj2.semBarrier1") == 1) return 1;
        if (semaphores_close(&sem_barrier_2, "/ios.proj2.semBarrier2") == 1) return 1;
        if (semaphores_close(&sem_barrier_3, "/ios.proj2.semBarrier3") == 1) return 1;
        if (semaphores_close(&sem_barrier_1_h, "/ios.proj2.semBarrier1H") == 1) return 1;
        if (semaphores_close(&sem_barrier_2_h, "/ios.proj2.semBarrier2H") == 1) return 1;
        if (semaphores_close(&sem_barrier_3_h, "/ios.proj2.semBarrier3H") == 1) return 1;
        if (semaphores_close(&sem_check1, "/ios.proj2.semCheck1") == 1) return 1;
        if (semaphores_close(&sem_check2, "/ios.proj2.semCheck2") == 1) return 1;
        if (memory_destroy() == 1) return 1;

        fclose(printOut);
    } 
    else return 1;
    
    return 0;
}

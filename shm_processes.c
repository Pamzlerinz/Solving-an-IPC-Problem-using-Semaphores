#include  <stdio.h>
#include  <stdlib.h>
#include  <sys/types.h>
#include  <sys/ipc.h>
#include  <sys/shm.h>
#include  <unistd.h>
#include  <sys/wait.h>
#include  <semaphore.h>
#include  <fcntl.h>
#include  <signal.h>
#include  <time.h>
#include  <sys/prctl.h>

/*
 * Poor Student / Dear Old Dad (with Lovable Mom + N students extra credit)
 *
 * Usage:  ./shm_processes [numParents numStudents]
 *   ./shm_processes          -> Dear Old Dad + 1 Poor Student
 *   ./shm_processes 1 3      -> Dear Old Dad + 3 Poor Students
 *   ./shm_processes 2 10     -> Dear Old Dad + Lovable Mom + 10 Poor Students
 *
 * The BankAccount is a shared int (System V shared memory). A named
 * semaphore "mutex" guards every read-modify-write of the BankAccount,
 * so no two processes are ever in the critical section at once.
 */

#define SEM_NAME "/psdd_bank_mutex"

int    ShmID;
int   *BankAccount;
sem_t *mutex;
pid_t *childPids = NULL;
int    numChildren = 0;

void DearOldDad(void);
void LovableMom(void);
void PoorStudent(void);

/* Parent cleanup when killed (e.g. kill -INT <parent pid> or Ctrl-C). */
void cleanup(int sig)
{
     (void)sig;
     for (int i = 0; i < numChildren; i++) {
          if (childPids[i] > 0)
               kill(childPids[i], SIGTERM);
     }
     while (wait(NULL) > 0)
          ;
     shmdt((void *) BankAccount);
     shmctl(ShmID, IPC_RMID, NULL);
     sem_close(mutex);
     sem_unlink(SEM_NAME);
     exit(0);
}

/* Fork a child that runs `role`; the child dies if the parent dies. */
pid_t spawn(void (*role)(void))
{
     pid_t pid = fork();
     if (pid < 0) {
          printf("*** fork error ***\n");
          exit(1);
     }
     if (pid == 0) {
          signal(SIGINT, SIG_DFL);
          signal(SIGTERM, SIG_DFL);
          prctl(PR_SET_PDEATHSIG, SIGTERM);   /* die with the parent */
          if (getppid() == 1)                 /* parent already gone */
               exit(0);
          srand(time(NULL) ^ (getpid() << 16));
          role();
          exit(0);
     }
     return pid;
}

int  main(int  argc, char *argv[])
{
     int numParents = 1, numStudents = 1;

     if (argc == 3) {
          numParents  = atoi(argv[1]);
          numStudents = atoi(argv[2]);
     } else if (argc != 1) {
          printf("Use: %s [#parents(1 or 2) #students]\n", argv[0]);
          exit(1);
     }
     if (numParents < 1)  numParents = 1;
     if (numParents > 2)  numParents = 2;
     if (numStudents < 1) numStudents = 1;

     /* Line-buffered output: each line is written whole, so lines from
        different processes never get mixed together. */
     setvbuf(stdout, NULL, _IOLBF, 0);

     /* Shared BankAccount */
     ShmID = shmget(IPC_PRIVATE, sizeof(int), IPC_CREAT | 0666);
     if (ShmID < 0) {
          printf("*** shmget error ***\n");
          exit(1);
     }
     BankAccount = (int *) shmat(ShmID, NULL, 0);
     if (BankAccount == (int *) -1) {
          printf("*** shmat error ***\n");
          exit(1);
     }
     *BankAccount = 0;

     /* Semaphore for mutual exclusion on the BankAccount */
     sem_unlink(SEM_NAME);   /* remove any leftover from a previous run */
     if ((mutex = sem_open(SEM_NAME, O_CREAT, 0644, 1)) == SEM_FAILED) {
          perror("semaphore initialization");
          exit(1);
     }

     signal(SIGINT, cleanup);
     signal(SIGTERM, cleanup);

     /* Create Lovable Mom (if requested) and the Poor Students */
     childPids = malloc(sizeof(pid_t) * (numStudents + 1));
     if (numParents == 2)
          childPids[numChildren++] = spawn(LovableMom);
     for (int i = 0; i < numStudents; i++)
          childPids[numChildren++] = spawn(PoorStudent);

     /* The original process is Dear Old Dad */
     srand(time(NULL) ^ (getpid() << 16));
     DearOldDad();

     return 0;
}

void DearOldDad(void)
{
     int localBalance = 0;

     while (1) {
          sleep(rand() % 6);                       /* 0 - 5 seconds */
          printf("Dear Old Dad: Attempting to Check Balance\n");

          if (rand() % 2 == 0) {
               if (localBalance < 100) {
                    /* Deposit Money */
                    sem_wait(mutex);
                    localBalance = *BankAccount;
                    int amount = rand() % 101;     /* 0 - 100 */
                    if (amount % 2 == 0) {
                         localBalance += amount;
                         printf("Dear old Dad: Deposits $%d / Balance = $%d\n",
                                amount, localBalance);
                         *BankAccount = localBalance;
                    } else {
                         printf("Dear old Dad: Doesn't have any money to give\n");
                    }
                    sem_post(mutex);
               } else {
                    printf("Dear old Dad: Thinks Student has enough Cash ($%d)\n",
                           localBalance);
               }
          } else {
               /* Just check the balance */
               sem_wait(mutex);
               localBalance = *BankAccount;
               sem_post(mutex);
               printf("Dear Old Dad: Last Checking Balance = $%d\n", localBalance);
          }
     }
}

void LovableMom(void)
{
     int localBalance = 0;

     while (1) {
          sleep(rand() % 11);                      /* 0 - 10 seconds */
          printf("Loveable Mom: Attempting to Check Balance\n");

          sem_wait(mutex);
          localBalance = *BankAccount;
          if (localBalance <= 100) {
               /* Deposit Money */
               int amount = rand() % 126;          /* 0 - 125 */
               localBalance += amount;
               printf("Lovable Mom: Deposits $%d / Balance = $%d\n",
                      amount, localBalance);
               *BankAccount = localBalance;
          }
          sem_post(mutex);
     }
}

void PoorStudent(void)
{
     int localBalance = 0;

     while (1) {
          sleep(rand() % 6);                       /* 0 - 5 seconds */
          printf("Poor Student: Attempting to Check Balance\n");

          if (rand() % 2 == 0) {
               /* Withdraw Money */
               sem_wait(mutex);
               localBalance = *BankAccount;
               int need = rand() % 51;             /* 0 - 50 */
               printf("Poor Student needs $%d\n", need);
               if (need <= localBalance) {
                    localBalance -= need;
                    printf("Poor Student: Withdraws $%d / Balance = $%d\n",
                           need, localBalance);
               } else {
                    printf("Poor Student: Not Enough Cash ($%d)\n", localBalance);
               }
               *BankAccount = localBalance;
               sem_post(mutex);
          } else {
               /* Just check the balance */
               sem_wait(mutex);
               localBalance = *BankAccount;
               sem_post(mutex);
               printf("Poor Student: Last Checking Balance = $%d\n", localBalance);
          }
     }
}
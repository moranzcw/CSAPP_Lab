/* 
 * tsh - A tiny shell program with job control
 * 
 * <Put your name and login ID here>
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>

/* Misc manifest constants */
#define MAXLINE    1024   /* max line size */
#define MAXARGS     128   /* max args on a command line */
#define MAXJOBS      16   /* max jobs at any point in time */
#define MAXJID    1<<16   /* max job ID */

/* Job states */
#define UNDEF 0 /* undefined */
#define FG 1    /* running in foreground */
#define BG 2    /* running in background */
#define ST 3    /* stopped */

/* 
 * Jobs states: FG (foreground), BG (background), ST (stopped)
 * Job state transitions and enabling actions:
 *     FG -> ST  : ctrl-z
 *     ST -> FG  : fg command
 *     ST -> BG  : bg command
 *     BG -> FG  : fg command
 * At most 1 job can be in the FG state.
 */

/* Global variables */
extern char **environ;      /* defined in libc */
char prompt[] = "tsh> ";    /* command line prompt (DO NOT CHANGE) */
int verbose = 0;            /* if true, print additional output */
int nextjid = 1;            /* next job ID to allocate */
char sbuf[MAXLINE];         /* for composing sprintf messages */

struct job_t {              /* The job struct */
    pid_t pid;              /* job PID */
    int jid;                /* job ID [1, 2, ...] */
    int state;              /* UNDEF, BG, FG, or ST */
    char cmdline[MAXLINE];  /* command line */
};
struct job_t jobs[MAXJOBS]; /* The job list */
/* End global variables */


/* Function prototypes */

/* Here are the functions that you will implement */
void eval(char *cmdline);
int builtin_cmd(char **argv);
void do_bgfg(char **argv);
void waitfg(pid_t pid);

void sigchld_handler(int sig);
void sigtstp_handler(int sig);
void sigint_handler(int sig);

/* Here are helper routines that we've provided for you */
int parseline(const char *cmdline, char **argv); 
void sigquit_handler(int sig);

void clearjob(struct job_t *job);
void initjobs(struct job_t *jobs);
int maxjid(struct job_t *jobs);
int addjob(struct job_t *jobs, pid_t pid, int state, char *cmdline);
int deletejob(struct job_t *jobs, pid_t pid);
pid_t fgpid(struct job_t *jobs);
struct job_t *getjobpid(struct job_t *jobs, pid_t pid);
struct job_t *getjobjid(struct job_t *jobs, int jid);
int pid2jid(pid_t pid);
void listjobs(struct job_t *jobs);

void usage(void);
void unix_error(char *msg);
void app_error(char *msg);
typedef void handler_t(int);
handler_t *Signal(int signum, handler_t *handler);

// Additional functions that I have implemented
// in order to check the return value of EVERY system call (which is worth 5 points)
// (System call error handling)
pid_t safe_fork(void);
void safe_setpgid(pid_t pid, pid_t pgid);
void safe_kill(pid_t pid, int sig);

void safe_sigemptyset(sigset_t *set);
void safe_sigaddset(sigset_t *set, int signum);
void safe_sigprocmask(int how, const sigset_t *set, sigset_t *oldset);

/*
 * main - The shell's main routine 
 */
int main(int argc, char **argv) 
{
    char c;
    char cmdline[MAXLINE];
    int emit_prompt = 1; /* emit prompt (default) */

    /* Redirect stderr to stdout (so that driver will get all output
     * on the pipe connected to stdout) */
    dup2(1, 2);

    /* Parse the command line */
    while ((c = getopt(argc, argv, "hvp")) != EOF)
    {
        switch (c)
        {
            case 'h':             /* print help message */
                usage();
                break;
            case 'v':             /* emit additional diagnostic info */
                verbose = 1;
                break;
            case 'p':             /* don't print a prompt */
                emit_prompt = 0;  /* handy for automatic testing */
                break;
            default:
                usage();
	    }
    }

    /* Install the signal handlers */

    /* These are the ones you will need to implement */
    Signal(SIGINT,  sigint_handler);   /* ctrl-c */
    Signal(SIGTSTP, sigtstp_handler);  /* ctrl-z */
    Signal(SIGCHLD, sigchld_handler);  /* Terminated or stopped child */

    /* This one provides a clean way to kill the shell */
    Signal(SIGQUIT, sigquit_handler); 

    /* Initialize the job list */
    initjobs(jobs);

    /* Execute the shell's read/eval loop */
    while (1)
    {
        /* Read command line */
        if (emit_prompt) 
        {
            printf("%s", prompt);
            fflush(stdout);
        }
        if ((fgets(cmdline, MAXLINE, stdin) == NULL) && ferror(stdin))
            app_error("fgets error");
        if (feof(stdin)) 
        { 
            /* End of file (ctrl-d) */
            fflush(stdout);
            exit(0);
	    }

        /* Evaluate the command line */
        eval(cmdline);
        fflush(stdout);
        fflush(stdout);
    } 

    exit(0); /* control never reaches here */
}
  
/* 
 * eval - Evaluate the command line that the user has just typed in
 * 
 * If the user has requested a built-in command (quit, jobs, bg or fg)
 * then execute it immediately. Otherwise, fork a child process and
 * run the job in the context of the child. If the job is running in
 * the foreground, wait for it to terminate and then return.  Note:
 * each child process must have a unique process group ID so that our
 * background children don't receive SIGINT (SIGTSTP) from the kernel
 * when we type ctrl-c (ctrl-z) at the keyboard.  
*/
void eval(char *cmdline) 
{
    // In eval, the parent must use sigprocmask to block SIGCHLD signals before it forks the child,
    // and then unblock these signals, again using sigprocmask after it adds the child to the job 
    // list by calling addjob.

    // Since children inherit the blocked vectors of their parents, the child must be sure to then 
    // unblock SIGCHLD signals before it execs the new program.

    // your shell is running in the foreground process group.
    // If your shell then creates a child process, by default that child will also be a member of 
    // the foreground process group.

    // Since typing ctrl-c sends a SIGINT to every process in the foreground group, typing ctrl-c
    // will send a SIGINT to your shell, as well as to every process that your shell
    // created, which obviously isn’t correct.
    
    // Here is the workaround: After the fork, but before the execve, the child process should call
    // setpgid(0, 0), which puts the child in a new process group whose group ID is identical to 
    // the child’s PID.

    // This ensures that there will be only one process, your shell, in the foreground process group.
    // When you type ctrl-c, the shell should catch the resulting SIGINT and then forward it to the
    // appropriate foreground job (or more precisely, the process group that contains the foreground
    // job).
    
    // The parent needs to block the SIGCHLD signals in this way in order to avoid the race
    // condition where the child is reaped by sigchld handler (and thus removed from the job list)
    // before the parent calls addjob.
    
    
    char *argv[MAXARGS];    // Argument list execve()
    int bg;                 // Should the job run in bg or fg?
    pid_t pid;              // Process id
    sigset_t mask;          // Signal set to block certain signals
    
    bg = parseline(cmdline, argv);

    // Ignore empty lines
    if (argv[0] == NULL)
        return;             
    
    if (!builtin_cmd(argv)) 
    {
        // Blocking SIGCHILD
        safe_sigemptyset(&mask);                    // initialize signal set
        safe_sigaddset(&mask, SIGCHLD);             // addes SIGCHLD to the set
        safe_sigprocmask(SIG_BLOCK, &mask, NULL);   // Adds the signals in set to blocked
        
        // Child process
        if ((pid = safe_fork()) == 0)
        {
            // set child's group to a new process group (this is identical to the child's PID)
            safe_setpgid(0, 0);
            // Unblocks SIGCHLD signal
            safe_sigprocmask(SIG_UNBLOCK, &mask, NULL);
            
            if (execve(argv[0], argv, environ) < 0) 
            {
                printf("%s: Command not found\n", argv[0]);
                exit(0);
            }
        }
        
        // Parent process
        if (!bg) 
        {
            // Foreground
            addjob(jobs, pid, FG, cmdline);                 // Add this process to the job list
            safe_sigprocmask(SIG_UNBLOCK, &mask, NULL);     // Unblocks SIGCHLD signal
            waitfg(pid);                                    // Parent waits for foreground job to terminate
        }
        else 
        {
            // Background
            addjob(jobs, pid, BG, cmdline);                             // Add this process to the job list
            safe_sigprocmask(SIG_UNBLOCK, &mask, NULL);                 // Unblocks SIGCHLD signal
            printf("[%d] (%d) %s", pid2jid(pid), (int)pid, cmdline);    // Print background process info
        }
    }

    return;
}

/* 
 * parseline - Parse the command line and build the argv array.
 * 
 * Characters enclosed in single quotes are treated as a single
 * argument.  Return true if the user has requested a BG job, false if
 * the user has requested a FG job.  
 */
int parseline(const char *cmdline, char **argv) 
{
    static char array[MAXLINE]; /* holds local copy of command line */
    char *buf = array;          /* ptr that traverses command line */
    char *delim;                /* points to first space delimiter */
    int argc;                   /* number of args */
    int bg;                     /* background job? */

    strcpy(buf, cmdline);
    buf[strlen(buf)-1] = ' ';  /* replace trailing '\n' with space */
    while (*buf && (*buf == ' ')) /* ignore leading spaces */
	buf++;

    /* Build the argv list */
    argc = 0;
    if (*buf == '\'') 
    {
        buf++;
        delim = strchr(buf, '\'');
    }
    else 
    {
	    delim = strchr(buf, ' ');
    }

    while (delim) 
    {
        argv[argc++] = buf;
        *delim = '\0';
        buf = delim + 1;
        while (*buf && (*buf == ' ')) /* ignore spaces */
            buf++;

        if (*buf == '\'') 
        {
            buf++;
            delim = strchr(buf, '\'');
        }
        else 
        {
            delim = strchr(buf, ' ');
        }
    }
    argv[argc] = NULL;
    
    if (argc == 0)  /* ignore blank line */
	    return 1;

    /* should the job run in the background? */
    if ((bg = (*argv[argc-1] == '&')) != 0) 
    {
	    argv[--argc] = NULL;
    }
    return bg;
}

/* 
 * builtin_cmd - If the user has typed a built-in command then execute
 *    it immediately.  
 */
int builtin_cmd(char **argv) 
{   
    if (!strcmp(argv[0], "quit")) 
    {
        exit(0);
    }
    else if (!strcmp(argv[0], "jobs")) 
    {
        listjobs(jobs);
        return 1;
    }
    else if (!strcmp(argv[0], "bg"))
    {
        do_bgfg(argv);
        return 1;
    }
    else if (!strcmp(argv[0], "fg")) 
    {
        do_bgfg(argv);
        return 1;
    }
    
    return 0;     /* not a builtin command */
}

/* 
 * do_bgfg - Execute the builtin bg and fg commands
 */
void do_bgfg(char **argv) 
{
    // Checks if it has second argument
    if (argv[1] == NULL) 
    {
        printf("%s command requires PID or %%jobid argument\n", argv[0]);
        return;
    }
    // Checks if the second argument is valid
    if (!isdigit(argv[1][0]) && argv[1][0] != '%')
    {
        printf("%s: argument must be a PID or %%jobid\n", argv[0]);
        return;
    }
    
    // Checks if the second argument is refering to PID or JID
    int is_job_id = (argv[1][0] == '%' ? 1 : 0);
    struct job_t *tempjob;
    
    // if it is JID
    if (is_job_id)
    {
        // Get JID. pointer starts from the second character of second argument
        tempjob = getjobjid(jobs, atoi(&argv[1][1]));
        
        // Checks if the given JID is alive
        if (tempjob == NULL)
        {
            printf("%s: No such job\n", argv[1]);
            return;
        }
    }
    // if it is PID
    else
    {
        // Get PID with the second argument
        tempjob = getjobpid(jobs, (pid_t)atoi(argv[1]));

        // Checks if the given PID is there
        if (tempjob == NULL)
        {
            printf("(%d): No such process\n", atoi(argv[1]));
            return;
        }
    } 
    
    
    if(strcmp(argv[0], "bg") == 0)
    {
        // Change (FG > BG) or (ST -> BG)
        tempjob->state = BG;                                           
        printf("[%d] (%d) %s", tempjob->jid, tempjob->pid, tempjob->cmdline);

        // Send SIGCONT signal to entire group of the given job
        safe_kill(-tempjob->pid, SIGCONT);                              
    }
    else
    {
        // Change (BG -> FG) or (ST -> FG)
        tempjob->state = FG;

        // Send SIGCONT signal to entire group of the given job
        safe_kill(-tempjob->pid, SIGCONT);

        // Wait for fg job to finish
        waitfg(tempjob->pid);
    }

    return;
}

/* 
 * waitfg - Block until process pid is no longer the foreground process
 */
void waitfg(pid_t pid)
{
    // In waitfg, use a busy loop around the sleep function.
    while (1)
    {
        if (pid != fgpid(jobs))
        {
            if (verbose)
                printf("waitfg: Process (%d) no longer the fg process\n", (int) pid);
            break;
        }
        else 
        {
            sleep(1);
        }
    }
    
    return;
}

/*****************
 * Signal handlers
 *****************/

/* 
 * sigchld_handler - The kernel sends a SIGCHLD to the shell whenever
 *     a child job terminates (becomes a zombie), or stops because it
 *     received a SIGSTOP or SIGTSTP signal. The handler reaps all
 *     available zombie children, but doesn't wait for any other
 *     currently running children to terminate.  
 */
void sigchld_handler(int sig) 
{
    // In sigchld_handler, use exactly one call to wait pid.
    // Must reap all available zombie children.

    if (verbose)
        printf("sigchld_handler: entering\n");

    pid_t pid;
    int status;
    int jobid;
    
    // waitpid(pid_t pid, int *status, int options) funciton info
    // if pid > 0 : thne the wait set is the singleton child process whose process ID is equal to
    //              the given pid
    // if pid = -1 : then the wait set consists of all of the parent's child process
    // WNOHANG | WUNTRACED option :
    // Return immediately, with a return value of 0, if none of the children in the wait set has stopped or terminated,
    // or with a return value equal to the PID of one of the stopped or terminated children.
    
    while((pid = waitpid(-1, &status, WNOHANG | WUNTRACED)) > 0)
    {
        // Reap a zombie child
        jobid = pid2jid(pid);
        
        // Now checking the exit status of a reaped child
        
        // WIFEXITED returns true if the child terminated normally
        if (WIFEXITED(status))
        {
            // Delete the child from the job list
            deletejob(jobs, pid);
            if (verbose)
                printf("sigchld_handler: Job [%d] (%d) deleted\n", jobid, (int) pid);
            if (verbose)
                printf("sigchld_handler: Job [%d] (%d) terminates OK (status %d)\n", jobid, (int) pid, WEXITSTATUS(status));
        }
        
        // WIFSIGNALED returns true if the child process terminated because of a signal that was not caught
        // For example, SIGINT default behavior is terminate
        else if (WIFSIGNALED(status))
        {
            deletejob(jobs,pid);
            if (verbose)
                printf("sigchld_handler: Job [%d] (%d) deleted\n", jobid, (int) pid);
            printf("Job [%d] (%d) terminated by signal %d\n", jobid, (int) pid, WTERMSIG(status));
        }
        
        // WIFSTOPPED returns true if the child that cause the return is currently stopped.
        else if (WIFSTOPPED(status)) 
        {
            /*checks if child process that caused return is currently stopped */
            getjobpid(jobs, pid)->state = ST; // Change job status to ST (stopped)
            printf("Job [%d] (%d) stopped by signal %d\n", jobid, (int) pid, WSTOPSIG(status));
        }
        
    }
    
    if (verbose)
        printf("sigchld_handler: exiting\n");
    return;
}

/* 
 * sigint_handler - The kernel sends a SIGINT to the shell whenver the
 *    user types ctrl-c at the keyboard.  Catch it and send it along
 *    to the foreground job.  
 */
void sigint_handler(int sig) 
{
    // be sure to send SIGINT and SIGTSTP signals to the entire foreground process group, using 
    // ”-pid” instead of ”pid” in the argument to the kill function. The sdriver.pl program tests
    // for this error.

    if (verbose)
        printf("sigint_handler: entering\n");
    
    pid_t pid = fgpid(jobs);
    
    if (pid != 0)
    {
        // Sends SIGINT to every process in the same process group with pid
        safe_kill(-pid, sig);
        if (verbose)
            printf("sigint_handler: Job [%d] and its entire foreground jobs with same process group are killed\n", (int)pid);
    }
    
    if (verbose)
        printf("sigint_handler: exiting\n");
    return;
}

/*
 * sigtstp_handler - The kernel sends a SIGTSTP to the shell whenever
 *     the user types ctrl-z at the keyboard. Catch it and suspend the
 *     foreground job by sending it a SIGTSTP.  
 */
void sigtstp_handler(int sig) 
{
    // be sure to send SIGINT and SIGTSTP signals to the en- tire foreground process group, using
    // ”-pid” instead of ”pid” in the argument to the kill function. The sdriver.pl program tests
    // for this error.
    
    pid_t pid = fgpid(jobs);
    
    if (pid != 0) 
    {
        // Sends SIGTSTP to every process in the same process group with pid
        safe_kill(-pid, sig);
        if (verbose)
            printf("sigtstp_handler: Job [%d] and its entire foreground jobs with same process group are killed\n", (int)pid);
    }
    
    return;
}

/*********************
 * End signal handlers
 *********************/

/***********************************************
 * Helper routines that manipulate the job list
 **********************************************/

/* clearjob - Clear the entries in a job struct */
void clearjob(struct job_t *job) 
{
    job->pid = 0;
    job->jid = 0;
    job->state = UNDEF;
    job->cmdline[0] = '\0';
}

/* initjobs - Initialize the job list */
void initjobs(struct job_t *jobs) 
{
    int i;

    for (i = 0; i < MAXJOBS; i++)
	    clearjob(&jobs[i]);
}

/* maxjid - Returns largest allocated job ID */
int maxjid(struct job_t *jobs) 
{
    int i, max=0;

    for (i = 0; i < MAXJOBS; i++)
	    if (jobs[i].jid > max)
	        max = jobs[i].jid;
    return max;
}

/* addjob - Add a job to the job list */
int addjob(struct job_t *jobs, pid_t pid, int state, char *cmdline) 
{
    int i;
    
    if (pid < 1)
	    return 0;

    for (i = 0; i < MAXJOBS; i++) 
    {
	    if (jobs[i].pid == 0) 
        {
            jobs[i].pid = pid;
            jobs[i].state = state;
            jobs[i].jid = nextjid++;
            if (nextjid > MAXJOBS)
            nextjid = 1;
            strcpy(jobs[i].cmdline, cmdline);
            if(verbose){
                printf("Added job [%d] %d %s\n", jobs[i].jid, jobs[i].pid, jobs[i].cmdline);
            }
            return 1;
	    }
    }
    printf("Tried to create too many jobs\n");
    return 0;
}

/* deletejob - Delete a job whose PID=pid from the job list */
int deletejob(struct job_t *jobs, pid_t pid) 
{
    int i;

    if (pid < 1)
	return 0;

    for (i = 0; i < MAXJOBS; i++) 
    {
        if (jobs[i].pid == pid) 
        {
            clearjob(&jobs[i]);
            nextjid = maxjid(jobs)+1;
            return 1;
        }
    }
    return 0;
}

/* fgpid - Return PID of current foreground job, 0 if no such job */
pid_t fgpid(struct job_t *jobs) 
{
    int i;

    for (i = 0; i < MAXJOBS; i++)
	    if (jobs[i].state == FG)
	        return jobs[i].pid;
    return 0;
}

/* getjobpid  - Find a job (by PID) on the job list */
struct job_t *getjobpid(struct job_t *jobs, pid_t pid) 
{
    int i;

    if (pid < 1)
	    return NULL;
    for (i = 0; i < MAXJOBS; i++)
	    if (jobs[i].pid == pid)
	        return &jobs[i];
    return NULL;
}

/* getjobjid  - Find a job (by JID) on the job list */
struct job_t *getjobjid(struct job_t *jobs, int jid) 
{
    int i;

    if (jid < 1)
	return NULL;
    for (i = 0; i < MAXJOBS; i++)
	    if (jobs[i].jid == jid)
	        return &jobs[i];
    return NULL;
}

/* pid2jid - Map process ID to job ID */
int pid2jid(pid_t pid) 
{
    int i;

    if (pid < 1)
	return 0;
    for (i = 0; i < MAXJOBS; i++)
        if (jobs[i].pid == pid) 
        {
            return jobs[i].jid;
        }
    return 0;
}

/* listjobs - Print the job list */
void listjobs(struct job_t *jobs) 
{
    int i;
    
    for (i = 0; i < MAXJOBS; i++) 
    {
        if (jobs[i].pid != 0) 
        {
            printf("[%d] (%d) ", jobs[i].jid, jobs[i].pid);
            switch (jobs[i].state) 
            {
                case BG: 
                    printf("Running ");
                    break;
                case FG: 
                    printf("Foreground ");
                    break;
                case ST: 
                    printf("Stopped ");
                    break;
                default:
                    printf("listjobs: Internal error: job[%d].state=%d ", 
                    i, jobs[i].state);
            }
            printf("%s", jobs[i].cmdline);
        }
    }
}
/******************************
 * end job list helper routines
 ******************************/


/***********************
 * Other helper routines
 ***********************/

/*
 * usage - print a help message
 */
void usage(void) 
{
    printf("Usage: shell [-hvp]\n");
    printf("   -h   print this message\n");
    printf("   -v   print additional diagnostic information\n");
    printf("   -p   do not emit a command prompt\n");
    exit(1);
}

/*
 * unix_error - unix-style error routine
 */
void unix_error(char *msg)
{
    fprintf(stdout, "%s: %s\n", msg, strerror(errno));
    exit(1);
}

/*
 * app_error - application-style error routine
 */
void app_error(char *msg)
{
    fprintf(stdout, "%s\n", msg);
    exit(1);
}

/*
 * Signal - wrapper for the sigaction function
 */
handler_t *Signal(int signum, handler_t *handler) 
{
    struct sigaction action, old_action;

    action.sa_handler = handler;  
    sigemptyset(&action.sa_mask); /* block sigs of type being handled */
    action.sa_flags = SA_RESTART; /* restart syscalls if possible */

    if (sigaction(signum, &action, &old_action) < 0)
	unix_error("Signal error");
    return (old_action.sa_handler);
}

/*
 * sigquit_handler - The driver program can gracefully terminate the
 *    child shell by sending it a SIGQUIT signal.
 */
void sigquit_handler(int sig) 
{
    printf("Terminating after receipt of SIGQUIT signal\n");
    exit(1);
}


/**************************************
 * System call error handling functions
 **************************************/

pid_t safe_fork(void) 
{
    pid_t pid;
    if ((pid = fork()) < 0) 
    {
        unix_error("fork error");
    }
    return pid;
}

void safe_setpgid(pid_t pid, pid_t pgid) 
{
    if (setpgid(pid, pgid) < 0) 
    {
        unix_error("setpgid error");
    }
}
void safe_kill(pid_t pid, int sig) 
{
    if (kill(pid, sig) < 0) 
    {
        unix_error("kill error");
    }
}

void safe_sigemptyset(sigset_t *set) 
{
    if (sigemptyset(set) < 0) 
    {
        app_error("sigemptyset error");
    }
}
void safe_sigaddset(sigset_t *set, int signum) 
{
    if (sigaddset(set, signum) < 0) 
    {
        app_error("sigaddset error");
    }
}
void safe_sigprocmask(int how, const sigset_t *set, sigset_t *oldset) 
{
    if (sigprocmask(how, set, oldset) < 0) 
    {
        app_error("sigprocmask error");
    }
}
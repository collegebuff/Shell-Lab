// 
// tsh - A tiny shell program with job control
// 
// <Put your name and login ID here>
//

using namespace std;

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <string>

#include "globals.h"
#include "jobs.h"
#include "helper-routines.h"

//
// Needed global variable definitions
//

static char prompt[] = "tsh> ";
int verbose = 0;

//
// You need to implement the functions eval, builtin_cmd, do_bgfg,
// waitfg, sigchld_handler, sigstp_handler, sigint_handler
//
// The code below provides the "prototypes" for those functions
// so that earlier code can refer to them. You need to fill in the
// function bodies below.
// 

void eval(char *cmdline);
int builtin_cmd(char **argv);
void do_bgfg(char **argv);
void waitfg(pid_t pid);

void sigchld_handler(int sig);
void sigtstp_handler(int sig);
void sigint_handler(int sig);

//
// main - The shell's main routine 
//
int main(int argc, char **argv) 
{
  int emit_prompt = 1; // emit prompt (default)

  //
  // Redirect stderr to stdout (so that driver will get all output
  // on the pipe connected to stdout)
  //
  dup2(1, 2);

  /* Parse the command line */
  char c;
  while ((c = getopt(argc, argv, "hvp")) != EOF) {
    switch (c) {
    case 'h':             // print help message
      usage();
      break;
    case 'v':             // emit additional diagnostic info
      verbose = 1;
      break;
    case 'p':             // don't print a prompt
      emit_prompt = 0;  // handy for automatic testing
      break;
    default:
      usage();
    }
  }

  //
  // Install the signal handlers
  //

  //
  // These are the ones you will need to implement
  //
  Signal(SIGINT,  sigint_handler);   // ctrl-c
  Signal(SIGTSTP, sigtstp_handler);  // ctrl-z
  Signal(SIGCHLD, sigchld_handler);  // Terminated or stopped child

  //
  // This one provides a clean way to kill the shell
  //
  Signal(SIGQUIT, sigquit_handler); 

  //
  // Initialize the job list
  //
  initjobs(jobs);

  //
  // Execute the shell's read/eval loop
  //
  for(;;) {
    //
    // Read command line
    //
    if (emit_prompt) {
      printf("%s", prompt);
      fflush(stdout);
    }

    char cmdline[MAXLINE];

    if ((fgets(cmdline, MAXLINE, stdin) == NULL) && ferror(stdin)) {
      app_error("fgets error");
    }
    //
    // End of file? (did user type ctrl-d?)
    //
    if (feof(stdin)) {
      fflush(stdout);
      exit(0);
    }

    //
    // Evaluate command line
    //
    eval(cmdline);
    fflush(stdout);
    fflush(stdout);
  } 

  exit(0); //control never reaches here
}
  
/////////////////////////////////////////////////////////////////////////////
//
// eval - Evaluate the command line that the user has just typed in
// 
// If the user has requested a built-in command (quit, jobs, bg or fg)
// then execute it immediately. Otherwise, fork a child process and
// run the job in the context of the child. If the job is running in
// the foreground, wait for it to terminate and then return.  Note:
// each child process must have a unique process group ID so that our
// background children don't receive SIGINT (SIGTSTP) from the kernel
// when we type ctrl-c (ctrl-z) at the keyboard.
//
void eval(char *cmdline) 
{
  /* Parse command line */
  //
  // The 'argv' vector is filled in by the parseline
  // routine below. It provides the arguments needed
  // for the execve() routine, which you'll need to
  // use below to launch a process.
  //
  char *argv[MAXARGS];

  //
  // The 'bg' variable is TRUE if the job should run
  // in background mode or FALSE if it should run in FG
  //
    int bg;
    pid_t pid; //the process id variable
    sigset_t set; //declares a signal set variabe
    sigemptyset(&set);   //make set empty
    sigaddset(&set, SIGCHLD);  //the SIGCHLD signal is sent to the parent when the child is either terminated or stopped.
    bg = parseline(cmdline, argv); //parseline reads from the command line and sets argv (argv is passed to execve later)
    //parseline returns 0 if foreground and 1 if background.
    
    if (argv[0] == NULL){
      return;   /* ignore empty lines */
    }
    
    if(!builtin_cmd(argv)) { //if the first argument is not a buitin command, we will fork a child to run a file from the user
        sigprocmask(SIG_BLOCK, &set, NULL); //all the signals in set (the set in this case is the SIGCHLD signal) are blocked
        //this is so that you can reap the child later and the data wont be lost.
        pid = fork(); //fork a child
        /* In the parent, fork returns the PID of the child. In the child,
fork returns a value of 0.*/

        if (pid < 0) { //if the process id is less than zero there will be an error
            printf("fork(): forking error\n");
            exit(0);  //there was an error forking (the pid is a negative number) so exit the program
        }
        
        if (pid == 0) { //if the process id is that of the child
            setpgid(0,0); //sets group ID to the Patent's ID (this is used to address all elements under the parent).
            /*setpgid sets the group ID of the process specified by pid to pgid.  If
             pid is zero (like it is in this case), then the process ID of the calling process is used. If
             pgid is zero, then the PGID of the process specified by pid is made the
             same as its process ID.*/
            if (execvp(argv[0], argv) < 0) {        //executes the file pointed to by argv[0] with the parameters argv in the enviorn
                printf("%s: Command not found.\n", argv[0]); //execve does not return on success, so if it returns less than 0 there was a mistake
                exit(0); //exit the program
            }
        }
        
        else{
            if(!bg){
                addjob(jobs, pid ,FG, cmdline);
            }
            else{
                addjob(jobs, pid, BG, cmdline);
            }
            if(sigprocmask(SIG_UNBLOCK, &set, NULL)!= 0){
                unix_error("sigprocmask error");
            }
            
            if(!bg){
                waitfg(pid);
            }
            else{
                printf("%s", cmdline);
            }
        }
    }
}


/////////////////////////////////////////////////////////////////////////////
//
// builtin_cmd - If the user has typed a built-in command then execute
// it immediately. The command name would be in argv[0] and
// is a C string. We've cast this to a C++ string type to simplify
// string comparisons; however, the do_bgfg routine will need 
// to use the argv array as well to look for a job number.
//
int builtin_cmd(char **argv) 
{
    if(!strcmp(argv[0], "quit")){ /* quit command*/
        printf("Quit\n");
        exit(0);
    }
    
    if(!strcmp(argv[0], "jobs")){
        listjobs(jobs);
        return 1;
    }
    
    if(!strcmp(argv[0], "bg") || !strcmp(argv[0], "fg")){
        do_bgfg(argv);
        return 1;
    }
    
    if(!strcmp(argv[0], "&")) /* no singel*/
        return 1;
    
  return 0;     /* not a builtin command */
}

/////////////////////////////////////////////////////////////////////////////
//
// do_bgfg - Execute the builtin bg and fg commands
//
void do_bgfg(char **argv) 
{
  struct job_t *jobp=NULL;
    int jid;
    
  /* Ignore command if no argument */
  if (argv[1] == NULL) {
    printf("%s command requires PID or %%jobid argument\n", argv[0]);
    return;
  }
    
  /* Parse the required PID or %JID arg */
  if (isdigit(argv[1][0])) {
    pid_t pid = atoi(argv[1]); //atoi converts argv[1] into a string
    if (!(jobp = getjobpid(jobs, pid))) { //find the process id of the desired process and put it in jobp
      printf("(%d): No such process\n", pid);
      return;
    }
  }
  else if (argv[1][0] == '%') {
     jid = atoi(&argv[1][1]);
    if (!(jobp = getjobjid(jobs, jid))) {
      printf("%s: No such job\n", argv[1]);
      return;
    }
  }	    
  else {
    printf("%s: argument must be a PID or %%jobid\n", argv[0]);
    return;
  }

  //
  // You need to complete rest. At this point,
  // the variable 'jobp' is the job pointer
  // for the job ID specified as an argument.
  //
  // Your actions will depend on the specified command
  // so we've converted argv[0] to a string (cmd) for
  // your benefit.
  //
    if(kill(-(jobp->pid), SIGCONT)<0){
        if(errno != ESRCH){
            unix_error("kill error");
        }
    }
    
    if(!strcmp("fg", argv[0])){
        jobp->state = FG;
        waitfg(jobp->pid);
    }
    else if(!strcmp("bg", argv[0])){
        //printf("[%d] (%d) %s", jobp->jid, jobp->pid, jobp->cmdline);
        jobp->state = BG;
    }
    else {
        printf("bg/bf error: %s\n", argv[0]);
    }

  
}

/////////////////////////////////////////////////////////////////////////////
//
// waitfg - Block until process pid is no longer the foreground process
//
void waitfg(pid_t pid)
{
    while(pid == fgpid(jobs)){//while the process id is in the foreground
       sleep(1); //wait until the process is out of the foreground
    }
    
  return;
}

/////////////////////////////////////////////////////////////////////////////
//
// Signal handlers
//


/////////////////////////////////////////////////////////////////////////////
//
// sigchld_handler - The kernel sends a SIGCHLD to the shell whenever
//     a child job terminates (becomes a zombie), or stops because it
//     received a SIGSTOP or SIGTSTP signal. The handler reaps all
//     available zombie children, but doesn't wait for any other
//     currently running children to terminate.  
//
void sigchld_handler(int sig) 
{
    int status;
    pid_t pid; /* structure for pid to use */
    /* -1 is waiting for any child process */ /* WNOHANG: return immediately if no child process*/ /* return if a child has stopped */
    while((pid = waitpid(-1, &status, WNOHANG | WUNTRACED)) > 0){
        
        if(WIFEXITED(status)){ /* checks to see if child terminated normally*/
            deletejob(jobs, pid);
        }
        
        else if(WIFSIGNALED(status)){ /* checks if child was terminated by a signal that was not caught above*/
            printf("Job [%d] (%d) terminated by signal %d\n", pid2jid(pid),pid, WTERMSIG(status));
            sigint_handler(-2); /* this calls the sigint_handler*/
        }
        else if(WIFSTOPPED(status)){ /*checks if child process that caused return is then stopped as well*/
            sigtstp_handler(20);
            printf("[%d] Stopped %s\n", pid2jid(pid), jobs->cmdline);
        }
        
    }
    /* ECHILD does not exist or is not a child of the calling process.*/
    /* strerror(errno) gives the string of the error code*/
    if(pid < 0 && errno != ECHILD){
        printf("waitpid error: %s\n", strerror(errno));
    }
  return;
}

/////////////////////////////////////////////////////////////////////////////
//
// sigint_handler - The kernel sends a SIGINT to the shell whenver the
//    user types ctrl-c at the keyboard.  Catch it and send it along
//    to the foreground job.  
//
void sigint_handler(int sig) 
{
    int pid = fgpid(jobs); //set pid to PID
    int jid = pid2jid(pid); //jid is a map for pid
    
    // this checks for job or group of jobs if there are any continue
    if(pid != 0){
        
        kill(-pid, SIGINT); /*stops the foreground job / group*/
        /* checks to see if sig is greater than 0 then job is deleted*/
        if(sig > 0){
        printf("Job [%d] (%d) terminated by signal %d\n", jid, pid, sig);
            deletejob(jobs, pid);
        }
    }
    
  return;
}

/////////////////////////////////////////////////////////////////////////////
//
// sigtstp_handler - The kernel sends a SIGTSTP to the shell whenever
//     the user types ctrl-z at the keyboard. Catch it and suspend the
//     foreground job by sending it a SIGTSTP.  
//
void sigtstp_handler(int sig) 
{
    int pid = fgpid(jobs); //set pid to PID
    int jid = pid2jid(pid); //jid is a map for pid
    
    // this checks for job or group of jobs if there are any continue
    if(pid != 0){
        printf("Job [%d] (%d) Stopped by signal %d\n", jid, pid, sig);
        getjobpid(jobs, pid)->state=ST; /*finds job by pid and sets its state to stop*/
        kill(-pid, SIGTSTP); /*stops the foreground job / group*/
        /* -pid gives the process id of the group which is the parents id*/
    }
  return;
}

/*********************
 * End signal handlers
 *********************/





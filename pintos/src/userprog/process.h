#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

//Aggiunte
#include "threads/thread.h"
#include "stdlib.h"

tid_t process_execute (const char *file_name); //Modificata
int process_wait (tid_t); //Modificata
void process_exit (void); //Modificata
void process_activate (void);

/* Mi serve una struct per tenere traccia dei processi figlio
e dei loro valori di ritorno. */
struct child 
{
  tid_t id; //identificatore processo figlio
  int ret_val; //valore di ritorno
  int used; //intero per indicare se il processo figlio è ancora attivo o terminato
  struct list_elem elem; //lista di processi figlio
};


#endif /* userprog/process.h */


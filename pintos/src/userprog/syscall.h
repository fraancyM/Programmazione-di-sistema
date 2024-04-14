#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include <stdio.h>
#include "lib/kernel/list.h"

void syscall_init (void);

/* Il file system non è implementato in Pintos in modo concorrente
per cui mi serve un blocco per evitare che più thread accedano contemporaneamente
al file system e garantire l'accesso esclusivo. */
struct lock file_lock;

/* Struttura per tenere traccia dei file aperti da un processo 
e consentire operazioni di lettura e scrittura tramite file descriptor.
Mi serve per implementare la write, open, read, filesize...*/
struct file_desc
{
  struct file *fp; //puntatore a struct che rappresenza un file aperto
  int fd; //file descriptor associato al file aperto
  struct list_elem elem; //struttura per gestire elementi all'interno di una lista
};

#endif /* userprog/syscall.h */

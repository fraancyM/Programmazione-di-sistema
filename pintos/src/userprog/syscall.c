#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

//Aggiunte
#include "process.h"
#include "threads/vaddr.h"
#include "devices/shutdown.h"

static void syscall_handler (struct intr_frame *);

// Prototipi system calls
void halt(void);
int open(const char *file);
void exit(int status);
int write(int fd, const void *buffer, unsigned size);
void close(int fd);
bool create(const char *file, unsigned initial_size);
int read (int, void *, unsigned);
int filesize (int fd);

// Funzione per verificare se l'indirizzo è valido
bool check (void *addr);

// Funzione per file descriptor
struct file_desc *get_fd (int fd);

void
syscall_init (void)
{
  lock_init(&file_lock); //Inizializzazione per sincronizzare l'accesso a risorse condivise ed evitare race conditions.
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f)
{
  int *ptr = f->esp;
  if(check(ptr) == false)
    exit(-1);

  int syscall_number = *ptr;
  ASSERT(sizeof(syscall_number) == 4 ); // x86

  switch (syscall_number){

    case SYS_HALT:
      halt();
      break;

    case SYS_EXIT:
        if(check(ptr+1)==false)
            exit(-1);

        exit(*(ptr+1)); //exit ha 1 argomento --> ptr+1
        break;

    case SYS_WRITE:
        if (check(ptr+5)==false || check(ptr+6)==false ||
        check (ptr+7)==false || check(*(ptr+6))==false)
            exit(-1);
        f->eax = write(*(ptr+5),*(ptr+6),*(ptr+7)); //write ha 3 argomenti--> ptr+5,6,7
        break;

    case SYS_OPEN:
      if(!check (ptr+1) || !check(*(ptr+1)))
        exit(-1);
      f->eax = open (*(ptr+1));//open ha 1 argomento --> ptr+1
      break;

    case SYS_CLOSE:
      if (!check(ptr+1))
        exit(-1);
      close(*(ptr+1));
      break;

    case SYS_CREATE:
      if(!check(ptr+4) || !check(ptr+5) || !check(*(ptr+4)))
        exit(-1);
      f->eax = create (*(ptr+4), *(ptr+5));//write ha 2 argomenti--> ptr+4,5
      break;

     case SYS_READ:
      if (!check(ptr+5) || !check (ptr+6) || !check (ptr+7) || !check (*(ptr+6)))
        exit(-1);
      f->eax=read(*(ptr+5),*(ptr+6),*(ptr+7));//read ha 3 argomenti--> ptr+5,6,7
      break;

    case SYS_FILESIZE:
      if (!check(ptr+1))
        exit(-1);
      f->eax = filesize(*(ptr+1));//filesize ha 1 argomento --> ptr+1
      break;

    default:
      // Numero System Call invalido, exit(-1) del processo
      printf("Invalid System Call number\n");
      exit(-1);
      break;
  }
}

// Funzione per verificare se l'indirizzo è valido
bool check(void *addr) {

    if (is_user_vaddr(addr) == true &&
        pagedir_get_page(thread_current()->pagedir,addr)!=NULL)
        return true;
    else
        return false;
}

/* Implementazioni system calls */

/* Shutdown Pintos */
void halt (void){
  shutdown_power_off(); //Funzione già esistente in Pintos
}

/* Apre il file */
int open(const char * file)
{
  // finire di implementare
  lock_acquire(&file_lock);  // acquisco il lock
  struct file *file_p = filesys_open(file);   // apertura del file
  lock_release(&file_lock);  // rilascio del lock

  if(file_p == NULL)  // controllo se il file è stato aperto con successos
    return -1;

  struct file_desc * file_d = malloc (sizeof(struct file_desc)); // alloco dinamicamente una struttura dati che tiene traccia delle informazioni relative al file aperto
  file_d->fd = ++thread_current()->fd_count;  // viene assegnato un numero univoco (a ogni file aperto da questo thread)
  file_d->fp = file_p;  // assegno il puntatore del file
  list_push_front(&thread_current()->file_list, &file_d->elem);  // inserisco il file nella lista dei file aperti dal thread

  return file_d->fd;  // restituisco il numero univoco (descrittore)
}

/* Chiude il file descrittore fd */
void close (int fd)
{
  // Controllo se fd sia uguale a STDIN_FILENO o STDOUT_FILENO
  if (fd == STDIN_FILENO || fd == STDOUT_FILENO) // vorrei evitare di effettuare operazioni su stdin o stdout
    return;

  // Prendo il file equivalente a fd sotto forma di file_desc
  struct file_desc * fd_el = get_fd(fd);

  if (fd_el == NULL)	// se è nullo allora esco dalla funzione
    return -1;

  lock_acquire(&file_lock);	// Acquisco il lock
  file_close(fd_el->fp);	// Chiudo il file usando una sys function per i file
  lock_release(&file_lock);

  list_remove(&fd_el->elem);	// rimuovo il file dalla lista e quindi libero la memoria
  free(fd_el);

}

void exit (int status){

    /* Per gestire il processo di terminazione di un thread,
    devo memorizzare il valore di uscita, notificare il processo genitore (se in attesa),
    e terminare il thread corrente */

    /* Memorizzo il valore di uscita del thread che può essere recuperato dal processo genitore.*/
    struct thread *cur = thread_current ();
    cur->exit_code = status;
    //printf("%s: exit(%d)\n", cur->name, status);

    struct thread *parent_thread = cur->parent;
    struct child *children = get_child(cur->tid,parent_thread);

    if(children->id == cur->tid ){
        children->used = 1;
        children->ret_val = status; //Valore di ritorno del figlio

        /* Se il genitore sta aspettando il completamento del thread figlio,
        occorre svegliare il genitore con sema_up e notificare che il figlio ha terminato
        per consentirgli di continuare l'esecuzione.
        waiton_child è l'id del thread in attesa */
        if(parent_thread->waiton_child == cur->tid)
            sema_up(&parent_thread->child_sem);
    }

    //il thread ha completato la sua esecuzione, posso terminare e liberare le risorse associate al thread
    thread_exit();
}

int write (int fd, const void *buff, unsigned size){

    int num_bytes = -1; // Inizializzo il numero di byte scritti a -1

    lock_acquire(&file_lock); // Acquisisco il lock per garantire l'accesso esclusivo ai file.

    // STDOUT_FILENO‎ = 1 in lib/stdio.h
    if (fd == STDOUT_FILENO){ //Scrivo su standard output
        putbuf(buff, size); //Scrivo il contenuto del buffer sulla console
        num_bytes = size; // Imposto il numero di byte scritti come la dimensione del buffer
    }
    else {
        struct file_desc *f_desc = get_fd(fd);
        if(f_desc == NULL)
            num_bytes = -1; // Se il file descriptor non esiste, inizializzo di nuovo a -1
        else
            num_bytes = file_write (f_desc->fp, buff, size); //file_write in filesys/file.c
    }

    lock_release(&file_lock);

    return num_bytes;
}

struct file_desc *get_fd (int fd) {

    struct thread *thread_corrente = thread_current();
    struct list_elem *elemento_lista = list_begin(&thread_corrente->file_list);

    while (elemento_lista != list_end(&thread_corrente->file_list)) {
        struct file_desc *descrittore_file = list_entry(elemento_lista, struct file_desc, elem);

        // Verifico se l'fd del descrittore di file corrente corrisponde all'fd cercato.
        if (descrittore_file->fd == fd)
            return descrittore_file;

        elemento_lista = list_next(elemento_lista);
    }

    return NULL; // Restituisco NULL se l'fd non è stato trovato nella lista.
}

//crea un nuono file|non lo apre
bool create (const char * file, unsigned initial_size)
{
  if (file == NULL) //controlla se il nome del file è nullo
    return -1;//restituisce un valore non valido (-1) se il nome del file è nullo

  lock_acquire(&file_lock); //acquisisce il lock per evitare conflitti dovuti alla concorrenza
  int ret = filesys_create(file,initial_size); //Chiama la funzione di sistema filesys_create (filesys/filesys.c) per creare un nuovo file con il nome specificato e la dimensione iniziale specificata.
  lock_release(&file_lock); //rilascia il lock

  return ret; //restituisce il valore di ritorno di filesys_create -> booleano di successo
}

//legge il numero di byte da un file aperto in un buffer
int read (int fd, void * buffer, unsigned length)
{
  unsigned int len =0; //variabile per tenere traccia della lunghezza effettiva letta

  if (fd == STDIN_FILENO) //Se il file descriptor è stdin (standard input)
  {
    while (len < length) //Legge i byte da input_getc() fino a raggiungere la lunghezza specificata
    {
      *((char *)buffer+len) = input_getc();
      len++;
    }
    return len; //restituisce la lunghezza effettiva leta
  }


  struct file_desc * fd_elem = get_fd(fd); //se il fd non è stdin, ottiene l'elemnto del file descriptor

  if (fd_elem == NULL) //se l'elemento non è valido restituisce errore (-1)
    return -1;

  /*L'elemento è valido*/

  lock_acquire(&file_lock); //acquisisco il lock per evitare problematiche legate alla concorrenza
  len = file_read(fd_elem->fp,buffer,length);//chiama la funzione di sistema file_read (filesys/file.c) per leggere il file
  lock_release(&file_lock);//rilascia il lock

  return len; //restituisce la lunghezza effettiva letta dal file
}

//restiruisce la lunghezza del file
int filesize (int fd)
{
  struct file_desc * fd_elem = get_fd(fd);//prendo l'elemento della lista dei file corrispondente al descrittore fd


  if(fd_elem == NULL)//controllo che l'elemento non esiste
    return -1;//ritorno errore (-1)

  lock_acquire(&file_lock);//acquisisco il lock per evitare problematiche legate alla concorrenza
  int length = file_length(fd_elem->fp); //Ottengo la lunghezza del file con file_length (filesys/file.c)
  lock_release(&file_lock);//rilascio il lock
  return length;//ritorno la lunghezza del file
}

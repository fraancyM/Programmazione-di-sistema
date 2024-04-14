#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "userprog/gdt.h"
#include "userprog/pagedir.h"
#include "userprog/tss.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"

#include "userprog/syscall.h" //Aggiunto

static thread_func start_process NO_RETURN;
static bool load (const char *cmdline, void (**eip) (void), void **esp);

struct child *get_child(tid_t, struct thread *); //Aggiunta

/* Starts a new thread running a user program loaded from
   FILENAME.  The new thread may be scheduled (and may even exit)
   before process_execute() returns.  Returns the new process's
   thread id, or TID_ERROR if the thread cannot be created. */
tid_t
process_execute (const char *file_name)
{
  char *fn_copy;
  char *process_name; //Aggiunto
  tid_t tid;

  /* Make a copy of FILE_NAME.
     Otherwise there's a race between the caller and load(). */
  fn_copy = palloc_get_page (0);
  if (fn_copy == NULL)
    return TID_ERROR;
  strlcpy (fn_copy, file_name, PGSIZE);

    //-- Aggiunto
    /* Estraggo il nome del processo da parametri riga di comando
    e lo copio in process_name. */
    process_name=fn_copy+strlen(fn_copy)+1;
    strlcpy(process_name,file_name,strlen(file_name)+1); //Modificato
    char *save_ptr;
    process_name = strtok_r (process_name," ", &save_ptr);
    //-- fine

    /* Create a new thread to execute FILE_NAME. */
    tid = thread_create (process_name, PRI_DEFAULT, start_process, fn_copy);
    if (tid == TID_ERROR) //If thread_create fails, free page and return TID_ERROR
        palloc_free_page (fn_copy);
    return tid;

    //-- Aggiunto
	  /* Gestione della sincronizzazione tra un thread padre e il suo thread figlio */
	  /* Utilizzazo il semaforo per sincronizzare il padre e il figlio
	  durante il caricamento dell'eseguibile.
	  Se il thread padre raggiunge questa riga prima che il thread figlio
	  incrementi il valore del semaforo (inizialmente 0), il thread
	  padre si blocchera in attesa del caricamento da parte del thread figlio. */
	sema_down(&thread_current()->production_sem);

    /*Verifico se il caricamento non è stato completato con successo
    tramite flag e ritorno errore in caso di caricamento fallito. */
    if (thread_current()->production_flag == false) {
        struct child *child = get_child(tid,thread_current());
    if (child!= NULL) {
        list_remove(&child->elem); //rimuovo l'elemento dalla lista dei figli del thread corrente
        free(child);
    }
    return TID_ERROR;
  }
  return tid;
  //-- fine
}

/* A thread function that loads a user process and starts it
   running. */
static void
start_process (void *file_name_)
{
  char *file_name = file_name_;
  struct intr_frame if_;
  bool success;

  /* Initialize interrupt frame and load executable. */
  memset (&if_, 0, sizeof if_);
  if_.gs = if_.fs = if_.es = if_.ds = if_.ss = SEL_UDSEG;
  if_.cs = SEL_UCSEG;
  if_.eflags = FLAG_IF | FLAG_MBS;
  success = load (file_name, &if_.eip, &if_.esp);

  /* If load failed, quit. */
  palloc_free_page (file_name);
  if (!success)
    thread_exit ();

  /* Start the user process by simulating a return from an
     interrupt, implemented by intr_exit (in
     threads/intr-stubs.S).  Because intr_exit takes all of its
     arguments on the stack in the form of a `struct intr_frame',
     we just point the stack pointer (%esp) to our stack frame
     and jump to it. */
  asm volatile ("movl %0, %%esp; jmp intr_exit" : : "g" (&if_) : "memory");
  NOT_REACHED ();
}

/* Waits for thread TID to die and returns its exit status.  If
   it was terminated by the kernel (i.e. killed due to an
   exception), returns -1.  If TID is invalid or if it was not a
   child of the calling process, or if process_wait() has already
   been successfully called for the given TID, returns -1
   immediately, without waiting.
*/

//Implementata
/* Questa funzione si deve occupare di aspettare la terminazione di un processo
figlio sspecificato da child_tid. Se il processo figlio è ancora attivo,
il padre si blocca fino a quando il figlio non termina.
Una volta che il figlio termina, il padre deve recuperare il valore di ritorno
del figlio, rimuovere il figlio dalla lista dei figli e restituire il valore
di ritorno del figlio. In caso di errore (il padre non ha figli o il
figlio specificato non esiste) ritorno -1. */
int
process_wait (tid_t child_tid UNUSED)
{
  /* Verifico se il thread corrente non ha figli vivi. Se la lista
  è vuota, non ci sono figli attivi e ritorno -1. */
  if(list_empty(&thread_current()->children))
    return -1;

  /* Verifico se è stato trovato un puntatore valido per il thred figlio.
  Se non viene trovato ritorno un errore. */
  struct child *child = get_child(child_tid, thread_current());
  if(child == NULL){
    return -1;
  }

  /* Devo tenere trcci del thred figlio per cui il thred corrente sta aspettando. */
  thread_current()->waiton_child = child_tid;

  /* Se il thread figlio è ancora attivo (valore divero da 1)e quindi
  non ha ancora aggiornato la struttura child del genitore, il thread
  corrente i blocca utilizzando il semaforo associato al thread corrente, in
  modo da aspettare la terminazione del figlio.  */
    if (child->used == 1) {
        ASSERT (child->used == 1); //Genero un errore
    } else {
        sema_down(&thread_current()->child_sem);
    }

  int ret_child = child->ret_val; //Recupero il valore di ritorno del figlio struct child

  /* Rimuovo l'elemento dll lista dei figli del thread corrente e
  dealloco la memoria. */
  list_remove(&child->elem);
  free(child);

  return ret_child;
}

/* Free the current process's resources. */
void
process_exit (void)
{
  struct thread *cur = thread_current ();
  uint32_t *pd;

  //Stampo un messaggio di uscita
  printf("%s: exit(%d)\n",cur->name,cur->exit_code);

   /* Acquisisco il lock per garantire l'accesso esclusivo alla
  risorsa condivisa. Se il puntatore al file associato al thread
  corrente non è nullo, allora il thread corrente ha aperto un
  file eseguibile, per cui lo chiudo per rendere il file
  modificabile. */
  lock_acquire(&file_lock);
  if (thread_current()->file != NULL)
    file_close(thread_current()->file);
  lock_release(&file_lock);

  /* Scorro la lista dei figli del thread corrente e dealloco la memoria
  associata a ciascun figlio. */
	while(!list_empty(&thread_current()->children)){
        struct list_elem *e = list_pop_front(&thread_current()->children);
        struct child *child = list_entry(e,struct child,elem);
        list_remove(e);
        free(child);
  }

  /* Faccio la stessa cosa per chiudere i file aperti. */
	while(!list_empty(&thread_current()->file_list)){
        struct list_elem *e = list_pop_front(&thread_current()->file_list);
        struct file_desc *fd_element = list_entry(e,struct file_desc,elem);
        lock_acquire(&file_lock);
        file_close(fd_element->fp);
        lock_release(&file_lock);

        list_remove(e);
        free(fd_element);
  }

/* Genero errore in caso di liste non vuote. */
  ASSERT(list_empty(&thread_current()->children));
  ASSERT(list_empty(&thread_current()->file_list));


  /* Destroy the current process's page directory and switch back
     to the kernel-only page directory. */
  pd = cur->pagedir;
  if (pd != NULL)
    {
      /* Correct ordering here is crucial.  We must set
         cur->pagedir to NULL before switching page directories,
         so that a timer interrupt can't switch back to the
         process page directory.  We must activate the base page
         directory before destroying the process's page
         directory, or our active page directory will be one
         that's been freed (and cleared). */
      cur->pagedir = NULL;
      pagedir_activate (NULL);
      pagedir_destroy (pd);
    }
}

/* Sets up the CPU for running user code in the current
   thread.
   This function is called on every context switch. */
void
process_activate (void)
{
  struct thread *t = thread_current ();

  /* Activate thread's page tables. */
  pagedir_activate (t->pagedir);

  /* Set thread's kernel stack for use in processing
     interrupts. */
  tss_update ();
}

/* We load ELF binaries.  The following definitions are taken
   from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
typedef uint32_t Elf32_Word, Elf32_Addr, Elf32_Off;
typedef uint16_t Elf32_Half;

/* For use with ELF types in printf(). */
#define PE32Wx PRIx32   /* Print Elf32_Word in hexadecimal. */
#define PE32Ax PRIx32   /* Print Elf32_Addr in hexadecimal. */
#define PE32Ox PRIx32   /* Print Elf32_Off in hexadecimal. */
#define PE32Hx PRIx16   /* Print Elf32_Half in hexadecimal. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
   This appears at the very beginning of an ELF binary. */
struct Elf32_Ehdr
  {
    unsigned char e_ident[16];
    Elf32_Half    e_type;
    Elf32_Half    e_machine;
    Elf32_Word    e_version;
    Elf32_Addr    e_entry;
    Elf32_Off     e_phoff;
    Elf32_Off     e_shoff;
    Elf32_Word    e_flags;
    Elf32_Half    e_ehsize;
    Elf32_Half    e_phentsize;
    Elf32_Half    e_phnum;
    Elf32_Half    e_shentsize;
    Elf32_Half    e_shnum;
    Elf32_Half    e_shstrndx;
  };

/* Program header.  See [ELF1] 2-2 to 2-4.
   There are e_phnum of these, starting at file offset e_phoff
   (see [ELF1] 1-6). */
struct Elf32_Phdr
  {
    Elf32_Word p_type;
    Elf32_Off  p_offset;
    Elf32_Addr p_vaddr;
    Elf32_Addr p_paddr;
    Elf32_Word p_filesz;
    Elf32_Word p_memsz;
    Elf32_Word p_flags;
    Elf32_Word p_align;
  };

/* Values for p_type.  See [ELF1] 2-3. */
#define PT_NULL    0            /* Ignore. */
#define PT_LOAD    1            /* Loadable segment. */
#define PT_DYNAMIC 2            /* Dynamic linking info. */
#define PT_INTERP  3            /* Name of dynamic loader. */
#define PT_NOTE    4            /* Auxiliary info. */
#define PT_SHLIB   5            /* Reserved. */
#define PT_PHDR    6            /* Program header table. */
#define PT_STACK   0x6474e551   /* Stack segment. */

/* Flags for p_flags.  See [ELF3] 2-3 and 2-4. */
#define PF_X 1          /* Executable. */
#define PF_W 2          /* Writable. */
#define PF_R 4          /* Readable. */

static bool setup_stack (void **esp, char * cmdline);
static bool validate_segment (const struct Elf32_Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
                          uint32_t read_bytes, uint32_t zero_bytes,
                          bool writable);

/* Loads an ELF executable from FILE_NAME into the current thread.
   Stores the executable's entry point into *EIP
   and its initial stack pointer into *ESP.
   Returns true if successful, false otherwise. */
bool
load (const char *file_name, void (**eip) (void), void **esp)
{
  struct thread *t = thread_current ();
  struct Elf32_Ehdr ehdr;
  struct file *file = NULL;
  off_t file_ofs;
  bool success = false;
  int i;

  /* Allocate and activate page directory. */
  t->pagedir = pagedir_create ();
  if (t->pagedir == NULL)
    goto done;
  process_activate ();

  /* Open executable file. */
  file = filesys_open (file_name);
  if (file == NULL)
    {
      printf ("load: %s: open failed\n", file_name);
      goto done;
    }
  /* Read and verify executable header. */
  if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
      || memcmp (ehdr.e_ident, "\177ELF\1\1\1", 7)
      || ehdr.e_type != 2
      || ehdr.e_machine != 3
      || ehdr.e_version != 1
      || ehdr.e_phentsize != sizeof (struct Elf32_Phdr)
      || ehdr.e_phnum > 1024)
    {
      printf ("load: %s: error loading executable\n", file_name);
      goto done;
    }
  /* Read program headers. */
  file_ofs = ehdr.e_phoff;
  for (i = 0; i < ehdr.e_phnum; i++)
    {
      struct Elf32_Phdr phdr;

      if (file_ofs < 0 || file_ofs > file_length (file))
        goto done;
      file_seek (file, file_ofs);

      if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
        goto done;
      file_ofs += sizeof phdr;
      switch (phdr.p_type)
        {
        case PT_NULL:
        case PT_NOTE:
        case PT_PHDR:
        case PT_STACK:
        default:
          /* Ignore this segment. */
          break;
        case PT_DYNAMIC:
        case PT_INTERP:
        case PT_SHLIB:
          goto done;
        case PT_LOAD:
          if (validate_segment (&phdr, file))
            {
              bool writable = (phdr.p_flags & PF_W) != 0;
              uint32_t file_page = phdr.p_offset & ~PGMASK;
              uint32_t mem_page = phdr.p_vaddr & ~PGMASK;
              uint32_t page_offset = phdr.p_vaddr & PGMASK;
              uint32_t read_bytes, zero_bytes;
              if (phdr.p_filesz > 0)
                {
                  /* Normal segment.
                     Read initial part from disk and zero the rest. */
                  read_bytes = page_offset + phdr.p_filesz;
                  zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
                                - read_bytes);
                }
              else
                {
                  /* Entirely zero.
                     Don't read anything from disk. */
                  read_bytes = 0;
                  zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
                }
              if (!load_segment (file, file_page, (void *) mem_page,
                                 read_bytes, zero_bytes, writable))
                goto done;
            }
          else
            goto done;
          break;
        }
    }

  /* Set up stack. */
  if (!setup_stack (esp,file_name))
    goto done;

  /* Start address. */
  *eip = (void (*) (void)) ehdr.e_entry;

  success = true;

 done:
 /* We arrive here whether the load is successful or not. */
    file_close (file);
  return success;
}

/* load() helpers. */

static bool install_page (void *upage, void *kpage, bool writable);

/* Checks whether PHDR describes a valid, loadable segment in
   FILE and returns true if so, false otherwise. */
static bool
validate_segment (const struct Elf32_Phdr *phdr, struct file *file)
{
  /* p_offset and p_vaddr must have the same page offset. */
  if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK))
    return false;

  /* p_offset must point within FILE. */
  if (phdr->p_offset > (Elf32_Off) file_length (file))
    return false;

  /* p_memsz must be at least as big as p_filesz. */
  if (phdr->p_memsz < phdr->p_filesz)
    return false;

  /* The segment must not be empty. */
  if (phdr->p_memsz == 0)
    return false;

  /* The virtual memory region must both start and end within the
     user address space range. */
  if (!is_user_vaddr ((void *) phdr->p_vaddr))
    return false;
  if (!is_user_vaddr ((void *) (phdr->p_vaddr + phdr->p_memsz)))
    return false;

  /* The region cannot "wrap around" across the kernel virtual
     address space. */
  if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
    return false;

  /* Disallow mapping page 0.
     Not only is it a bad idea to map page 0, but if we allowed
     it then user code that passed a null pointer to system calls
     could quite likely panic the kernel by way of null pointer
     assertions in memcpy(), etc. */
  if (phdr->p_vaddr < PGSIZE)
    return false;

  /* It's okay. */
  return true;
}

/* Loads a segment starting at offset OFS in FILE at address
   UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
   memory are initialized, as follows:

        - READ_BYTES bytes at UPAGE must be read from FILE
          starting at offset OFS.

        - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.

   The pages initialized by this function must be writable by the
   user process if WRITABLE is true, read-only otherwise.

   Return true if successful, false if a memory allocation error
   or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
              uint32_t read_bytes, uint32_t zero_bytes, bool writable)
{
  ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
  ASSERT (pg_ofs (upage) == 0);
  ASSERT (ofs % PGSIZE == 0);

  file_seek (file, ofs);
  while (read_bytes > 0 || zero_bytes > 0)
    {
      /* Calculate how to fill this page.
         We will read PAGE_READ_BYTES bytes from FILE
         and zero the final PAGE_ZERO_BYTES bytes. */
      size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
      size_t page_zero_bytes = PGSIZE - page_read_bytes;

      /* Get a page of memory. */
      uint8_t *kpage = palloc_get_page (PAL_USER);
      if (kpage == NULL)
        return false;

      /* Load this page. */
      if (file_read (file, kpage, page_read_bytes) != (int) page_read_bytes)
        {
          palloc_free_page (kpage);
          return false;
        }
      memset (kpage + page_read_bytes, 0, page_zero_bytes);

      /* Add the page to the process's address space. */
      if (!install_page (upage, kpage, writable))
        {
          palloc_free_page (kpage);
          return false;
        }

      /* Advance. */
      read_bytes -= page_read_bytes;
      zero_bytes -= page_zero_bytes;
      upage += PGSIZE;
    }
  return true;
}

/* Create a minimal stack by mapping a zeroed page at the top of
   user virtual memory. */
static bool
setup_stack (void **esp, char *file_name)
{
  uint8_t *kpage;
  bool success = false;

  kpage = palloc_get_page (PAL_USER | PAL_ZERO);
  if (kpage != NULL)
    {
      success = install_page (((uint8_t *) PHYS_BASE) - PGSIZE, kpage, true);
      if (success)
        *esp = PHYS_BASE;
      else
        palloc_free_page (kpage);
    }

   //Aggiunto
  /* Gestione della linea di comando */
  char *current_token; //Puntatore per la porzione della stringa file_name
  char *save_ptr;
  int argv_size = 2; //Dimensione iniziale del vettore argv
  int argc_count = 0; //Contatore numero di argomenti riga di comando
  char **argv = malloc (argv_size * sizeof(char *));

  for (current_token = strtok_r (file_name, " ", &save_ptr); current_token!= NULL; current_token = strtok_r (NULL, " ", &save_ptr)){
    *esp -= strlen(current_token) + 1;
    argv[argc_count] = *esp;
    argc_count++;

    /* Verifico se il numero di argomenti supera il limite massimo consentito (64).
    In quel caso la memoria allocata di argv viene liberata. */
    if (argc_count >= 64){
      free(argv);
      return false;
    }

    if (argc_count >= argv_size){
      argv_size *= 2; // Raddoppio della dimensione di argv_size per riallocare
      argv = realloc(argv,argv_size* sizeof(char *));
    }

    memcpy(*esp,current_token,strlen(current_token) + 1);

  }
  argv[argc_count] = 0;

  int i = 0;
  for (i = argc_count; i >= 0; i--){
    *esp -= sizeof(char*);
    memcpy(*esp,&argv[i],sizeof(char*));
  }

  //argv
  current_token = *esp;
  *esp-=sizeof(char**);
  memcpy(*esp,&current_token,sizeof(char**));

  //argc
  *esp -= sizeof(int);
  memcpy(*esp,&argc_count,sizeof(int));

  // Pushing fake return address
  *esp -= sizeof(void*);
  memcpy(*esp, &argv[argc_count],sizeof(void *));
  free(argv);

  return success;
}

/* Adds a mapping from user virtual address UPAGE to kernel
   virtual address KPAGE to the page table.
   If WRITABLE is true, the user process may modify the page;
   otherwise, it is read-only.
   UPAGE must not already be mapped.
   KPAGE should probably be a page obtained from the user pool
   with palloc_get_page().
   Returns true on success, false if UPAGE is already mapped or
   if memory allocation fails. */
static bool
install_page (void *upage, void *kpage, bool writable)
{
  struct thread *t = thread_current ();

  /* Verify that there's not already a page at that virtual
     address, then map our page there. */
  return (pagedir_get_page (t->pagedir, upage) == NULL
          && pagedir_set_page (t->pagedir, upage, kpage, writable));
}

//Aggiunta
/* Cerco un elemento nella lista dei figli del thread in base
all'identificatore id5. */
struct child *get_child(tid_t id, struct thread *curr){
  struct list_elem *e;
  for (e=list_begin(&curr->children); e!=list_end(&curr->children); e=list_next(e)) {
    struct child *child = list_entry(e,struct child,elem);
    if(child->id == id)
      return child;
  }
  return NULL;
}

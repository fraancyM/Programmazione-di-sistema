/* Writes data spanning two pages in virtual address space,
   which must succeed. */

#include <string.h>
#include <syscall.h>
#include "tests/userprog/boundary.h"
#include "tests/userprog/sample.inc"
#include "tests/lib.h"
#include "tests/main.h"

void
test_main (void) 
{
  int handle;
  int byte_cnt;
  char *sample_p;

  // Ottiene un puntatore a una stringa che attraversa il confine di pagina virtuale
  sample_p = copy_string_across_boundary (sample);

  // Apre un file chiamato "sample.txt" per la scrittura
  CHECK ((handle = open ("sample.txt")) > 1, "open \"sample.txt\"");

  // Scrive nel file utilizzando la syscall write
  byte_cnt = write (handle, sample_p, sizeof sample - 1);
  // Verifica se la syscall write ha scritto il numero corretto di byte
  if (byte_cnt != sizeof sample - 1)
    fail ("write() returned %d instead of %zu", byte_cnt, sizeof sample - 1);
}

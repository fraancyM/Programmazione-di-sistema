/* Try a 0-byte write, which should return 0 without writing
   anything. */

#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

void
test_main (void) 
{
  int handle, byte_cnt;
  char buf;

  // Apre un file chiamato "sample.txt" per la scrittura
  CHECK ((handle = open ("sample.txt")) > 1, "open \"sample.txt\"");

  // Inizializza un carattere `buf` con il valore 123
  buf = 123;

  // Prova a scrivere 0 byte nel file utilizzando la syscall write
  byte_cnt = write (handle, &buf, 0);

  // Verifica se la syscall write ha restituito 0, come previsto
  if (byte_cnt != 0)
    fail("write() returned %d instead of 0", byte_cnt);
}

/* Try writing a file in the most normal way. */

#include <syscall.h>
#include "tests/userprog/sample.inc"
#include "tests/lib.h"
#include "tests/main.h"

void
test_main (void) 
{
  int handle, byte_cnt;

  // Crea un file chiamato "test.txt" con i contenuti di "sample"
  CHECK (create ("test.txt", sizeof sample - 1), "create \"test.txt\"");
  // Apre il file "test.txt" per la scrittura
  CHECK ((handle = open ("test.txt")) > 1, "open \"test.txt\"");
  
  // Scrive nel file utilizzando la syscall write
  byte_cnt = write (handle, sample, sizeof sample - 1);
  // Verifica se la syscall write ha scritto il numero corretto di byte
  if (byte_cnt != sizeof sample - 1)
    fail ("write() returned %d instead of %zu", byte_cnt, sizeof sample - 1);
}


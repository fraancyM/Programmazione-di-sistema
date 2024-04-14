/* Tests the halt system call. */

#include "tests/lib.h"
#include "tests/main.h"

void
test_main (void) 
{
  halt ();
  
  //Questo codice non dovrebbe essere mai eseguito se funziona correttamente la sys_halt
  fail ("should have halted");
}

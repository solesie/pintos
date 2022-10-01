/* fibonacci.c

   Creates a directory. */

#include <stdio.h>
#include <syscall.h>
#include <stdlib.h>

int
main (int argc, char** argv) 
{
  if (argc != 2) 
    {
      printf ("usage: %s number\n", *(argv));
      return EXIT_FAILURE;
    }
  printf("%d\n",fibonacci(atoi(*(argv + 1))));
  
  return EXIT_SUCCESS;
}

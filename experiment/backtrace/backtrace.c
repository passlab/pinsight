#define _GNU_SOURCE

#include <execinfo.h>
#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>

/* Obtain a backtrace and print it to stdout. */
void
print_trace (void)
{
  void *array[10];
  char **strings;
  int size, i;

  size = backtrace (array, 10);
  strings = backtrace_symbols (array, size);
  if (strings != NULL)
  {

    printf ("Obtained %d stack frames.\n", size);
    for (i = 0; i < size; i++)
      printf ("0x%p: %s\n", array[i], strings[i]);
  }

  printf("------------------Dl_info --------------\n");
  void * rt = __builtin_return_address(0);
  Dl_info dlinfo;
  dladdr(rt, &dlinfo);
  printf("0x%p: offset: %p\n", rt, (void*)((unsigned long int) rt - (unsigned long int)dlinfo.dli_fbase));
  printf("Dl_info: filename: %s, base address: %p, symbol: %s, symbol address: %p\n", 
		  dlinfo.dli_fname, dlinfo.dli_fbase, dlinfo.dli_sname, dlinfo.dli_saddr);

  free (strings);
}

/* A dummy function to make the backtrace more interesting. */
void
dummy_function (void)
{
  print_trace ();
}

int
main (void)
{
  dummy_function ();
  return 0;
}

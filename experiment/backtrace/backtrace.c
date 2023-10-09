#define _GNU_SOURCE

#include <execinfo.h>
#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>
#define UNW_LOCAL_ONLY
#include <libunwind.h>

/* Obtain a backtrace and print it to stdout. */
void
print_trace (void)
{
  void *array[10];
  char **strings;
  int size, i;

  size = backtrace (array, 10);
  strings = backtrace_symbols (array, size);
  Dl_info dlinfo;
  if (strings != NULL)
  {

    printf ("Obtained %d stack frames.\n", size);
    for (i = 0; i < size; i++) {
      printf ("%s\n", strings[i]);
//      dladdr(array[i], &dlinfo);
//      void * offset = (void*)((unsigned long int) array[i] - (unsigned long int)dlinfo.dli_fbase);
      //printf("0x%p: offset: %p\n", array[i], offset);
      //printf("Dl_info: filename: %s, base address: %p, symbol: %s, symbol address: %p\n", 
      //		  dlinfo.dli_fname, dlinfo.dli_fbase, dlinfo.dli_sname, dlinfo.dli_saddr);

//      printf("%s(+%p) [%p]\n", dlinfo.dli_fname, offset, array[i]);
    }
  }

//  printf("------------------Dl_info for __builtin_return_address (0) --------------\n");
//  void * rt = __builtin_return_address(0);
//  dladdr(rt, &dlinfo);
//  printf("0x%p: offset: %p\n", rt, (void*)((unsigned long int) rt - (unsigned long int)dlinfo.dli_fbase));
//  printf("Dl_info: filename: %s, base address: %p, symbol: %s, symbol address: %p\n", 
//		  dlinfo.dli_fname, dlinfo.dli_fbase, dlinfo.dli_sname, dlinfo.dli_saddr);

  free (strings);

  printf("---------------------------\n");

  unw_cursor_t cursor; unw_context_t uc;
  unw_word_t ip, sp;

  unw_getcontext(&uc);
  unw_init_local(&cursor, &uc);
  unw_get_reg(&cursor, UNW_REG_IP, &ip);
  unw_get_reg(&cursor, UNW_REG_SP, &sp);
  printf ("ip = %lx, sp = %lx\n", (long) ip, (long) sp);
  while (unw_step(&cursor) > 0) {
    unw_get_reg(&cursor, UNW_REG_IP, &ip);
    unw_get_reg(&cursor, UNW_REG_SP, &sp);
    printf ("ip = %lx, sp = %lx\n", (long) ip, (long) sp);
  }
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
  print_trace();
  dummy_function ();
  return 0;
}

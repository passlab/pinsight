/**
 * For loading base address of the application, the base address is used to calculate the
 * codeptr offset for addr2line
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <dlfcn.h>

unsigned long int find_load_baseaddr(const void * codeptr) {
  Dl_info dlinfo;
  dladdr(codeptr, &dlinfo);

  printf("Based on address %p (offset: %p), the app info are: \n", codeptr, (void*)((unsigned long int) codeptr - (unsigned long int)dlinfo.dli_fbase));
  printf("\tDl_info: filename: %s, base address: %p, symbol: %s, symbol address: %p\n",
                  dlinfo.dli_fname, dlinfo.dli_fbase, dlinfo.dli_sname, dlinfo.dli_saddr);
  return (unsigned long int) dlinfo.dli_fbase;
}

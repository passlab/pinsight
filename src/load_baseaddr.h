
/** the codeptr_ra from OMPT and __builtin_return_address are runtime
 * memory address of the text segment. To convert those addresses to their
 * relative offset address that can be used by addr2line to retrieve source code
 * line number, we need to the base memory address where the app is loaded in
 * memory. With this base address, relative offset can be calculated by codeptr - load_baseaddr
 *
 * The load_baseaddr is retrieved using Dl_info, see below
#define _GNU_SOURCE
#include <dlfcn.h>

void * rt = __builtin_return_address(0); //or codeptr_ra
Dl_info dlinfo;
dladdr(rt, &dlinfo);
printf("0x%p: offset: %p\n", rt, (void*)((unsigned long int) rt - (unsigned long int)dlinfo.dli_fbase));
printf("Dl_info: filename: %s, base address: %p, symbol: %s, symbol address: %p\n",
                  dlinfo.dli_fname, dlinfo.dli_fbase, dlinfo.dli_sname, dlinfo.dli_saddr);

The above call will be put in OMPT/PMPI/CUPTI callbacks, but we only need to do it once. macro is used to guard this.
By default, it will be called by OMPT, if OMPT is not enabled, MPI and CUPTI will call it.
 */
extern unsigned long int load_baseaddr;

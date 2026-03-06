Approach 1: Flag-based (recommended)
Signal handlers have strict restrictions — only async-signal-safe functions are allowed (stat() is safe, but fprintf, malloc, fopen used in parsing are not). So the handler should only set a flag, and the actual re-read happens at the next safe point:

c
// trace_config.c
#include <signal.h>
static volatile sig_atomic_t config_reload_requested = 0;
static void pinsight_signal_handler(int sig) {
    config_reload_requested = 1;  // Only safe operation in a signal handler
}
// Call this during constructor to install the handler
void pinsight_install_signal_handler() {
    struct sigaction sa;
    sa.sa_handler = pinsight_signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;  // Don't interrupt syscalls
    sigaction(SIGUSR1, &sa, NULL);
}
Then check the flag at a natural callback point, e.g., in 

lexgion_set_top_trace_bit_domain_event
:

c
if (config_reload_requested) {
    config_reload_requested = 0;
    pinsight_load_trace_config(NULL);  // Safe here — not in signal context
}
Approach 2: Direct handler (simpler but unsafe)
Call 

pinsight_load_trace_config
 directly from the signal handler. Works in practice on Linux but technically violates POSIX signal safety rules.

Recommendation: Approach 1 (flag-based). The check in 

lexgion_set_top_trace_bit_domain_event
 is essentially free — just reading one volatile variable. Only one thread needs to act on it.
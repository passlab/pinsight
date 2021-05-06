#!/bin/bash
lttng-sessiond --daemonize
lttng list --userspace
lttng create my-user-space-session
lttng enable-event -u "lttng_ust_dl:*"
lttng enable-event -u -a
lttng add-context -u -t vpid -t ip
lttng start
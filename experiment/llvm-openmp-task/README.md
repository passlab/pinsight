### 
A simple program to demonstrate the use of OMPT callbacks. callback.h ompt-singal.h file copied from LLVM OpenMP runtime test for easy experiment. You need to have
clang/llvm installed with OpenMP OMPT enabled to make this working.

### Task Directives on OpenMP 5.0

For OpenMP 5.0 specification, task has three model events:

1. The task-create event occurs when a thread encounters a construct that causes a new task to be created. The event occurs after the task is initialized but before it begins execution or is deferred.
1. The task-dependences event occurs in a thread that encounters a task generating construct or a taskwait construct with adepend clause immediately after the task-create event for the new task or the taskwait-init event.
1. The task-schedule event occurs in a thread when the thread switches tasks at a task scheduling point; no event occurs when switching to or from a merged task.

It also has four tool callbacks for different events.

1. ompt_callback_task_create :  A thread dispatches a registered ompt_callback_task_create callback for each occurrence of a task-create event in the context of the encountering task.

This callback has the type signature ompt_callback_task_create_t and the flags argument indicates the task types shown below:

Operation | Evaluates to true
--- | --- 
(flags & ompt_task_explicit) | Always in the dispatched callback 
(flags & ompt_task_undeferred) | If the task is an undeferred task
(flags & ompt_task_final) | If the task is a final task
(flags & ompt_task_untied) | If the task is an untied task
(flags & ompt_task_mergeable) | If the task is a mergeable task
(flags& ompt_task_merged) | If the task is a merged task

2. ompt_callback_dependences : A thread dispatches the ompt_callback_dependences callback for each occurrence of the task-dependences event to announce its dependences with respect to the list items in thedepend clause. This callback has type signatureompt_callback_dependences_t.

3. ompt_callback_task_dependence : A thread dispatches the ompt_callback_task_dependence callback for a task-dependence event to report a dependence between a predecessor task (src_task_data) and a dependent task (sink_task_data). This callback has type signatureompt_callback_task_dependence_t.

4. ompt_callback_task_schedule : A thread dispatches a registered ompt_callback_task_schedule callback for each occurrence of a task-schedule event in the context of the task that begins or resumes. This callback has the type signature ompt_callback_task_schedule_t. The argument prior_task_status is used to indicate the cause for suspending the prior task. This cause may be the completion of the prior task region, the encountering of a taskyield construct, or the encountering of an active cancellation point.

### Difference between OpenMP 5.0 and OpenMP 5.1
There is no difference discovered between OpenMP 5.0 and OpenMP 5.1

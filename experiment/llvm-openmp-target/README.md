###
A simple program to demonstrate the use of OMPT callbacks. callback.h ompt-singal.h file copied from LLVM OpenMP runtime test for easy experiment. You need to have
clang/llvm installed with OpenMP OMPT enabled to make this working.

### Device Drirectives on OpenMP 5.0
For OpenMP 5.0 specificantion, target offloading has four execution model events: device-initialize, device-load, device-unload, device-finalize. 

1. The device-initialize event occurs in a thread that encounters the first target, target data, or target enter data construct or a device memory routine that is associated with a particular target device after the thread initiates initialization of OpenMP on the device and the device’s OpenMP initialization, which may include device-side tool initialization, completes.
1. The device-load event for a code block for a target device occurs in some thread before any thread executes code from that code block on that target device.
1. The device-unload event for a target device occurs in some thread whenever a code block is unloaded from the device.
1. The device-finalize event for a target device that has been initialized occurs in some thread before an OpenMP implementation shuts down.

It also has four tools callback for different events.

1. ompt_callback_device_initialize_t: A thread dispatches a registered ompt_callback_device_initialize callback for each occurrence of a device-initialize event in that thread.
1. ompt_callback_device_load_t: A thread dispatches a registered ompt_callback_device_load callback for each occurrence of a device-load event in that thread.
1. ompt_callback_device_unload_t: A thread dispatches a registered ompt_callback_device_unload callback for each occurrence of a device-unload event in that thread. 
1. ompt_callback_device_finalize_t: A thread dispatches a registered ompt_callback_device_finalize callback for each occurrence of a device-finalize event in that thread. 

Besides, target offloading may need some value information which can get through the: ompt_get_target_info and ompt_get_num_devices.

It also has four callback related the target offloading: ompt_callback_target_t, ompt_callback_target_map_t, ompt_callback_target_data_op_t, ompt_callback_target_submit_t.

1. The ompt_callback_target_t type is used for callbacks that are dispatched when a thread begins to execute a device construct.
1. The ompt_callback_target_map_t type is used for callbacks that are dispatched to indicate data mapping relationships.
1. The ompt_callback_target_data_op_t type is used for callbacks that are dispatched when a thread maps data to a device.
1. The ompt_callback_target_submit_t type is used for callbacks that are dispatched when an initial task is created on a device.

### Device Drirectives on OpenMP 5.1

For OpenMP 5.1 specificantion, target offloading has four execution model events: device-initialize, device-load, device-unload, device-finalize. 

1. The device-initialize event occurs in a thread that encounters the first target, target data, or target enter data construct or a device memory routine that is associated with a particular target device after the thread initiates initialization of OpenMP on the device and the device’s OpenMP initialization, which may include device-side tool initialization, completes.
1. The device-load event for a code block for a target device occurs in some thread before any thread executes code from that code block on that target device.
1. The device-unload event for a target device occurs in some thread whenever a code block is unloaded from the device.
1. The device-finalize event for a target device that has been initialized occurs in some thread before an OpenMP implementation shuts down.

It also has four tools callback for different events.

1. ompt_callback_device_initialize_t: A thread dispatches a registered ompt_callback_device_initialize callback for each occurrence of a device-initialize event in that thread.
1. ompt_callback_device_load_t: A thread dispatches a registered ompt_callback_device_load callback for each occurrence of a device-load event in that thread.
1. ompt_callback_device_unload_t: A thread dispatches a registered ompt_callback_device_unload callback for each occurrence of a device-unload event in that thread. 
1. ompt_callback_device_finalize_t: A thread dispatches a registered ompt_callback_device_finalize callback for each occurrence of a device-finalize event in that thread. 

Besides, target offloading may need some value information which can get through the: ompt_get_target_info and ompt_get_num_devices.

It also has eight callback related the target offloading: oompt_callback_target_t, ompt_callback_target_map_t, ompt_callback_target_data_op_t, ompt_callback_target_submit_t,ompt_callback_target_emi_t, ompt_callback_target_map_emi_t, ompt_callback_target_data_op_emi_t, ompt_callback_target_submit_emi_t.

1. The ompt_callback_target_t and ompt_callback_target_emi_t type are used for callbacks that are dispatched when a thread begins to execute a device construct.
1. The ompt_callback_target_map_t and ompt_callback_target_map_emi_t type are used for callbacks that are dispatched to indicate data mapping relationships.
1. The ompt_callback_target_data_op_t and ompt_callback_target_data_op_emi_t type are used for callbacks that are dispatched when a thread maps data to a device.
1. The ompt_callback_target_submit_t and ompt_callback_target_submit_emi_t type are used for callbacks that are dispatched when an initial task is created on a device.

### Difference between OpenMP 5.0 and OpenMP 5.1

Comapred to OpenMP 5.0, OpenMP 5.1 add four new callback which are ompt_callback_target_emi_t, ompt_callback_target_map_emi_t, ompt_callback_target_data_op_emi_t, ompt_callback_target_submit_emi_t. Those new callback has detailded descirption as follow:

A thread dispatches a registered ompt_callback_target_data_op_emi orompt_callback_target_data_op callback when device memory is allocated or freed, as well as when data is copied to or from a device. ompt_callback_target_map_emi or ompt_callback_target_map callback to report the effect of all mappings or multiple ompt_callback_target_map_emi or ompt_callback_target_map callbacks with each reporting a subset of the mappings. Furthermore, an OpenMP implementation may omit mappings that it determines are unnecessary. If an OpenMP implementation issues multiple ompt_callback_target_map_emi or ompt_callback_target_map callbacks, these callbacks may be interleaved with ompt_callback_target_data_op_emi or ompt_callback_target_data_op allbacks used to report data operations associated with the mappings. A thread dispatches a registered ompt_callback_target_submit_emi or ompt_callback_target_submit callback on the host before and after a target task initiates creation of an initial task on a device.

#include <stdio.h>
#include <omp.h>
#include <omp-tools.h>
#include "pinsight.h"

/**
 * sysdefault config is set according to whether the callback is implemented or not in omp_callback.c file
 */

#define omp_callback_setting_full(name, cbf, __implemented__, __toChange__, __status__, __newStatus__)                       \
do {                                                          \
	omp_trace_configs[name].event_callback = name; \
	omp_trace_configs[name].callback = (ompt_callback_t) &cbf; \
	omp_trace_configs[name].implemented = __implemented__; \
	omp_trace_configs[name].status = __status__; \
	omp_trace_configs[name].toChange = __toChange__; \
	omp_trace_configs[name].newStatus = __newStatus__; \
} while(0)

#define omp_callback_setting_name_full(name, __implemented__, __toChange__, __status__, __newStatus__) omp_callback_setting_full(name, on_##name, __implemented__, __toChange__, __status__, __newStatus__)

#define CALLBACK_IMPLEMENTED 1
#define CALLBACK_NOT_IMPLEMENTED 0
#define STATUS_TO_CHANGE 1
#define STATUS_NOT_TO_CHANGE 0
#define CURRENT_STATUS_ENABLED 1
#define CURRENT_STATUS_DISABLED 0
#define NEW_STATUS_ENABLED 1
#define NEW_STATUS_DISABLED 0

#define omp_callback_setting_name(name, __newStatus__) \
do {                                                  \
	omp_trace_configs[name].toChange = STATUS_TO_CHANGE; \
	omp_trace_configs[name].newStatus = __newStatus__; \
} while(0)

//* place holder for callback funcs
void on_ompt_callback_thread_begin(){}
void on_ompt_callback_thread_end(){}
void on_ompt_callback_parallel_begin(){}
void on_ompt_callback_parallel_end(){}
void on_ompt_callback_task_create(){}
void on_ompt_callback_task_schedule(){}
void on_ompt_callback_implicit_task(){}
void on_ompt_callback_target(){}
void on_ompt_callback_target_data_op(){}
void on_ompt_callback_target_submit(){}
void on_ompt_callback_control_tool(){}
void on_ompt_callback_device_initialize(){}
void on_ompt_callback_device_finalize(){}
void on_ompt_callback_device_load(){}
void on_ompt_callback_device_unload(){}
void on_ompt_callback_sync_region_wait(){}
void on_ompt_callback_mutex_released(){}
void on_ompt_callback_dependences(){}
void on_ompt_callback_task_dependence(){}
void on_ompt_callback_work(){}
void on_ompt_callback_masked(){}
void on_ompt_callback_sync_region(){}
void on_ompt_callback_lock_init(){}
void on_ompt_callback_lock_destroy(){}
void on_ompt_callback_mutex_acquire(){}
void on_ompt_callback_mutex_acquired(){}
void on_ompt_callback_nest_lock(){}
void on_ompt_callback_flush(){}
void on_ompt_callback_cancel(){}
void on_ompt_callback_reduction(){}
void on_ompt_callback_dispatch(){}
void on_ompt_callback_target_map(){}
void on_ompt_callback_target_emi(){}
void on_ompt_callback_target_data_op_emi(){}
void on_ompt_callback_target_submit_emi(){}
void on_ompt_callback_target_map_emi(){}
void on_ompt_callback_error(){}

void omp_config_set_sysdefault () {
	omp_callback_setting_name_full(ompt_callback_thread_begin, CALLBACK_IMPLEMENTED, STATUS_TO_CHANGE, CURRENT_STATUS_DISABLED, NEW_STATUS_ENABLED);
	omp_callback_setting_name_full(ompt_callback_thread_end, CALLBACK_IMPLEMENTED, STATUS_TO_CHANGE, CURRENT_STATUS_DISABLED, NEW_STATUS_ENABLED);
	omp_callback_setting_name_full(ompt_callback_parallel_begin, CALLBACK_IMPLEMENTED, STATUS_TO_CHANGE, CURRENT_STATUS_DISABLED, NEW_STATUS_ENABLED);
	omp_callback_setting_name_full(ompt_callback_parallel_end, CALLBACK_IMPLEMENTED, STATUS_TO_CHANGE, CURRENT_STATUS_DISABLED, NEW_STATUS_ENABLED);

	omp_callback_setting_name_full(ompt_callback_task_create, CALLBACK_NOT_IMPLEMENTED, STATUS_TO_CHANGE, CURRENT_STATUS_DISABLED, NEW_STATUS_DISABLED);
	omp_callback_setting_name_full(ompt_callback_task_schedule, CALLBACK_NOT_IMPLEMENTED, STATUS_TO_CHANGE, CURRENT_STATUS_DISABLED, NEW_STATUS_DISABLED);
	omp_callback_setting_name_full(ompt_callback_implicit_task, CALLBACK_IMPLEMENTED, STATUS_TO_CHANGE, CURRENT_STATUS_DISABLED, NEW_STATUS_ENABLED);

	omp_callback_setting_name_full(ompt_callback_target, CALLBACK_NOT_IMPLEMENTED, STATUS_TO_CHANGE, CURRENT_STATUS_DISABLED, NEW_STATUS_DISABLED);
	omp_callback_setting_name_full(ompt_callback_target_data_op, CALLBACK_NOT_IMPLEMENTED, STATUS_TO_CHANGE, CURRENT_STATUS_DISABLED, NEW_STATUS_DISABLED);
	omp_callback_setting_name_full(ompt_callback_target_submit, CALLBACK_NOT_IMPLEMENTED, STATUS_TO_CHANGE, CURRENT_STATUS_DISABLED, NEW_STATUS_DISABLED);
	omp_callback_setting_name_full(ompt_callback_control_tool, CALLBACK_IMPLEMENTED, STATUS_TO_CHANGE, CURRENT_STATUS_DISABLED, NEW_STATUS_DISABLED);
	omp_callback_setting_name_full(ompt_callback_device_initialize, CALLBACK_NOT_IMPLEMENTED, STATUS_TO_CHANGE, CURRENT_STATUS_DISABLED, NEW_STATUS_DISABLED);
	omp_callback_setting_name_full(ompt_callback_device_finalize, CALLBACK_NOT_IMPLEMENTED, STATUS_TO_CHANGE, CURRENT_STATUS_DISABLED, NEW_STATUS_DISABLED);
	omp_callback_setting_name_full(ompt_callback_device_load, CALLBACK_NOT_IMPLEMENTED, STATUS_TO_CHANGE, CURRENT_STATUS_DISABLED, NEW_STATUS_DISABLED);
	omp_callback_setting_name_full(ompt_callback_device_unload, CALLBACK_NOT_IMPLEMENTED, STATUS_TO_CHANGE, CURRENT_STATUS_DISABLED, NEW_STATUS_DISABLED);
	omp_callback_setting_name_full(ompt_callback_sync_region_wait, CALLBACK_IMPLEMENTED, STATUS_TO_CHANGE, CURRENT_STATUS_DISABLED, NEW_STATUS_ENABLED);
	omp_callback_setting_name_full(ompt_callback_mutex_released, CALLBACK_NOT_IMPLEMENTED, STATUS_TO_CHANGE, CURRENT_STATUS_DISABLED, NEW_STATUS_DISABLED);
	omp_callback_setting_name_full(ompt_callback_dependences, CALLBACK_NOT_IMPLEMENTED, STATUS_TO_CHANGE, CURRENT_STATUS_DISABLED, NEW_STATUS_DISABLED);
	omp_callback_setting_name_full(ompt_callback_task_dependence, CALLBACK_NOT_IMPLEMENTED, STATUS_TO_CHANGE, CURRENT_STATUS_DISABLED, NEW_STATUS_DISABLED);
	omp_callback_setting_name_full(ompt_callback_work, CALLBACK_IMPLEMENTED, STATUS_TO_CHANGE, CURRENT_STATUS_DISABLED, NEW_STATUS_ENABLED);
	omp_callback_setting_name_full(ompt_callback_masked, CALLBACK_IMPLEMENTED, STATUS_TO_CHANGE, CURRENT_STATUS_DISABLED, NEW_STATUS_ENABLED);
	omp_callback_setting_name_full(ompt_callback_sync_region, CALLBACK_IMPLEMENTED, STATUS_TO_CHANGE, CURRENT_STATUS_DISABLED, NEW_STATUS_ENABLED);
	omp_callback_setting_name_full(ompt_callback_lock_init, CALLBACK_NOT_IMPLEMENTED, STATUS_TO_CHANGE, CURRENT_STATUS_DISABLED, NEW_STATUS_DISABLED);
	omp_callback_setting_name_full(ompt_callback_lock_destroy, CALLBACK_NOT_IMPLEMENTED, STATUS_TO_CHANGE, CURRENT_STATUS_DISABLED, NEW_STATUS_DISABLED);
	omp_callback_setting_name_full(ompt_callback_mutex_acquire, CALLBACK_NOT_IMPLEMENTED, STATUS_TO_CHANGE, CURRENT_STATUS_DISABLED, NEW_STATUS_DISABLED);
	omp_callback_setting_name_full(ompt_callback_mutex_acquired, CALLBACK_NOT_IMPLEMENTED, STATUS_TO_CHANGE, CURRENT_STATUS_DISABLED, NEW_STATUS_DISABLED);
	omp_callback_setting_name_full(ompt_callback_nest_lock, CALLBACK_NOT_IMPLEMENTED, STATUS_TO_CHANGE, CURRENT_STATUS_DISABLED, NEW_STATUS_DISABLED);
	omp_callback_setting_name_full(ompt_callback_flush, CALLBACK_NOT_IMPLEMENTED, STATUS_TO_CHANGE, CURRENT_STATUS_DISABLED, NEW_STATUS_DISABLED);
	omp_callback_setting_name_full(ompt_callback_cancel, CALLBACK_NOT_IMPLEMENTED, STATUS_TO_CHANGE, CURRENT_STATUS_DISABLED, NEW_STATUS_DISABLED);
	omp_callback_setting_name_full(ompt_callback_reduction, CALLBACK_NOT_IMPLEMENTED, STATUS_TO_CHANGE, CURRENT_STATUS_DISABLED, NEW_STATUS_DISABLED);
	omp_callback_setting_name_full(ompt_callback_dispatch, CALLBACK_IMPLEMENTED, STATUS_TO_CHANGE, CURRENT_STATUS_DISABLED, NEW_STATUS_DISABLED);
	omp_callback_setting_name_full(ompt_callback_target_map, CALLBACK_NOT_IMPLEMENTED, STATUS_TO_CHANGE, CURRENT_STATUS_DISABLED, NEW_STATUS_DISABLED);
	omp_callback_setting_name_full(ompt_callback_target_emi, CALLBACK_IMPLEMENTED, STATUS_TO_CHANGE, CURRENT_STATUS_DISABLED, NEW_STATUS_DISABLED);
	omp_callback_setting_name_full(ompt_callback_target_data_op_emi, CALLBACK_IMPLEMENTED, STATUS_TO_CHANGE, CURRENT_STATUS_DISABLED, NEW_STATUS_DISABLED);
	omp_callback_setting_name_full(ompt_callback_target_submit_emi, CALLBACK_IMPLEMENTED, STATUS_TO_CHANGE, CURRENT_STATUS_DISABLED, NEW_STATUS_DISABLED);
	omp_callback_setting_name_full(ompt_callback_target_map_emi, CALLBACK_IMPLEMENTED, STATUS_TO_CHANGE, CURRENT_STATUS_DISABLED, NEW_STATUS_DISABLED);
	omp_callback_setting_name_full(ompt_callback_error, CALLBACK_NOT_IMPLEMENTED, STATUS_TO_CHANGE, CURRENT_STATUS_DISABLED, NEW_STATUS_DISABLED);
}

/**
 * Check the configuration (enable or disable) of the callback to enable or disable it based on the requested
 * reconfiguration setting in the callback config object.
 */
static void omp_config_check2config(ompt_callbacks_t callback) {
	omp_trace_config_t * config = &omp_trace_configs[callback];
	if (config->event_callback == callback) {
		if (config->toChange == STATUS_TO_CHANGE) {
			int newStatus = config->newStatus;
			if (newStatus == NEW_STATUS_ENABLED) {//to enable/register it
				if (config->implemented == CALLBACK_IMPLEMENTED) {
					if (config->status == CURRENT_STATUS_ENABLED) { //already enabled, report and do not anything
						printf("OMP event callback %d has already been enabled and registered\n", callback);
					} else {
						if (ompt_set_callback(callback, config->callback) == ompt_set_never) {
							printf("Could not register callback %d\n", callback);
						} else {
							printf("Enabling OMP event callback %d\n", callback);
							config->status = CURRENT_STATUS_ENABLED;
							config->toChange = STATUS_NOT_TO_CHANGE; //XXX: potential data race: e.g. one thread is updating the config while the other thread is applying the new config for a callback.
						}
					}
				} else {
					printf("OMP event callback %d has not been implemented and thus cannot be enabled or registered\n", callback);
				}
			} else { //to disable/unregister it
				if (config->status == CURRENT_STATUS_ENABLED) {
					ompt_set_callback(callback, NULL);
					config->status = CURRENT_STATUS_DISABLED;
					config->toChange = STATUS_NOT_TO_CHANGE; //XXX: potential data race: e.g. one thread is updating the config while the other thread is applying the new config for a callback.
					printf("Unregistering OMP event callback for event %d\n", callback);
				} else {
					//already disabled, not need to do anything
				}
			}
		} else {
			//The status is not to change, do nothing
		}
	} else {
		//This callback does not exist
	}
}
/**
 * sysdefault config is set according to whether the callback is implemented or not in omp_callback.c file
 */
void omp_config_config() {
	omp_config_check2config(ompt_callback_thread_begin);
	omp_config_check2config(ompt_callback_thread_end);
	omp_config_check2config(ompt_callback_parallel_begin);
	omp_config_check2config(ompt_callback_parallel_end);
	omp_config_check2config(ompt_callback_task_create);
	omp_config_check2config(ompt_callback_task_schedule);
	omp_config_check2config(ompt_callback_implicit_task);
	omp_config_check2config(ompt_callback_target);
	omp_config_check2config(ompt_callback_target_data_op);
	omp_config_check2config(ompt_callback_target_submit);
	omp_config_check2config(ompt_callback_control_tool);
	omp_config_check2config(ompt_callback_device_initialize);
	omp_config_check2config(ompt_callback_device_finalize);
	omp_config_check2config(ompt_callback_device_load);
	omp_config_check2config(ompt_callback_device_unload);
	omp_config_check2config(ompt_callback_sync_region_wait);
	omp_config_check2config(ompt_callback_mutex_released);
	omp_config_check2config(ompt_callback_dependences);
	omp_config_check2config(ompt_callback_task_dependence);
	omp_config_check2config(ompt_callback_work);
	omp_config_check2config(ompt_callback_masked);
	omp_config_check2config(ompt_callback_target_map);
	omp_config_check2config(ompt_callback_sync_region);
	omp_config_check2config(ompt_callback_lock_init);
	omp_config_check2config(ompt_callback_lock_destroy);
	omp_config_check2config(ompt_callback_mutex_acquire);
	omp_config_check2config(ompt_callback_mutex_acquired);
	omp_config_check2config(ompt_callback_nest_lock);
	omp_config_check2config(ompt_callback_flush);
	omp_config_check2config(ompt_callback_cancel);
	omp_config_check2config(ompt_callback_reduction);
	omp_config_check2config(ompt_callback_dispatch);
	omp_config_check2config(ompt_callback_target_emi);
	omp_config_check2config(ompt_callback_target_data_op_emi);
	omp_config_check2config(ompt_callback_target_submit_emi);
	omp_config_check2config(ompt_callback_target_map_emi);
	omp_config_check2config(ompt_callback_error);
}

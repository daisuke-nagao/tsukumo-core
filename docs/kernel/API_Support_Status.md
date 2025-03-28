# uT-Kernel API

## uT-Kernel/OS API

* [ ] `tk_can_wup` - Disable task wakeup request
* [ ] `tk_chg_pri` - Change task priority
* [x] `tk_clr_flg` - Clear event flag
* [ ] `tk_cre_alm` - Create alarm handler
* [ ] `tk_cre_cyc` - Create cyclic handler
* [ ] `tk_cre_cyc_u` - Create cyclic handler (microsecond unit)
* [x] `tk_cre_flg` - Create event flag
* [ ] `tk_cre_mbf` - Create message buffer
* [ ] `tk_cre_mbx` - Create mailbox
* [ ] `tk_cre_mpf` - Create fixed-size memory pool
* [ ] `tk_cre_mpl` - Create variable-size memory pool
* [ ] `tk_cre_mtx` - Create mutex
* [ ] `tk_cre_sem` - Create semaphore
* [x] `tk_cre_tsk` - Create task
* [ ] `tk_def_int` - Define interrupt handler
* [ ] `tk_def_ssy` - Define subsystem
* [ ] `tk_def_tex` - Define task exception handler
* [ ] `tk_del_alm` - Delete alarm handler
* [ ] `tk_del_cyc` - Delete cyclic handler
* [ ] `tk_del_flg` - Delete event flag
* [ ] `tk_del_mbf` - Delete message buffer
* [ ] `tk_del_mbx` - Delete mailbox
* [ ] `tk_del_mpf` - Delete fixed-size memory pool
* [ ] `tk_del_mpl` - Delete variable-size memory pool
* [ ] `tk_del_mtx` - Delete mutex
* [ ] `tk_del_sem` - Delete semaphore
* [ ] `tk_del_tsk` - Delete task
* [ ] `tk_dis_dsp` - Disable dispatch
* [ ] `tk_dis_tex` - Disable task exception
* [ ] `tk_dis_wai` - Disable task wait state
* [x] `tk_dly_tsk` - Delay task
* [ ] `tk_dly_tsk_u` - Delay task (microsecond unit)
* [ ] `tk_ena_dsp` - Enable dispatch
* [ ] `tk_ena_tex` - Enable task exception
* [ ] `tk_ena_wai` - Enable task wait state
* [ ] `tk_end_tex` - End task exception handler
* [ ] `tk_evt_ssy` - Call event processing function
* [x] `tk_exd_tsk` - Exit and delete self-task
* [x] `tk_ext_tsk` - Exit self-task
* [ ] `tk_frsm_tsk` - Force resume task in forced wait state
* [ ] `tk_get_cpr` - Get coprocessor register
* [ ] `tk_get_mpf` - Acquire fixed-size memory block
* [ ] `tk_get_mpf_u` - Acquire fixed-size memory block (microsecond unit)
* [ ] `tk_get_mpl` - Acquire variable-size memory block
* [ ] `tk_get_mpl_u` - Acquire variable-size memory block (microsecond unit)
* [ ] `tk_get_otm` - Refer system operating time
* [ ] `tk_get_otm_u` - Refer system operating time (microsecond unit)
* [ ] `tk_get_reg` - Get task register
* [ ] `tk_get_tid` - Refer task ID of running task
* [ ] `tk_get_tim` - Refer system time (TRON expression)
* [ ] `tk_get_tim_u` - Refer system time (TRON expression, microsecond unit)
* [ ] `tk_get_utc` - Refer system time
* [ ] `tk_get_utc_u` - Refer system time (microsecond unit)
* [ ] `tk_loc_mtx` - Lock mutex
* [ ] `tk_loc_mtx_u` - Lock mutex (microsecond unit)
* [ ] `tk_ras_tex` - Raise task exception
* [ ] `tk_rcv_mbf` - Receive from message buffer
* [ ] `tk_rcv_mbf_u` - Receive from message buffer (microsecond unit)
* [ ] `tk_rcv_mbx` - Receive from mailbox
* [ ] `tk_rcv_mbx_u` - Receive from mailbox (microsecond unit)
* [ ] `tk_ref_alm` - Refer alarm handler state
* [ ] `tk_ref_alm_u` - Refer alarm handler state (microsecond unit)
* [ ] `tk_ref_cyc` - Refer cyclic handler state
* [ ] `tk_ref_cyc_u` - Refer cyclic handler state (microsecond unit)
* [ ] `tk_ref_flg` - Refer event flag state
* [ ] `tk_ref_mbf` - Refer message buffer state
* [ ] `tk_ref_mbx` - Refer mailbox state
* [ ] `tk_ref_mpf` - Refer fixed-size memory pool state
* [ ] `tk_ref_mpl` - Refer variable-size memory pool state
* [ ] `tk_ref_mtx` - Refer mutex state
* [ ] `tk_ref_sem` - Refer semaphore state
* [ ] `tk_ref_ssy` - Refer subsystem definition information
* [ ] `tk_ref_sys` - Refer system state
* [ ] `tk_ref_tex` - Refer task exception state
* [ ] `tk_ref_tsk` - Refer task state
* [ ] `tk_ref_ver` - Refer version
* [ ] `tk_rel_mpf` - Return fixed-size memory block
* [ ] `tk_rel_mpl` - Return variable-size memory block
* [x] `tk_rel_wai` - Release other task from wait state
* [ ] `tk_ret_int` - Return from interrupt handler
* [ ] `tk_rot_rdq` - Rotate task priority
* [ ] `tk_rsm_tsk` - Resume task in forced wait state
* [ ] `tk_set_cpr` - Set coprocessor register
* [x] `tk_set_flg` - Set event flag
* [ ] `tk_set_pow` - Set power-saving mode
* [ ] `tk_set_reg` - Set task register
* [ ] `tk_set_tim` - Set system time (TRON expression)
* [ ] `tk_set_tim_u` - Set system time (TRON expression, microsecond unit)
* [ ] `tk_set_utc` - Set system time
* [ ] `tk_set_utc_u` - Set system time (microsecond unit)
* [ ] `tk_sig_sem` - Return semaphore resource
* [ ] `tk_sig_tev` - Send task event
* [x] `tk_slp_tsk` - Transition self-task to wakeup wait state
* [ ] `tk_slp_tsk_u` - Transition self-task to wakeup wait state (microsecond unit)
* [ ] `tk_snd_mbf` - Send to message buffer
* [ ] `tk_snd_mbf_u` - Send to message buffer (microsecond unit)
* [ ] `tk_snd_mbx` - Send to mailbox
* [ ] `tk_sta_alm` - Start alarm handler
* [ ] `tk_sta_alm_u` - Start alarm handler (microsecond unit)
* [ ] `tk_sta_cyc` - Start cyclic handler
* [x] `tk_sta_tsk` - Start task
* [ ] `tk_stp_alm` - Stop alarm handler
* [ ] `tk_stp_cyc` - Stop cyclic handler
* [ ] `tk_sus_tsk` - Transition other task to forced wait state
* [ ] `tk_ter_tsk` - Force terminate other task
* [ ] `tk_unl_mtx` - Unlock mutex
* [x] `tk_wai_flg` - Wait for event flag
* [ ] `tk_wai_flg_u` - Wait for event flag (microsecond unit)
* [ ] `tk_wai_sem` - Acquire semaphore resource
* [ ] `tk_wai_sem_u` - Acquire semaphore resource (microsecond unit)
* [ ] `tk_wai_tev` - Wait for task event
* [ ] `tk_wai_tev_u` - Wait for task event (microsecond unit)
* [x] `tk_wup_tsk` - Wake up other task

## uT-Kernel/DS API

* [ ] `td_flg_que` - Reference event flag wait queue
* [ ] `td_get_otm` - Reference system operating time
* [ ] `td_get_otm_u` - Reference system operating time (microsecond unit)
* [ ] `td_get_reg` - Reference task register
* [ ] `td_get_tim` - Reference system time (TRON expression)
* [ ] `td_get_tim_u` - Reference system time (TRON expression, microsecond unit)
* [ ] `td_get_utc` - Reference system time
* [ ] `td_get_utc_u` - Reference system time (microsecond unit)
* [ ] `td_hok_dsp` - Define task dispatch hook routine
* [ ] `td_hok_int` - Define interrupt handler hook routine
* [ ] `td_hok_svc` - Define system call/extended SVC hook routine
* [ ] `td_lst_alm` - Reference alarm handler ID list
* [ ] `td_lst_cyc` - Reference cyclic handler ID list
* [ ] `td_lst_flg` - Reference event flag ID list
* [ ] `td_lst_mbf` - Reference message buffer ID list
* [ ] `td_lst_mbx` - Reference mailbox ID list
* [ ] `td_lst_mpf` - Reference fixed-size memory pool ID list
* [ ] `td_lst_mpl` - Reference variable-size memory pool ID list
* [ ] `td_lst_mtx` - Reference mutex ID list
* [ ] `td_lst_sem` - Reference semaphore ID list
* [ ] `td_lst_ssy` - Reference subsystem ID list
* [ ] `td_lst_tsk` - Reference task ID list
* [ ] `td_mbx_que` - Reference mailbox wait queue
* [ ] `td_mpf_que` - Reference fixed-size memory pool wait queue
* [ ] `td_mpl_que` - Reference variable-size memory pool wait queue
* [ ] `td_mtx_que` - Reference mutex wait queue
* [ ] `td_rdy_que` - Reference task priority queue
* [ ] `td_ref_alm` - Reference alarm handler state
* [ ] `td_ref_alm_u` - Reference alarm handler state (microsecond unit)
* [ ] `td_ref_cyc` - Reference cyclic handler state
* [ ] `td_ref_cyc_u` - Reference cyclic handler state (microsecond unit)
* [ ] `td_ref_dsname` - Reference DS object name
* [ ] `td_ref_flg` - Reference event flag state
* [ ] `td_ref_mbf` - Reference message buffer state
* [ ] `td_ref_mbx` - Reference mailbox state
* [ ] `td_ref_mpf` - Reference fixed-size memory pool state
* [ ] `td_ref_mpl` - Reference variable-size memory pool state
* [ ] `td_ref_mtx` - Reference mutex state
* [ ] `td_ref_sem` - Reference semaphore state
* [ ] `td_ref_ssy` - Reference subsystem definition information
* [ ] `td_ref_sys` - Reference system state
* [ ] `td_ref_tex` - Reference task exception state
* [ ] `td_ref_tsk` - Reference task state
* [ ] `td_rmbf_que` - Reference message buffer receive wait queue
* [ ] `td_sem_que` - Reference semaphore wait queue
* [ ] `td_set_dsname` - Set DS object name
* [ ] `td_set_reg` - Set task register
* [ ] `td_smbf_que` - Reference message buffer send wait queue

## uT-Kernel/SM API

* [ ] `abortfn` - Abort function
* [ ] `CheckInt` - Check interrupt occurrence
* [ ] `ClearInt` - Clear interrupt occurrence
* [ ] `closefn` - Close function
* [ ] `ControlCache` - Control cache
* [ ] `CreateLock` - Create fast lock
* [ ] `CreateMLock` - Create fast multi-lock
* [ ] `DefinePhysicalTimerHandler` - Define physical timer handler
* [ ] `DeleteLock` - Delete fast lock
* [ ] `DeleteMLock` - Delete fast multi-lock
* [x] `DI` - Disable external interrupt
* [ ] `DisableInt` - Disable interrupt
* [x] `EI` - Enable external interrupt
* [ ] `EnableInt` - Enable interrupt
* [ ] `EndOfInt` - Issue EOI to interrupt controller
* [ ] `eventfn` - Event function
* [ ] `execfn` - Start processing function
* [ ] `GetCpuIntLevel` - Get CPU internal interrupt mask level
* [ ] `GetCtrlIntLevel` - Get interrupt controller internal interrupt mask level
* [ ] `GetPhysicalTimerConfig` - Get physical timer configuration information
* [ ] `GetPhysicalTimerCount` - Get physical timer count value
* [x] `in_b` - Read I/O port (byte)
* [x] `in_d` - Read I/O port (double word)
* [x] `in_h` - Read I/O port (half word)
* [x] `in_w` - Read I/O port (word)
* [x] `isDI` - Get external interrupt disable state
* [ ] `Kcalloc` - Allocate and clear memory
* [ ] `Kfree` - Free memory
* [ ] `Kmalloc` - Allocate memory
* [ ] `Krealloc` - Reallocate memory
* [ ] `Lock` - Lock fast lock
* [ ] `low_pow` - Transition system to low power mode
* [ ] `MLock` - Lock fast multi-lock
* [ ] `MLockTmo` - Lock fast multi-lock (with timeout)
* [ ] `MLockTmo_u` - Lock fast multi-lock (with timeout, microsecond unit)
* [ ] `MUnlock` - Unlock fast multi-lock
* [ ] `off_pow` - Transition system to suspend state
* [ ] `openfn` - Open function
* [x] `out_b` - Write I/O port (byte)
* [x] `out_d` - Write I/O port (double word)
* [x] `out_h` - Write I/O port (half word)
* [x] `out_w` - Write I/O port (word)
* [ ] `SetCacheMode` - Set cache mode
* [ ] `SetCpuIntLevel` - Set CPU internal interrupt mask level
* [ ] `SetCtrlIntLevel` - Set interrupt controller internal interrupt mask level
* [ ] `SetIntMode` - Set interrupt mode
* [ ] `SetOBJNAME` - Set object name
* [ ] `StartPhysicalTimer` - Start physical timer
* [ ] `StopPhysicalTimer` - Stop physical timer
* [ ] `tk_cls_dev` - Close device
* [ ] `tk_def_dev` - Register device
* [ ] `tk_evt_dev` - Send driver request event to device
* [ ] `tk_get_cfn` - Get numeric array from system configuration information
* [ ] `tk_get_cfs` - Get string from system configuration information
* [ ] `tk_get_dev` - Get device name
* [ ] `tk_lst_dev` - Get list of registered devices
* [ ] `tk_opn_dev` - Open device
* [ ] `tk_oref_dev` - Get device information
* [ ] `tk_rea_dev` - Start reading device
* [ ] `tk_rea_dev_du` - Start reading device (64-bit microsecond unit)
* [ ] `tk_ref_dev` - Get device information
* [ ] `tk_ref_idv` - Get initial device information
* [ ] `tk_srea_dev` - Synchronous read device
* [ ] `tk_srea_dev_d` - Synchronous read device (64-bit)
* [ ] `tk_sus_dev` - Suspend device
* [ ] `tk_swri_dev` - Synchronous write device
* [ ] `tk_swri_dev_d` - Synchronous write device (64-bit)
* [ ] `tk_wai_dev` - Wait for device request completion
* [ ] `tk_wai_dev_u` - Wait for device request completion (microsecond unit)
* [ ] `tk_wri_dev` - Start writing device
* [ ] `tk_wri_dev_du` - Start writing device (64-bit microsecond unit)
* [ ] `Unlock` - Unlock fast lock
* [ ] `waitfn` - Wait for completion function
* [ ] `WaitNsec` - Micro wait (nanoseconds)
* [ ] `WaitUsec` - Micro wait (microseconds)

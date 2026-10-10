# Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
# SPDX-License-Identifier: BSD-3-Clause

define print_thread_state
    set $_s = $arg0
    set $_t = $arg1
    if $_t == _kernel.cpus[0].current
        printf "RUNNING  "
    else
        if $_s & 0x08
            printf "DEAD     "
        else
            if $_s & 0x10
                printf "SUSPENDED"
            else
                if $_s & 0x02
                    printf "PENDING  "
                else
                    if $_s & 0x20
                        printf "ABORTING "
                    else
                        if $_s & 0x80
                            printf "QUEUED   "
                        else
                            printf "0x%02x     ", $_s
                        end
                    end
                end
            end
        end
    end
end

define pat
    printf "%-12s  %-20s  %-9s  %4s  %-12s  %s\n", \
        "Addr", "Name", "State", "Prio", "Stack", "Size"
    set $t = _kernel.threads
    while ($t != 0)
        printf "0x%08x  ", $t
        printf "%-20s  ", $t->name
        print_thread_state $t->base.thread_state $t
        printf "  %4d  ", $t->base.prio
        printf "0x%08x  ", $t->stack_info.start
        printf "%d\n", $t->stack_info.size
        set $t = $t->next_thread
    end
end


# btthread -- backtrace a Zephyr thread by name on RISC-V
#
# Usage:
#   btthread <thread-name>
#
# RISC-V Zephyr callee_saved layout:
#   sp, ra, s0-s11

define btthread
  set $_th = (struct k_thread *)$arg0

  # save current CPU context
  set $_s_sp  = $sp
  set $_s_pc  = $pc
  set $_s_ra  = $ra
  set $_s_s0  = $s0
  set $_s_s1  = $s1
  set $_s_s2  = $s2
  set $_s_s3  = $s3
  set $_s_s4  = $s4
  set $_s_s5  = $s5
  set $_s_s6  = $s6
  set $_s_s7  = $s7
  set $_s_s8  = $s8
  set $_s_s9  = $s9
  set $_s_s10 = $s10
  set $_s_s11 = $s11

  if $_th->callee_saved.ra == z_riscv_thread_start
    # ISR-preempted: actual state in arch_esf at callee_saved.sp
    set $_esf = (struct arch_esf *)($_th->callee_saved.sp)
    set $pc  = $_esf->mepc
    set $ra  = $_esf->ra
    set $s0  = $_esf->s0
    set $sp  = (unsigned long)$_esf + sizeof(struct arch_esf)
    set $s1  = $_th->callee_saved.s1
    set $s2  = $_th->callee_saved.s2
    set $s3  = $_th->callee_saved.s3
    set $s4  = $_th->callee_saved.s4
    set $s5  = $_th->callee_saved.s5
    set $s6  = $_th->callee_saved.s6
    set $s7  = $_th->callee_saved.s7
    set $s8  = $_th->callee_saved.s8
    set $s9  = $_th->callee_saved.s9
    set $s10 = $_th->callee_saved.s10
    set $s11 = $_th->callee_saved.s11
  else
    # cooperative switch: state in callee_saved
    set $sp  = $_th->callee_saved.sp
    set $pc  = $_th->callee_saved.ra
    set $ra  = $_th->callee_saved.ra
    set $s0  = $_th->callee_saved.s0
    set $s1  = $_th->callee_saved.s1
    set $s2  = $_th->callee_saved.s2
    set $s3  = $_th->callee_saved.s3
    set $s4  = $_th->callee_saved.s4
    set $s5  = $_th->callee_saved.s5
    set $s6  = $_th->callee_saved.s6
    set $s7  = $_th->callee_saved.s7
    set $s8  = $_th->callee_saved.s8
    set $s9  = $_th->callee_saved.s9
    set $s10 = $_th->callee_saved.s10
    set $s11 = $_th->callee_saved.s11
  end

  printf "=== backtrace: %s (state=0x%x prio=%d) ===\n", \
    $_th->name, $_th->base.thread_state, $_th->base.prio
  bt

  # restore CPU context
  set $sp  = $_s_sp
  set $pc  = $_s_pc
  set $ra  = $_s_ra
  set $s0  = $_s_s0
  set $s1  = $_s_s1
  set $s2  = $_s_s2
  set $s3  = $_s_s3
  set $s4  = $_s_s4
  set $s5  = $_s_s5
  set $s6  = $_s_s6
  set $s7  = $_s_s7
  set $s8  = $_s_s8
  set $s9  = $_s_s9
  set $s10 = $_s_s10
  set $s11 = $_s_s11
end

document btthread
Usage: btthread <thread-addr>
Print the call stack of a suspended Zephyr thread (RISC-V).
Get thread address from bpat output.
Example: btthread 0x6300fa00
end

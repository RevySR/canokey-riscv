# CPU1 may be kept in reset by firmware that runs only on CPU0.
proc k230d_connect_cpu1 {{start 0}} {
    set previous [target current]
    set running 0
    set release_attempted 0
    set dm_armed 0
    set rc [catch {
        targets k230d.cpu0
        k230d.cpu0 arp_poll
        set state [k230d.cpu0 curstate]
        if {$state ne "running" && $state ne "halted"} {
            error "CPU0 is $state; cannot access CPU1 reset control"
        }
        set running [expr {$state eq "running"}]
        if {$running} { halt 1000 }
        set reset [lindex [read_memory 0x9110100c 32 1] 0]
        if {($reset & 1) && !$start} {
            echo "CPU1 is held in reset. Set K230D_START_CPU1=1 to release it halted."
        } else {
            if {$reset & 1} {
                set clock [lindex [read_memory 0x91100004 32 1] 0]
                if {($clock & 0x80001) != 0x80001} {
                    error "CPU1 core/APB clocks are disabled"
                }
                set saved_dm [riscv dmi_read 0x410]
                set dm_armed 1
                riscv dmi_write 0x410 1
                set status [riscv dmi_read 0x411]
                if {($status & 0xa0) != 0xa0} {
                    error "CPU1 DM lacks authentication or reset-halt support"
                }
                # Arm halt-on-reset and haltreq before removing the SoC reset.
                riscv dmi_write 0x410 0x80000009
                set release_attempted 1
                # Bit 16 is the write-enable mask for cpu1_reset_req, bit 0.
                write_memory 0x9110100c 32 {0x10000}
                set halted 0
                for {set i 0} {$i < 100} {incr i} {
                    set status [riscv dmi_read 0x411]
                    if {$status & 0x200} {
                        set halted 1
                        break
                    }
                    sleep 10
                }
                if {!$halted} { error "CPU1 did not halt after reset release" }
                riscv dmi_write 0x410 5
            }
            k230d.cpu1 arp_examine
            k230d_cpu_info k230d.cpu1
            echo "CPU1 state: [k230d.cpu1 curstate]"
        }
    } message]

    targets k230d.cpu0
    set rollback_rc 0
    if {$rc && $dm_armed} {
        set rollback_rc [catch {
            if {$release_attempted} {
                write_memory 0x9110100c 32 {0x10001}
            }
            riscv dmi_write 0x410 5
            riscv dmi_write 0x410 $saved_dm
        } rollback_message]
    }
    set resume_rc 0
    if {$running} { set resume_rc [catch {resume} resume_message] }
    targets $previous
    if {$rollback_rc} { error "CPU1 rollback failed: $rollback_message; original error: $message" }
    if {$resume_rc} { error "CPU0 could not resume: $resume_message" }
    if {$rc} { error $message }
}

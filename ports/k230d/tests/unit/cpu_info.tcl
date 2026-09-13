# Run with: openocd -f ports/k230d/tests/unit/cpu_info.tcl -c shutdown
source [file join [file dirname [info script]] ../../openocd/cpu-info.tcl]

proc echo {message} {
    lappend ::output $message
}

proc target {command} {
    return $::selected
}

proc targets {name} {
    set ::selected $name
}

proc halt {timeout} {
    incr ::halts
    set ::state halted
}

proc resume {} {
    incr ::resumes
    set ::state running
}

proc k230d.cpu0 {command args} {
    switch $command {
        arp_poll { return }
        curstate { return $::state }
        get_reg {
            if {$::fail_read} { error "injected register read failure" }
            if {[lindex $args 1] eq "csr_mcpuid"} {
                set word [lindex $::words $::index]
                set ::index [expr {($::index + 1) % 7}]
                return [dict create csr_mcpuid $word]
            }
            return $::regs
        }
        default { error "unexpected command $command" }
    }
}

proc check {condition} {
    if {![uplevel 1 [list expr $condition]]} { error "check failed: $condition" }
}

proc prepare {initial_state} {
    set ::selected other.cpu
    set ::state $initial_state
    set ::halts 0
    set ::resumes 0
    set ::fail_read 0
    set ::index 3
    set ::output {}
}

set regs [dict create mvendorid 0x5b7 marchid 0x8000000009140d00 \
    mimpid 0x50000 mhartid 0 misa 0x800000000094112f]
set words {0x0914030d 0x10050000 0x260c0001 0x304b0066 0x46180404 0x50000000 0x60000955}

prepare running
k230d_cpu_info
check {$state eq "running" && $halts == 1 && $resumes == 1 && $selected eq "other.cpu"}
check {[lsearch -exact $output "  Model: XuanTie C908"] >= 0}
check {[lsearch -exact $output "  Revision: R0S1P16"] >= 0}
check {[lsearch -exact $output "  Hart: 0, XLEN: 64"] >= 0}
check {[lsearch -exact $output "  MISA extensions: A B C D F I M S U X"] >= 0}
check {[llength [lsearch -all -regexp $output {MCPUID\[[0-6]\]}]] == 7}
check {$index == 3}

prepare halted
k230d_cpu_info
check {$state eq "halted" && $halts == 0 && $resumes == 0 && $selected eq "other.cpu"}

prepare running
set fail_read 1
check {[catch {k230d_cpu_info} message] == 1}
check {$message eq "injected register read failure"}
check {$state eq "running" && $resumes == 1 && $selected eq "other.cpu"}

prepare reset
check {[catch {k230d_cpu_info}] == 1}
check {$halts == 0 && $resumes == 0 && $selected eq "other.cpu"}

prepare halted
dict set regs marchid 0x8000000000000001
k230d_cpu_info
check {[lsearch -exact $output "  Model: Unknown"] >= 0}
# Version fields must not include the index or unrelated low bits.
check {[k230d_cpuid_revision [dict create 0 0x0914030d 1 0x13a9babc]] eq "R3S42P27"}
check {[k230d_cpuid_revision [dict create 0 0x0914030d 1 0x1fffffff]] eq "R15S63P63"}
check {[k230d_cpuid_revision [dict create 0 0x0914030d 1 0x10000000]] eq "R0S0P0"}
check {[k230d_cpuid_revision [dict create 0 0x0914030d]] eq "Unknown"}
check {[k230d_cpuid_revision [dict create 0 0x0914030e 1 0x10050000]] eq "Unknown"}
check {[k230d_cpuid_revision [dict create 0 0x0914030d 1 0x20050000]] eq "Unknown"}
puts "PASS: CPU identification and revision, MCPUID rotation, target state and error recovery"

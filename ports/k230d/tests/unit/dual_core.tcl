# Run with: openocd -f ports/k230d/tests/unit/dual_core.tcl -c shutdown
source [file join [file dirname [info script]] ../../openocd/dual-core.tcl]

proc echo {message} {}
proc target {command} { return $::selected }
proc targets {name} { set ::selected $name }
proc halt {timeout} { set ::state halted }
proc resume {} { set ::state running }
proc sleep {ms} {}
proc k230d.cpu0 {command} {
    if {$command eq "curstate"} { return $::state }
}
proc k230d.cpu1 {command} {
    if {$command eq "arp_examine"} {
        if {$::fail_examine} { error "injected examine failure" }
        incr ::examined
    } else { return halted }
}
proc k230d_cpu_info {name} { incr ::queries }
proc read_memory {address width count} {
    switch $address {
        0x9110100c { return [list $::reset] }
        0x91100004 { return {0x9900d} }
        default { error "unexpected memory read" }
    }
}
proc write_memory {address width values} {
    if {$address != 0x9110100c || $width != 32} { error "unexpected write" }
    set value [lindex $values 0]
    lappend ::writes $value
    set ::reset [expr {$value & 1}]
    if {!$::reset && $::dm != 0x80000009} { error "reset released before halt armed" }
}
proc riscv {command address args} {
    if {$command eq "dmi_write"} {
        if {$address != 0x410} { error "unexpected DMI write" }
        set ::dm [lindex $args 0]
    } elseif {$address == 0x410} {
        return $::dm
    } elseif {$address == 0x411} {
        if {$::reset || $::fail_halt} { return 0x4030a2 }
        return 0x4003a2
    } else { error "unexpected DMI read" }
}
proc check {condition} {
    if {![uplevel 1 [list expr $condition]]} { error "check failed: $condition" }
}
proc prepare {rst} {
    set ::selected other.cpu
    set ::state running
    set ::reset $rst
    set ::dm 0
    set ::examined 0
    set ::queries 0
    set ::writes {}
    set ::fail_halt 0
    set ::fail_examine 0
}

prepare 1
k230d_connect_cpu1
check {$reset == 1 && $writes eq {} && $dm == 0 && $queries == 0}
check {$state eq "running" && $selected eq "other.cpu"}

prepare 0
k230d_connect_cpu1
check {$writes eq {} && $queries == 1 && $examined == 1}
check {$state eq "running" && $selected eq "other.cpu"}

prepare 1
k230d_connect_cpu1 1
check {$reset == 0 && $writes eq {0x10000} && $queries == 1}
check {$state eq "running" && $selected eq "other.cpu"}

prepare 1
set state halted
k230d_connect_cpu1 1
check {$reset == 0 && $state eq "halted" && $selected eq "other.cpu"}

foreach failure {fail_halt fail_examine} {
    prepare 1
    set $failure 1
    check {[catch {k230d_connect_cpu1 1}] == 1}
    check {$reset == 1 && $dm == 0 && $writes eq {0x10000 0x10001}}
    check {$state eq "running" && $selected eq "other.cpu"}
}
puts "PASS: dual-core opt-in, reset-halt ordering, rollback and CPU0 state"

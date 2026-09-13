# Identification for supported XuanTie RISC-V cores.
proc xuantie_cpuid_revision {cpuid} {
    if {![dict exists $cpuid 0] || ![dict exists $cpuid 1]} {
        return "Unknown"
    }
    set word0 [dict get $cpuid 0]
    set word1 [dict get $cpuid 1]
    if {($word0 >> 28) != 0 || ($word1 >> 28) != 1 || (($word0 & 7) != 5 && ($word0 & 7) != 6)} {
        return "Unknown"
    }
    # MCPUID formats 5 and 6: WORD1[27:24] = R, [23:18] = S, [17:12] = P.
    return [format "R%dS%dP%d" [expr {($word1 >> 24) & 15}] \
        [expr {($word1 >> 18) & 63}] [expr {($word1 >> 12) & 63}]]
}

proc xuantie_print_cpu_info {target regs cpuid} {
    set vendor [dict get $regs mvendorid]
    set arch [dict get $regs marchid]
    set misa [dict get $regs misa]
    set model "Unknown"
    # Match the complete identification words for the two K230D C908 variants, not IDCODE alone.
    if {$vendor == 0x5b7 && $arch == 0x8000000009140d00 &&
        [dict exists $cpuid 0] && ([dict get $cpuid 0] == 0x0914030d || [dict get $cpuid 0] == 0x09140b0d)} {
        set model "XuanTie C908"
    }

    if {$vendor == 0x5b7 && $arch == 0x89180e00 &&
        [dict exists $cpuid 0] && [dict get $cpuid 0] == 0x0918090e} {
        set model "XuanTie C907FDV-rv32"
    }

    set xlen [expr {($misa >> 62) & 3}]
    if {$xlen == 2} {
        set xlen 64
    } elseif {(($misa >> 30) & 3) == 1} {
        set xlen 32
    } else {
        set xlen unknown
    }
    set extensions {}
    for {set bit 0} {$bit < 26} {incr bit} {
        if {$misa & (1 << $bit)} {
            lappend extensions [format %c [expr {65 + $bit}]]
        }
    }

    echo "CPU identification ($target)"
    echo "  Model: $model"
    echo "  Revision: [xuantie_cpuid_revision $cpuid]"
    echo "  Hart: [dict get $regs mhartid], XLEN: $xlen"
    echo "  MISA extensions: [join $extensions { }]"
    foreach name {mvendorid marchid mimpid misa} {
        echo [format "  %-9s: 0x%016x" $name [dict get $regs $name]]
    }
    foreach index [lsort -integer [dict keys $cpuid]] {
        echo [format "  MCPUID\[%d\]: 0x%08x" $index [dict get $cpuid $index]]
    }
}

proc xuantie_cpu_info {target} {
    set previous [target current]
    set running 0
    set rc [catch {
        targets $target
        $target arp_poll
        set state [$target curstate]
        if {$state ne "running" && $state ne "halted"} {
            error "Cannot read CPU identification while target is $state"
        }
        set running [expr {$state eq "running"}]
        if {$running} {
            halt 1000
        }
        set regs [$target get_reg -force {mvendorid marchid mimpid mhartid misa}]
        set cpuid [dict create]
        if {[dict get $regs mvendorid] == 0x5b7} {
            # MCPUID cycles through seven words; bits 31:28 identify each word.
            for {set i 0} {$i < 7} {incr i} {
                set word [dict get [$target get_reg -force csr_mcpuid] csr_mcpuid]
                set index [expr {($word >> 28) & 15}]
                dict set cpuid $index $word
            }
        }
        xuantie_print_cpu_info $target $regs $cpuid
    } message]

    # Reading identification must not leave a running application halted.
    set resume_rc 0
    if {$running} {
        set resume_rc [catch {resume} resume_message]
    }
    targets $previous
    if {$resume_rc} {
        error "CPU information read could not restore execution: $resume_message"
    }
    if {$rc} {
        error $message
    }
}

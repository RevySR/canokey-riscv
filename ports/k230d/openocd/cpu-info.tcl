source [file join [file dirname [info script]] ../../common/openocd/cpu-info.tcl]

proc k230d_cpu_info {{target k230d.cpu0}} {
    xuantie_cpu_info $target
}

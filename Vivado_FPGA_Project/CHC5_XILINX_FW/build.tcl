# build.tcl - one-step build: Vivado project, bitstream and camera bitstream archive
# Copyright (c) 2026 Circuit Valley - Author: Gaurav Singh
# SPDX-License-Identifier: CC-BY-NC-ND-4.0
# https://creativecommons.org/licenses/by-nc-nd/4.0/

package require json

set here  [file normalize [file dirname [info script]]]
set proj  CHC5_XILINX_FW
set clean 0
if {[catch {exec nproc} jobs]} { set jobs 4 }
set jobs [expr {min(8, $jobs)}]

set opts $::argv
for {set i 0} {$i < [llength $opts]} {incr i} {
    switch -- [lindex $opts $i] {
        --jobs  { incr i; set jobs [lindex $opts $i] }
        --clean { set clean 1 }
        --help  {
            puts "usage: vivado -mode batch -source build.tcl \[-tclargs \[--jobs N\] \[--clean\]\]"
            puts "  --jobs N   parallel synthesis/implementation jobs (default $jobs)"
            puts "  --clean    rebuild even if the bitstream is up to date"
            return
        }
        default { error "build.tcl: unknown option '[lindex $opts $i]' (try --help)" }
    }
}

proc step {msg} { puts "\n==== build.tcl: $msg" }
proc fail {msg} { error "build.tcl: $msg" }

set oldcwd [pwd]
cd $here

set xpr [file join $here $proj $proj.xpr]
set cur [current_project -quiet]
if {$cur ne "" && [get_property NAME $cur] eq $proj} {
    step "using the open project"
} elseif {[file exists $xpr]} {
    if {$cur ne ""} { close_project }
    step "opening $proj/$proj.xpr"
    open_project $xpr
} else {
    if {$cur ne ""} { close_project }
    step "creating the project from rebuild.tcl"
    set saved [list $::argc $::argv]
    set ::argc 0
    set ::argv {}
    set ::origin_dir_loc $here
    source [file join $here rebuild.tcl]
    lassign $saved ::argc ::argv
}

set synth [get_runs synth_1]
set impl  [get_runs impl_1]
set done [expr {[get_property STATUS $impl] eq "write_bitstream Complete!" &&
                ![get_property NEEDS_REFRESH $synth] && ![get_property NEEDS_REFRESH $impl]}]
if {$clean || !$done} {
    if {$clean || [get_property NEEDS_REFRESH $synth] || [get_property PROGRESS $synth] ne "100%"} {
        reset_run $synth
    } else {
        reset_run $impl
    }
    step "synthesis, implementation and bitstream ($jobs jobs)"
    launch_runs impl_1 -to_step write_bitstream -jobs $jobs
    wait_on_run impl_1
} else {
    step "bitstream is up to date, not rebuilding (use --clean to force)"
}

if {[get_property PROGRESS $synth] ne "100%" || [get_property STATUS $impl] ne "write_bitstream Complete!"} {
    fail "build failed: synth_1 '[get_property STATUS $synth]', impl_1 '[get_property STATUS $impl]'; see the logs in $proj/$proj.runs/"
}

set top  [get_property TOP [current_fileset]]
set bit  [file join [get_property DIRECTORY $impl] $top.bit]
if {![file exists $bit]} { fail "bitstream not found: $bit" }

set out  [file join $here output]
set work [file join $out work]
file delete -force $work
file mkdir $work [file join $work pkg]

step "converting the bitstream with bootgen"
set bootgen [file join $::env(XILINX_VIVADO) bin bootgen]
if {![file executable $bootgen]} { set bootgen bootgen }
file copy -force $bit [file join $work design.bit]
cd $work
set fh [open bit.bif w]
puts $fh "all:\n\{\n  design.bit\n\}"
close $fh
if {[catch {exec -ignorestderr -- $bootgen -image bit.bif -arch zynq -process_bitstream bin -w on} msg]} {
    cd $oldcwd
    fail "bootgen failed: $msg"
}
set bin [file join $work design.bit.bin]
if {![file exists $bin]} { cd $oldcwd; fail "bootgen produced no design.bit.bin" }
set binsize [file size $bin]
if {$binsize > 8 * 1024 * 1024} { cd $oldcwd; fail "bitstream.bin is $binsize bytes, over the camera's 8 MB limit" }
set fh [open $bin rb]
set head [read $fh 256]
close $fh
if {[string first "\x66\x55\x99\xAA" $head] < 0 && [string first "\xAA\x99\x55\x66" $head] < 0} {
    cd $oldcwd
    fail "bitstream.bin has no Xilinx sync word in its first 256 bytes"
}

step "preparing manifest.json"
set mfile [file join $here manifest.json]
set fh [open $mfile r]
fconfigure $fh -translation binary
set raw [read $fh]
close $fh
if {[regexp {[^\t\n\r\x20-\x7E]} $raw]} { cd $oldcwd; fail "manifest.json contains non-ASCII characters" }
if {[regexp {\\[nrtbfu]} $raw]} { cd $oldcwd; fail "manifest.json has an escape sequence (\\n, \\t, \\u...) in a string; the camera rejects them" }

set date [clock format [clock seconds] -format {%Y-%m-%dT%H:%M:%SZ} -gmt 1]
if {[catch {exec git -C $here rev-parse --short=8 HEAD} sha]} {
    set sha   00000000
    set dirty 1
} else {
    set dirty [expr {[catch {exec git -C $here status --porcelain --untracked-files=no -- .} st] || $st ne ""}]
}
set vver [version -short]

foreach {key pattern value} [list \
        build_date     {("build_date"\s*:\s*)"[^"]*"}     "\"$date\"" \
        git_sha        {("git_sha"\s*:\s*)"[^"]*"}        "\"$sha\"" \
        git_dirty      {("git_dirty"\s*:\s*)[0-9]+}       $dirty \
        vivado_version {("vivado_version"\s*:\s*)"[^"]*"} "\"$vver\""] {
    if {[regsub -all $pattern $raw "\\1$value" raw] != 1} {
        cd $oldcwd
        fail "manifest.json must contain exactly one \"$key\" field"
    }
}

if {[catch {json::json2dict $raw} m]} { cd $oldcwd; fail "manifest.json is not valid JSON: $m" }
proc mget {d path} {
    foreach k [split $path .] {
        if {![dict exists $d $k]} { return -code error "missing \"$path\"" }
        set d [dict get $d $k]
    }
    set t [string trim $d]
    if {[string is double -strict $t]} { return $t }
    return $d
}
set problems {}
foreach path {manifest_kind manifest_version manifest.name manifest.version manifest.description
              manifest.build_date manifest.git_sha manifest.git_dirty fpga.vivado_version} {
    if {[catch {mget $m $path}]} { lappend problems "required field \"$path\" is missing" }
}
if {$problems eq ""} {
    set name [mget $m manifest.name]
    set desc [mget $m manifest.description]
    if {[mget $m manifest_kind] ne "bitstream_v1"} { lappend problems "manifest_kind must be \"bitstream_v1\"" }
    if {![string is double -strict [mget $m manifest_version]] || [mget $m manifest_version] < 1} {
        lappend problems "manifest_version must be a number of at least 1"
    }
    if {![regexp {^[A-Za-z0-9_+][A-Za-z0-9._+-]{0,62}$} $name]} {
        lappend problems "manifest.name \"$name\" must be 1-63 characters of A-Z a-z 0-9 . _ + - and not start with . or -"
    }
    if {[string length [mget $m manifest.version]] > 15} { puts "  note: manifest.version is shown cut to 15 characters" }
    if {[string length $desc] < 5} { lappend problems "manifest.description must be at least 5 characters" }
    if {[string length $desc] > 63} { puts "  note: the camera library shows only the first 63 characters of manifest.description" }
    if {![catch {mget $m fpga.features} f] && [string length $f] > 95} {
        lappend problems "fpga.features is [string length $f] characters; the camera cuts it at 95"
    }
    if {![catch {mget $m fpga.max_res} r] && ![regexp {^[0-9]+x[0-9]+$} $r]} {
        lappend problems "fpga.max_res \"$r\" must be WIDTHxHEIGHT"
    }
    if {![catch {mget $m fpga.bits} b] && (![string is integer -strict $b] || $b < 0 || $b > 32)} {
        lappend problems "fpga.bits must be an integer from 0 to 32"
    }
    if {![catch {mget $m fpga.lanes} l]} {
        foreach n [split [string map {" " ""} $l] ,] {
            if {$n ni {1 2 3 4 8}} { lappend problems "fpga.lanes \"$l\" must be 1, 2, 3, 4, 8 or a comma list of them" ; break }
        }
    }
}
if {[string length $raw] > 16384} { lappend problems "manifest.json is [string length $raw] bytes, over 16384" }
if {$problems ne ""} {
    cd $oldcwd
    fail "manifest.json rejected:\n  - [join $problems "\n  - "]"
}
puts "  $name [mget $m manifest.version], built $date, git $sha[expr {$dirty ? " (dirty)" : ""}], Vivado $vver"

step "packing the archive"
file copy -force $bin [file join $work pkg bitstream.bin]
set fh [open [file join $work pkg manifest.json] w]
fconfigure $fh -translation binary
puts -nonewline $fh $raw
close $fh
set tarball [file join $work $name.tar]
if {[catch {exec tar -C [file join $work pkg] --sort=name --mtime=@0 --owner=0 --group=0 --numeric-owner -cf $tarball .} msg]} {
    cd $oldcwd
    fail "tar failed (GNU tar is required): $msg"
}
if {[catch {exec xz -T1 -e -z -f $tarball} msg]} { cd $oldcwd; fail "xz failed: $msg" }
set archive [file join $out $name.tar.xz]
file rename -force $tarball.xz $archive
cd $oldcwd

set listing [lsort [split [string trim [exec tar -tJf $archive]] "\n"]]
if {$listing ne [list ./ ./bitstream.bin ./manifest.json]} { fail "unexpected archive contents: $listing" }
set asize [file size $archive]
if {$asize > 16 * 1024 * 1024} { fail "archive is $asize bytes, over the camera's 16 MB limit" }
file delete -force $work

step "done"
puts "  archive : $archive ($asize bytes)"
puts "  contents: bitstream.bin ($binsize bytes), manifest.json"
puts "  upload it in the camera's web interface, bitstream library"

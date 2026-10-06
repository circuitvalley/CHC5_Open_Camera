# SPDX-License-Identifier: CC-BY-NC-ND-4.0
#
# sd-boot.cmd - SD card boot script, run by the QSPI u-boot (compiled to chc5sd.scr)
#
# Copyright (c) 2026 Circuit Valley - Author: Gaurav Singh
# https://creativecommons.org/licenses/by-nc-nd/4.0/

setenv _sdvars "BOOT_ORDER BOOT_A_LEFT BOOT_B_LEFT upgrade_available qspi_boot boot_env_gen boot_env_ver chc5_var_reformat chc5_factory_reset chc5_factory_reset_tries chc5_factory_cred"
setenv _sde1 0x03100000
setenv _sde2 0x03200000
setenv _sdq  0x03300000
setenv _sdw  0x03400000
test -n "${kernel_addr_r}" || setenv kernel_addr_r 0x2000000

env export -c -s 0x20000 ${_sdq} ${_sdvars}
env delete -f ${_sdvars}

setenv _sdok1 0
setenv _sdok2 0
setenv _sdf1 0
setenv _sdf2 0
if fatload mmc 0:1 ${_sde1} uboot.env && test "${filesize}" = "20000"; then
	setenv _sdok1 1
	setexpr _sdfa ${_sde1} + 4
	setexpr.b _sdf1 *${_sdfa}
fi
if fatload mmc 0:1 ${_sde2} uboot-redund.env && test "${filesize}" = "20000"; then
	setenv _sdok2 1
	setexpr _sdfa ${_sde2} + 4
	setexpr.b _sdf2 *${_sdfa}
fi
setenv _sdtry "1 2"
if test 0x${_sdf1} -eq 0xff && test 0x${_sdf2} -eq 0x0; then
	setenv _sdtry "2 1"
elif test 0x${_sdf2} -eq 0xff && test 0x${_sdf1} -eq 0x0; then
	setenv _sdtry "1 2"
elif test 0x${_sdf2} -gt 0x${_sdf1}; then
	setenv _sdtry "2 1"
fi
setenv _sdcur 0
for n in ${_sdtry}; do
	if test ${_sdcur} = 0; then
		if test ${n} = 1 && test ${_sdok1} = 1 && env import -d -c ${_sde1} 0x20000 ${_sdvars}; then
			setenv _sdcur 1
			setenv _sdcurflag ${_sdf1}
		elif test ${n} = 2 && test ${_sdok2} = 1 && env import -d -c ${_sde2} 0x20000 ${_sdvars}; then
			setenv _sdcur 2
			setenv _sdcurflag ${_sdf2}
		fi
	fi
done
if test ${_sdcur} = 1 && test ${_sdok2} = 0; then
	echo "CHC5 SD: rebuilding uboot-redund.env from uboot.env"
	fatwrite mmc 0:1 ${_sde1} uboot-redund.env 0x20000
elif test ${_sdcur} = 2 && test ${_sdok1} = 0; then
	echo "CHC5 SD: rebuilding uboot.env from uboot-redund.env"
	fatwrite mmc 0:1 ${_sde2} uboot.env 0x20000
fi
if test ${_sdcur} = 0; then
	echo "CHC5 SD: no valid env on the card - using defaults"
	setenv _sdcur 2
	setenv _sdcurflag 0
fi

setenv _sdgo 1
if test "x${qspi_boot}" = "x1"; then
	echo "CHC5 SD: the SD system asked for QSPI - switching SD boot off"
	setenv _sdgo 0
	env import -d -c ${_sdq} 0x20000 ${_sdvars}
	env delete -f sd_boot
fi

if test ${_sdgo} = 1; then
	test -n "${BOOT_ORDER}"        || setenv BOOT_ORDER "A B"
	test -n "${BOOT_A_LEFT}"       || setenv BOOT_A_LEFT 3
	test -n "${BOOT_B_LEFT}"       || setenv BOOT_B_LEFT 3
	test -n "${upgrade_available}" || setenv upgrade_available 0
	setenv _trial 1
	for slot in ${BOOT_ORDER}; do
		setenv kpart;
		if test "x${slot}" = "xA"; then
			setenv sleft ${BOOT_A_LEFT}; setenv kpart 2; setenv rootpart 6;
		elif test "x${slot}" = "xB"; then
			setenv sleft ${BOOT_B_LEFT}; setenv kpart 3; setenv rootpart 7;
		else
			echo "CHC5 SD: unknown slot ${slot} in BOOT_ORDER - skipping";
		fi;
		if test -n "${kpart}"; then
			if test ${sleft} -gt 0; then
				if test "x${upgrade_available}" = "x1" && test "x${_trial}" = "x1"; then
					setexpr sleft ${sleft} - 1;
					if test "x${slot}" = "xA"; then setenv BOOT_A_LEFT ${sleft}; else setenv BOOT_B_LEFT ${sleft}; fi;
					setenv _sdsave 1;
				else
					setenv _sdsave 0;
				fi;
				if test ${_sdsave} = 1; then
					if test 0x${_sdcurflag} -eq 0xff; then setenv _sdnf 0; else setexpr _sdnf ${_sdcurflag} + 1; fi;
					env export -c -s 0x20000 ${_sdw} ${_sdvars};
					setexpr _sdfa ${_sdw} + 4;
					mw.b ${_sdfa} ${_sdnf};
					if test ${_sdcur} = 1; then
						fatwrite mmc 0:1 ${_sdw} uboot-redund.env 0x20000 && setenv _sdcur 2 && setenv _sdcurflag ${_sdnf};
					else
						fatwrite mmc 0:1 ${_sdw} uboot.env 0x20000 && setenv _sdcur 1 && setenv _sdcurflag ${_sdnf};
					fi;
				fi;
				if part start mmc 0 ${kpart} kstart && part size mmc 0 ${kpart} ksize; then
					setenv bootargs console=ttyPS0,115200 consoleblank=0 earlycon rootwait ro rootfstype=squashfs root=/dev/mmcblk0p${rootpart} rauc.slot=${slot};
					echo "CHC5 SD: booting slot ${slot} (kernel mmc0:${kpart}, root=/dev/mmcblk0p${rootpart}, ${sleft} left, trial=${_trial} probation=${upgrade_available})";
					if mmc read ${kernel_addr_r} ${kstart} ${ksize}; then
						bootm ${kernel_addr_r};
					fi;
				fi;
				echo "CHC5 SD: slot ${slot} failed to load - trying next";
			else
				echo "CHC5 SD: slot ${slot} has no attempts left - skipping";
			fi;
			setenv _trial 0;
		fi;
	done
	echo "CHC5 SD: no bootable slot on the card - booting QSPI (SD boot stays on)"
	if test "x${BOOT_ORDER}" != "xA B"; then
		setenv BOOT_ORDER "A B"
		if test 0x${_sdcurflag} -eq 0xff; then setenv _sdnf 0; else setexpr _sdnf ${_sdcurflag} + 1; fi
		env export -c -s 0x20000 ${_sdw} ${_sdvars}
		setexpr _sdfa ${_sdw} + 4
		mw.b ${_sdfa} ${_sdnf}
		if test ${_sdcur} = 1; then
			fatwrite mmc 0:1 ${_sdw} uboot-redund.env 0x20000
		else
			fatwrite mmc 0:1 ${_sdw} uboot.env 0x20000
		fi
	fi
	env import -d -c ${_sdq} 0x20000 ${_sdvars}
fi

env delete -f _sdvars _sde1 _sde2 _sdq _sdw _sdok1 _sdok2 _sdf1 _sdf2 _sdfa
env delete -f _sdtry _sdcur _sdcurflag _sdgo _sdsave _sdnf _trial sleft
env delete -f kpart rootpart kstart ksize bootargs filesize fileaddr
if test -n "${sd_boot}"; then
	setenv chc5_sdarg chc5.sd=noslot
else
	saveenv
	setenv chc5_sdarg chc5.sd=qspi
fi

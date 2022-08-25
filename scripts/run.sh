#!/bin/sh
# run.sh - wraps over emulators
# Copyright 2021, 2022 The NexNix Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# Finds the argument to a given option
getoptarg()
{
    isdash=$(echo "$1" | awk '/^-/')
    if [ ! -z "$isdash" ]
    then
        echo "option $2 requires an argument" $0
        exit 1
    fi
    # Report it
    echo "$1"
}

# Go through options
while [ $# -gt 0 ]
do
    case $1 in
    -help)
        cat <<HELPEND
$(basename $0) - runs an emulator
Usage - $0 [-help] 
            [-emu EMULATOR] [-disk HARDDISKPATH] [-cdrom CDROMPATH]
            [-floppy FLOPPYPATH] [-usbdisk USBDISK] [-debug] [-kvm]
            [-mem MEGS] [-cpus CPUCOUNT] [-drive DRIVETYPE] [-usbhci HCI]
            [-bus BUS] [-net NETDEV] [-input INPUTDEV] [-display DISPLAYDEV]
            [-sound SOUNDDEV] [-fw FIRMWARE] [-cpu CPU] [-machine MACHINE]
            [-cdromboot]
Valid arguments:
  -help
                        Shows this menu
  -emu EMULATOR
                        Specifies the emulator to use
                        Can be qemu, bochs, or vbox
  -disk HARDDISKPATH
                        Specifies the path to a hard disk image
  -cdrom CDROMPATH
                        Specifies the path to a CDROM image
  -floppy FLOPPYPATH
                        Specifies path to floppy disk
  -usbdisk USBDISK
                        Specifies path to USB drive image
  -debug
                        Specifies debugging with GDB or LLDB should be connected
  -kvm
                        Specifies if KVM should be used
  -mem MEGS
                        Specifies the amount of memory in megabytes
  -cpus CPUCOUNT
                        Specifies the amount of CPUs to use
  -drive DRIVETYPE
                        Specifies the disk system type to use
                        Can be ahci, ata, virtio, scsi, or nvme
  -usbhci HCI
                        Specifies the USB host controller to use
                        Can be xhci, ehci, ohci, or uhci
  -net NETDEV
                        Specifies the network adapter to use
                        Can be e1000, ne2k, pro1000 (vbox only) or pcnet
  -input INPUTDEV
                        Specifies the input device to use
                        Can be usb, ps2, or virtio
  -display DISPLAYDEV
                        Specifies the display device to use
                        Can be vga, vmware, ati, bochs, or virtio
  -sound SOUNDDEV
                        Specifies the sound device to use
                        Can be hda, sb16, or ac97
  -fw FIRMWARETYPE
                        Specifies the firmware type
                        Can be efi, bios or default
  -cpu CPU
                        Specifies the CPU type to use
                        Valid types depend on the emulator
  -machine MACHINE
                        Specifies the machine type
                        Only has meaning for QEMU emulator
  -cdromboot
                        Specifies if system should boot from CDROM
  -floppyboot
                        Specifies if system should boot from a floppy
HELPEND
        exit 0
    ;;
    -emu)
        emulator=$(getoptarg "$2" "$1")
        shift 2
    ;;
    -disk)
        diskpath=$(getoptarg "$2" "$1")
        shift 2
    ;;
    -cdrom)
        cdrompath=$(getoptarg "$2" "$1")
        EMU_CDROM=1
        shift 2
    ;;
    -usbdisk)
        usbpath=$(getoptarg "$2" "$1")
        EMU_USBDRIVE=1
        shift 2
    ;;
    -floppy)
        floppypath=$(getoptarg "$2" "$1")
        EMU_FLOPPY=1
        shift 2
    ;;
    -debug)
        shift
        EMU_DEBUG=1
    ;;
    -kvm)
        shift
        EMU_KVM=1
    ;;
    -mem)
        EMU_MEMCOUNT=$(getoptarg "$2" "$1")
        shift 2
    ;;
    -cpus)
        EMU_CPUCOUNT=$(getoptarg "$2" "$1")
        shift 2
    ;;
    -drive)
        EMU_DRIVETYPE=$(getoptarg "$2" "$1")
        shift 2
    ;;
    -usbhci)
        EMU_USBTYPE=$(getoptarg "$2" "$1")
        shift 2
    ;;
    -net)
        EMU_NETDEV=$(getoptarg "$2" "$1")
        shift 2
    ;;
    -input)
        EMU_INPUTDEV=$(getoptarg "$2" "$1")
        shift 2
    ;;
    -display)
        EMU_DISPLAYTYPE=$(getoptarg "$2" "$1")
        shift 2
    ;;
    -sound)
        EMU_SOUNDDEV=$(getoptarg "$2" "$1")
        shift 2
    ;;
    -fw)
        EMU_FWTYPE=$(getoptarg "$2" "$1")
        shift 2
    ;;
    -cpu)
        EMU_CPU=$(getoptarg "$2" "$1")
        shift 2
    ;;
    -machine)
        EMU_MACHINETYPE=$(getoptarg "$2" "$1")
        shift 2
    ;;
    -cdromboot)
        shift
        EMU_CDROMBOOT=1
    ;;
    -floppyboot)
        shift
        EMU_FLOPPYBOOT=1
    ;;
    *)
        echo "$0: invalid argument $1"
        exit 1
    esac
done

[ -z $emulator ] && emulator=qemu
[ -z "$diskpath" ] && diskpath=""
[ -z "$cdrompath" ] && cdrompath=""
[ -z "$usbpath" ] && usbpath=""
[ -z "$floppypath" ] && floppypath=""

if [ "$emulator" = "qemu" ]
then
    QEMUARGS="${QEMUARGS} -serial stdio"
    [ "$EMU_DEBUG" = "1" ] && QEMUARGS="${QEMUARGS} -S -s"
    [ "$EMU_KVM" = "1" ] && QEMUARGS="${QEMUARGS} -enable-kvm"
    if [ "$NNTARGETCONF" = "acpi-up" ]
    then
        # Prepare defaults
        [ -z "$EMU_MEMCOUNT" ] && EMU_MEMCOUNT=1024
        [ -z "$EMU_DRIVETYPE" ] && EMU_DRIVETYPE="ata"
        EMU_BUSTYPE="pci"
        [ -z "$EMU_USBTYPE" ] && EMU_USBTYPE="ehci"
        [ -z "$EMU_NETDEV" ] && EMU_NETDEV="ne2k"
        [ -z "$EMU_INPUTDEV" ] && EMU_INPUTDEV="usb"
        [ -z "$EMU_DISPLAYTYPE" ] && EMU_DISPLAYTYPE="bochs"
        [ -z "$EMU_SOUNDDEV" ] && EMU_SOUNDDEV="ac97"
        [ -z "$EMU_FWTYPE" ] && EMU_FWTYPE="bios"
        [ -z "$EMU_CPU" ] && EMU_CPU="athlon"
        [ -z "$EMU_MACHINETYPE" ] && EMU_MACHINETYPE="pc"
        [ -z "$EMU_CDROM" ] && EMU_CDROM=0
        [ -z "$EMU_FLOPPY" ] &&  EMU_FLOPPY=0
        [ -z "$EMU_FLOPPYBOOT" ] && EMU_FLOPPYBOOT=0
        [ -z "$EMU_USBDRIVE" ] && EMU_USBDRIVE=0
        EMU_SMP=0
        EMU_CDROMBOOT=0
    elif [ "$NNTARGETCONF" = "acpi" ]
    then
        # Prepare defaults
        if [ "$NNARCH" = "x86_64" ]
        then
            [ -z "$EMU_MEMCOUNT" ] && EMU_MEMCOUNT=3072
            [ -z "$EMU_CPUCOUNT" ] && EMU_CPUCOUNT=4
            [ -z "$EMU_DRIVETYPE" ] && EMU_DRIVETYPE="sata"
            EMU_BUSTYPE="pcie"
            [ -z "$EMU_USBTYPE" ] && EMU_USBTYPE="xhci"
            [ -z "$EMU_NETDEV" ] && EMU_NETDEV="e1000"
            [ -z "$EMU_INPUTDEV" ] && EMU_INPUTDEV="usb"
            [ -z "$EMU_DISPLAYTYPE" ] && EMU_DISPLAYTYPE="bochs"
            [ -z "$EMU_SOUNDDEV" ] && EMU_SOUNDDEV="hda"
            [ -z "$EMU_FWTYPE" ] && EMU_FWTYPE="uefi"
            [ -z "$EMU_CPU" ] && EMU_CPU="max"
            [ -z "$EMU_MACHINETYPE" ] && EMU_MACHINETYPE="q35"
            [ -z "$EMU_CDROM" ] && EMU_CDROM=0
            [ -z "$EMU_FLOPPY" ] &&  EMU_FLOPPY=0
            [ -z "$EMU_FLOPPYBOOT" ] && EMU_FLOPPYBOOT=0
            [ -z "$EMU_USBDRIVE" ] && EMU_USBDRIVE=0
        elif [ "$NNARCH" = "i386" ]
        then
            [ -z "$EMU_MEMCOUNT" ] && EMU_MEMCOUNT=1024
            [ -z "$EMU_CPUCOUNT" ] && EMU_CPUCOUNT=2
            [ -z "$EMU_DRIVETYPE" ] && EMU_DRIVETYPE="ata"
            EMU_BUSTYPE="pci"
            [ -z "$EMU_USBTYPE" ] && EMU_USBTYPE="ehci"
            [ -z "$EMU_NETDEV" ] && EMU_NETDEV="ne2k"
            [ -z "$EMU_INPUTDEV" ] && EMU_INPUTDEV="usb"
            [ -z "$EMU_DISPLAYTYPE" ] && EMU_DISPLAYTYPE="bochs"
            [ -z "$EMU_SOUNDDEV" ] && EMU_SOUNDDEV="ac97"
            [ -z "$EMU_FWTYPE" ] && EMU_FWTYPE="bios"
            [ -z "$EMU_CPU" ] && EMU_CPU="athlon"
            [ -z "$EMU_MACHINETYPE" ] && EMU_MACHINETYPE="pc"
            [ -z "$EMU_CDROM" ] && EMU_CDROM=0
            [ -z "$EMU_FLOPPY" ] &&  EMU_FLOPPY=0
            [ -z "$EMU_FLOPPYBOOT" ] && EMU_FLOPPYBOOT=0
            [ -z "$EMU_USBDRIVE" ] && EMU_USBDRIVE=0
        fi
        EMU_SMP=1
        [ -z "$EMU_CDROMBOOT" ] && EMU_CDROMBOOT=0
    elif [ "$NNTARGETCONF" = "pnp" ]
    then
        # Prepare defaults
        [ -z "$EMU_MEMCOUNT" ] && EMU_MEMCOUNT=512
        EMU_CPUCOUNT=1
        [ -z "$EMU_DRIVETYPE" ] && EMU_DRIVETYPE=ata
        EMU_BUSTYPE=isa
        EMU_USBTYPE=
        [ -z "$EMU_NETDEV" ] && EMU_NETDEV="ne2k"
        EMU_INPUTDEV="ps2"
        [ -z "$EMU_DISPLAYTYPE" ] && EMU_DISPLAYTYPE="vga"
        [ -z "$EMU_SOUNDDEV" ] && EMU_SOUNDDEV="sb16"
        [ -z "$EMU_FWTYPE" ] && EMU_FWTYPE="bios"
        [ -z "$EMU_CPU" ] && EMU_CPU="pentium"
        EMU_MACHINETYPE="isapc"
        [ -z "$EMU_CDROM" ] && EMU_CDROM=0
        [ -z "$EMU_FLOPPYBOOT" ] && EMU_FLOPPYBOOT=0
        EMU_FLOPPY=1
        EMU_USBDRIVE=0
        EMU_SMP=0
        [ -z "$EMU_CDROMBOOT" ] && EMU_CDROMBOOT=0
        QEMUARGS="${QEMUARGS} -no-acpi"
    elif [ "$NNTARGETCONF" = "mp" ]
    then
        [ -z "$EMU_MEMCOUNT" ] && EMU_MEMCOUNT=1024
        [ -z "$EMU_CPUCOUNT" ] && EMU_CPUCOUNT=2
        [ -z "$EMU_DRIVETYPE" ] && EMU_DRIVETYPE="ata"
        EMU_BUSTYPE=pci
        [ -z "$EMU_USBTYPE" ] && EMU_USBTYPE="ohci"
        [ -z "$EMU_NETDEV" ] && EMU_NETDEV="ne2k"
        [ -z "$EMU_INPUTDEV" ] && EMU_INPUTDEV="usb"
        [ -z "$EMU_DISPLAYTYPE" ] && EMU_DISPLAYTYPE="vga"
        [ -z "$EMU_SOUNDDEV" ] && EMU_SOUNDDEV="ac97"
        [ -z "$EMU_FWTYPE" ] && EMU_FWTYPE="bios"
        [ -z "$EMU_CPU" ] && EMU_CPU="pentium2"
        [ -z "$EMU_FLOPPYBOOT" ] && EMU_FLOPPYBOOT=0
        EMU_MACHINETYPE="pc"
        EMU_FLOPPY=1
        [ -z "$EMU_CDROM" ] && EMU_CDROM=0
        EMU_USBDRIVE=0
        [ -z "$EMU_CDROMBOOT" ] && EMU_CDROMBOOT=0
        EMU_SMP=1
        QEMUARGS="${QEMUARGS} -no-acpi"
    elif [ "$NNTARGETCONF" = "qemu" ]
    then
        [ -z "$EMU_MEMCOUNT" ] && EMU_MEMCOUNT=2048
        [ -z "$EMU_CPUCOUNT" ] && EMU_CPUCOUNT=4
        [ -z "$EMU_DRIVETYPE" ] && EMU_DRIVETYPE="virtio"
        EMU_BUSTYPE=pcie
        [ -z "$EMU_USBTYPE" ] && EMU_USBTYPE="xhci"
        [ -z "$EMU_NETDEV" ] && EMU_NETDEV="e1000"
        [ -z "$EMU_INPUTDEV" ] && EMU_INPUTDEV="usb"
        [ -z "$EMU_DISPLAYTYPE" ] && EMU_DISPLAYTYPE="virtio"
        [ -z "$EMU_SOUNDDEV" ] && EMU_SOUNDDEV="hda"
        EMU_FWTYPE="efi"
        EMU_CPU=max
        EMU_MACHINETYPE="virt"
        EMU_CDROM=0
        EMU_USBDRIVE=1
        EMU_CDROMBOOT=0
        EMU_SMP=1
    fi
    # Build command line
    QEMUARGS="${QEMUARGS} -m ${EMU_MEMCOUNT}M -M ${EMU_MACHINETYPE} -cpu $EMU_CPU"
    if [ "$EMU_SMP" = "1" ]
    then
        QEMUARGS="${QEMUARGS} -smp ${EMU_CPUCOUNT}"
    fi
    if [ ! -z $diskpath ]
    then
        if [ "$EMU_DRIVETYPE" = "sata" ]
        then
            QEMUARGS="${QEMUARGS} -device ich9-ahci"
            QEMUARGS="${QEMUARGS} -drive file=$diskpath,format=raw"
        elif [ "$EMU_DRIVETYPE" = "ata" ]
        then
            if [ "$NNTARGETCONF" = "pnp" ]
            then
                QEMUARGS="${QEMUARGS} -device isa-ide -device ide-hd,drive=hd0"
            else
                QEMUARGS="${QEMUARGS} -device piix4-ide -device ide-hd,drive=hd0"
            fi
            QEMUARGS="${QEMUARGS} -drive file=$diskpath,format=raw,id=hd0,if=none"
        elif [ "$EMU_DRIVETYPE" = "nvme" ]
        then
            QEMUARGS="${QEMUARGS} -device nvme -device nvme-ns,drive=hd0"
            QEMUARGS="${QEMUARGS} -drive file=$diskpath,format=raw,id=hd0"
        elif [ "$EMU_DRIVETYPE" = "virtio" ]
        then
            if [ "$EMU_BUSTYPE" = "virtio" ]
            then
                QEMUARGS="${QEMUARGS} -device virtio-blk-device,drive=hd0"
            else
                QEMUARGS="${QEMUARGS} -device virtio-blk,drive=hd0"
            fi
            QEMUARGS="${QEMUARGS} -drive file=$diskpath,format=raw,id=hd0"
        elif [ "$EMU_DRIVETYPE" = "scsi" ]
        then
            QEMUARGS="${QEMUARGS} -device scsi-block,drive=hd0"
            QEMUARGS="${QEMUARGS} -drive file=$diskpath,format=raw,id=hd0"
        else
            echo "$0: invalid drive type \"$EMU_DRIVETYPE\""
            exit 1
        fi
    fi   

    if [ $EMU_CDROM -eq 1 ]
    then
        if [ "$EMU_DRIVETYPE" = "scsi" ]
        then
            if [ ! -z "$cdrompath" ]
            then
                QEMUARGS="${QEMUARGS} -device scsi-cd,drive=cd0"
                QEMUARGS="${QEMUARGS} -drive file=$cdrompath,format=raw,id=cd0"
            fi
        else
            if [ ! -z "$cdrompath" ]
            then
                QEMUARGS="${QEMUARGS} -cdrom $cdrompath" 
            fi
        fi
    fi
    if [ $EMU_USBDRIVE -eq 1 ]
    then
        if [ ! -z "$usbpath" ]
        then
            QEMUARGS="${QEMUARGS} -device usb-storage,drive=ud0"
            QEMUARGS="${QEMUARGS} -drive file=$usbpath,format=raw,id=ud0"
        fi
    fi
    if [ $EMU_FLOPPY -eq 1 ]
    then
        if [ ! -z "$floppypath" ]
        then
            QEMUARGS="${QEMUARGS} -drive file=$floppypath,format=raw,id=fd0,if=none -device floppy,drive=fd0"
        fi
    fi

    # Set the bus type
    if [ "$EMU_BUSTYPE" = "pcie" ]
    then
        QEMUARGS="${QEMUARGS} -device pcie-root-port -device pcie-pci-bridge"
    elif [ "$EMU_BUSTYPE" = "pci" ] || [ "$EMU_BUSTYPE" = "isa" ] || [ "$EMU_BUSTYPE" = "virtio" ]
    then
        : ;
    else
        echo "$0: invalid bus type \"$EMU_BUSTYPE\""
        exit 1
    fi

    # Set the USB controller
    if [ "$EMU_USBTYPE" = "xhci" ]
    then
        QEMUARGS="${QEMUARGS} -device qemu-xhci -usb"
    elif [ "$EMU_USBTYPE" = "ehci" ]
    then
        QEMUARGS="${QEMUARGS} -device usb-ehci -device pci-ohci -device piix4-usb-uhci -usb"
    elif [ "$EMU_USBTYPE" = "uhci" ]
    then
        QEMUARGS="${QEMUARGS} -device piix4-usb-uhci -usb"
    elif [ "$EMU_USBTYPE" = "ohci" ]
    then
        QEMUARGS="${QEMUARGS} -device pci-ohci -usb"
    else
        if [ ! -z "$EMU_USBTYPE" ]
        then
            echo "$0: invalid USB controller \"$EMU_USBTYPE\""
            exit 1
        fi
    fi

    # Set the network device
    if [ "$EMU_NETDEV" = "e1000" ]
    then
        QEMUARGS="${QEMUARGS} -device e1000,netdev=net0,mac=DA:45:FC:31:AA:09 -netdev user,id=net0"
    elif [ "$EMU_NETDEV" = "pcnet" ]
    then
        QEMUARGS="${QEMUARGS} -device pcnet,netdev=net0,mac=DA:45:FC:31:AA:09 -netdev user,id=net0"
    elif [ "$EMU_NETDEV" = "ne2k" ]
    then
        if [ "$EMU_BUSTYPE" = "isa" ]
        then
            QEMUARGS="${QEMUARGS} -device ne2k_isa,netdev=net0,mac=DA:45:FC:31:AA:09 -netdev user,id=net0"
        elif [ "$EMU_BUSTYPE" = "pci" ]
        then
            QEMUARGS="${QEMUARGS} -device ne2k_pci,netdev=net0,mac=DA:45:FC:31:AA:09 -netdev user,id=net0"
        fi
    else
        echo "$0: invalid network controller \"$EMU_NETDEV\""
        exit 1
    fi

    # Set the input device
    if [ "$EMU_INPUTDEV" = "usb" ]
    then
        QEMUARGS="${QEMUARGS} -device usb-kbd -device usb-mouse"
    elif [ "$EMU_INPUTDEV" = "ps2" ]
    then
        QEMUARGS="${QEMUARGS} -device i8042"
    elif [ "$EMU_INPUTDEV" = "virtio" ]
    then
        if [ "$EMU_BUSTYPE" = "pci" ] || [ "$EMU_BUSTYPE" = "pcie" ]
        then
            QEMUARGS="${QEMUARGS} -device virtio-mouse -device virtio-keyboard"
        elif [ "$EMU_BUSTYPE" = "virtio" ]
        then
            QEMUARGS="${QEMUARGS} -device virtio-mouse-device -device virtio-keyboard-device"
        fi
    else
        echo "$0: invalid input device \"$EMU_INPUTDEV\""
        exit 1
    fi

    # Set the sound device
    if [ "$EMU_SOUNDDEV" = "hda" ]
    then
        QEMUARGS="${QEMUARGS} -device ich9-intel-hda -audiodev id=alsa,driver=alsa -device hda-duplex,audiodev=als"
    elif [ "$EMU_SOUNDDEV" = "ac97" ]
    then
        QEMUARGS="${QEMUARGS} -device ac97,audiodev=alsa -audiodev id=alsa,driver=alsa"
    elif [ "$EMU_SOUNDDEV" = "sb16" ]
    then
        QEMUARGS="${QEMUARGS} -device sb16,audiodev=alsa -audiodev id=alsa,driver=alsa"
    else
        echo "$0: invalid sound device \"$EMU_SOUNDDEV\""
        exit 1
    fi

    # Set the display type
    if [ "$EMU_DISPLAYTYPE" = "bochs" ]
    then
        QEMUARGS="${QEMUARGS} -device bochs-display -vga none"
    elif [ "$EMU_DISPLAYTYPE" = "vmware" ]
    then
        QEMUARGS="${QEMUARGS} -device vmware-svga"
    elif [ "$EMU_DISPLAYTYPE" = "virtio" ]
    then
        if [ "$EMU_BUSTYPE" = "pcie" ] || [ "$EMU_BUSTYPE" = "pci" ]
        then
            QEMUARGS="${QEMUARGS} -device virtio-gpu -vga none"
        elif [ "$EMU_BUSTYPE" = "virtio" ]
        then
            QEMUARGS="${QEMUARGS} -device virtio-gpu-device -vga none"
        fi
    elif [ "$EMU_DISPLAYTYPE" = "ati" ]
    then
        QEMUARGS="${QEMUARGS} -device ati-vga"
    elif [ "$EMU_DISPLAYTYPE" = "vga" ]
    then
        if [ "$NNTARGETCONF" = "pnp" ]
        then
            QEMUARGS="${QEMUARGS} -device isa-cirrus-vga"
        else
            QEMUARGS="${QEMUARGS} -device VGA"
        fi
    fi

    # Set the firmware images
    if [ "$EMU_FWTYPE" = "efi" ]
    then
        # Figure out the UEFI architecture
        if [ "$NNARCH" = "x86_64" ]
        then
            EFIARCH=X64
        elif [ "$NNARCH" = "i386" ]
        then
            EFIARCH=IA32
        else
            EFIARCH=${NNARCH}-
        fi
        # Add it to the command line
        QEMUARGS="${QEMUARGS} -drive if=pflash,file=$NNBUILDROOT/tools/firmware/EFIFW-$EFIARCH$NNBOARD.fd,format=raw,unit=0 \
                    -drive if=pflash,file=$NNBUILDROOT/tools/firmware/EFIFW_VARS-$EFIARCH$NNBOARD.fd,format=raw,unit=1"
    fi

    # Set the boot device
    if [ $EMU_CDROMBOOT -eq 1 ]
    then
        QEMUARGS="${QEMUARGS} -boot d"
    elif [ $EMU_FLOPPYBOOT -eq 1 ]
    then
        QEMUARGS="${QEMUARGS} -boot a"
    else
        QEMUARGS="${QEMUARGS} -boot c"
    fi
    QEMUARGS="${QEMUARGS} $EMU_EXTRAARGS"
    # Run QEMU
    qemu-system-$NNARCH $QEMUARGS
    exit $?
elif [ "$emulator" = "bochs" ]
then
    cd $NNCONFROOT
    # TODO: add networking and sound card support
    if [ "$NNBOARD" != "pc" ]
    then
        echo "$0: bochs is not supported for architecture $NNTARGET"
        exit 1
    fi
    # Begin writing out the configuration file
    if [ "$NNTARGETCONF" = "acpi" ] || [ "$NNTARGETCONF" = "mp" ] || \
       [ "$NNTARGETCONF" = "acpi-up" ]
    then
        [ -z "$EMU_MEMCOUNT" ] && EMU_MEMCOUNT=2048
        EMU_BUSTYPE="pci"
        if [ "$NNTARGETCONF" = "acpi" ]
        then
            [ -z "$EMU_USBTYPE" ] && EMU_USBTYPE="ehci"
            [ -z "$EMU_CPU" ] && EMU_CPU="ryzen"
            [ -z "$EMU_CPUCOUNT" ] && EMU_CPUCOUNT=4
        else
            [ -z "$EMU_USBTYPE" ] && EMU_USBTYPE="uhci"
            [ -z "$EMU_CPU" ] && EMU_CPU="p3_katmai"
            [ -z "$EMU_CPUCOUNT" ] && EMU_CPUCOUNT=1
        fi
        [ -z "$EMU_CDROM" ] && EMU_CDROM=0
        [ -z "$EMU_FLOPPY" ] &&  EMU_FLOPPY=0
        [ -z "$EMU_FLOPPYBOOT" ] && EMU_FLOPPYBOOT=0
        [ -z "$EMU_INPUTDEV" ] && EMU_INPUTDEV="usb"
        if [ "$NNTARGETCONF" = "acpi-up" ]
        then
            EMU_SMP=0
        else
            EMU_SMP=1
        fi
        # Write out BIOS line
        echo 'romimage: file=$BXSHARE/BIOS-bochs-latest,options=fastboot' > bochsrc.txt
    elif [ "$NNTARGETCONF" = "pnp" ]
    then
        [ -z "$EMU_MEMCOUNT" ] && EMU_MEMCOUNT=512
        [ -z "$EMU_CPUCOUNT" ] && EMU_CPUCOUNT=1
        EMU_BUSTYPE="isa"
        EMU_USBTYPE=
        [ -z "$EMU_CPU" ] && EMU_CPU="pentium"
        [ -z "$EMU_CDROM" ] && EMU_CDROM=0
        [ -z "$EMU_FLOPPY" ] &&  EMU_FLOPPY=1
        [ -z "$EMU_FLOPPYBOOT" ] && EMU_FLOPPYBOOT=0
        [ -z "$EMU_INPUTDEV" ] && EMU_INPUTDEV="ps2"
        EMU_SMP=0
        # Write out BIOS line
        echo 'romimage: file=$BXSHARE/BIOS-bochs-legacy,options=fastboot' > bochsrc.txt
    fi
    # Write out base stuff
    echo 'vgaromimage: file=$BXSHARE/VGABIOS-lgpl-latest' >> bochsrc.txt
    echo "log: bochslog.txt" >> bochsrc.txt
    echo "display_library: x" >> bochsrc.txt
    echo "magic_break: enabled=1" >> bochsrc.txt
    echo "megs: $EMU_MEMCOUNT" >> bochsrc.txt
    echo "clock: sync=realtime" >> bochsrc.txt
    # Create CMOS RAM image
    dd if=/dev/zero of=cmos.img bs=128 count=1 > /dev/null 2>&1
    # Write out CPU stuff
    echo "cpu: count=$EMU_CPUCOUNT, model=$EMU_CPU, ips=10000000" >> bochsrc.txt
    # Configure hard disk
    if [ ! -z $diskpath ]
    then
        echo "ata0-master: type=disk, path=$diskpath, mode=flat, translation=auto" \
            >> bochsrc.txt
    fi
    if [ "$EMU_CDROM" = "1" ]
    then
        echo "ata0-slave: type=cdrom, path=$cdrompath, status=inserted, translation=lba" >> bochsrc.txt
    fi
    if [ "$EMU_FLOPPY" = "1" ]
    then
        echo "floppya: 1_44=$floppypath, status=inserted" >> bochsrc.txt
    fi

    # Start working on PCI stuff
    if [ "$NNTARGETCONF" = "acpi" ] || [ "$NNTARGETCONF" = "acpi-up" ]
    then
        pciopt="pci: enabled=1, chipset=i440fx"
    elif [ "$NNTARGETCONF" = "mp" ]
    then
        pciopt="pci: enabled=1, chipset=i440fx, advopts=\"noacpi,nohpet\""
    elif [ "$NNTARGETCONF" = "pnp" ]
    then
        pciopt="pci: enabled=0"
    fi

    echo "$pciopt" >> bochsrc.txt

    # Check if USB input is desired
    if [ "$EMU_INPUTDEV" = "usb" ]
    then
        echo "usb_$EMU_USBTYPE: port1=mouse, port2=keyboard" >> bochsrc.txt
    elif [ "$EMU_INPUTDEV" = "ps2" ]
    then
        : ;
    else
        echo "$0: invalid input device $EMU_INPUTDEV"
    fi

    if [ "$EMU_CDROMBOOT" = "1" ]
    then
        echo "boot: cdrom, disk" >> bochsrc.txt
    elif [ "$EMU_FLOPPYBOOT" = "1" ]
    then
        echo "boot: floppy, disk" >> bochsrc.txt
    else
        echo "boot: disk, cdrom, floppy" >> bochsrc.txt
    fi
    # Run bochs
    bochs -q -f bochsrc.txt
    echo $?
# TODO: Add floppy boot support
elif [ "$emulator" = "vbox" ]
then
    if [ "$NNBOARD" != "pc" ]
    then
        echo "$0: virtualbox not supported on architecture $NNARCH"
    fi
    # Check if the virutal machine needs to be created
    docreate=$(VBoxManage list vms | grep "NexNix")
    if [ -z "$docreate" ]
    then
        # Create it
        VBoxManage createvm --name "NexNix" --register
    fi
    # Setup default settings
    if [ "$NNTARGETCONF" = "acpi" ] || [ "$NNTARGETCONF" = "acpi-up" ]
    then
        # Prepare defaults
        if [ "$NNARCH" = "i386" ]
        then
            [ -z "$EMU_MEMCOUNT" ] && EMU_MEMCOUNT=1024
            [ -z "$EMU_CPUCOUNT" ] && EMU_CPUCOUNT=2
            [ -z "$EMU_DRIVETYPE" ] && EMU_DRIVETYPE="ata"
            [ -z "$EMU_USBTYPE" ] && EMU_USBTYPE="ehci"
            [ -z "$EMU_NETDEV" ] && EMU_NETDEV="pcnet"
            [ -z "$EMU_INPUTDEV" ] && EMU_INPUTDEV="usb"
            [ -z "$EMU_DISPLAYTYPE" ] && EMU_DISPLAYTYPE="vmware"
            [ -z "$EMU_SOUNDDEV" ] && EMU_SOUNDDEV="ac97"
            [ -z "$EMU_FWTYPE" ] && EMU_FWTYPE="bios"
            [ -z "$EMU_CDROM" ] && EMU_CDROM=0
            [ -z "$EMU_FLOPPY" ] &&  EMU_FLOPPY=0
            [ -z "$EMU_FLOPPYBOOT" ] &&  EMU_FLOPPYBOOT=0
        elif [ "$NNARCH" = "x86_64" ]
        then
            [ -z "$EMU_MEMCOUNT" ] && EMU_MEMCOUNT=3072
            [ -z "$EMU_CPUCOUNT" ] && EMU_CPUCOUNT=4
            [ -z "$EMU_DRIVETYPE" ] && EMU_DRIVETYPE="sata"
            [ -z "$EMU_USBTYPE" ] && EMU_USBTYPE="xhci"
            [ -z "$EMU_NETDEV" ] && EMU_NETDEV="pcnet"
            [ -z "$EMU_INPUTDEV" ] && EMU_INPUTDEV="usb"
            [ -z "$EMU_DISPLAYTYPE" ] && EMU_DISPLAYTYPE="vmware"
            [ -z "$EMU_SOUNDDEV" ] && EMU_SOUNDDEV="hda"
            [ -z "$EMU_FWTYPE" ] && EMU_FWTYPE="bios"
            [ -z "$EMU_CDROM" ] && EMU_CDROM=0
            [ -z "$EMU_FLOPPY" ] &&  EMU_FLOPPY=0
            [ -z "$EMU_FLOPPYBOOT" ] &&  EMU_FLOPPYBOOT=0
        fi
        if [ "$NNTARGETCONF" = "acpi" ]
        then
            EMU_SMP=1
        else
            EMP_SMP=0
        fi
        EMU_CDROMBOOT=0
        # Set base settings for this board
        if [ "$NNARCH" = "x86_64" ]
        then
            VBoxManage modifyvm "NexNix" --ostype Other_64
            VBoxManage modifyvm "NexNix" --acpi on --hpet on \
                 --apic on --pae on --x2apic on --largepages on \
                 --biosapic x2apic
        elif [ "$NNARCH" = "i386" ]
        then
            VBoxManage modifyvm "NexNix" --ostype Other
            VBoxManage modifyvm "NexNix" --acpi on --apic on --pae on
        fi
    elif [ "$NNTARGETCONF" = "mp" ]
    then
        [ -z "$EMU_MEMCOUNT" ] && EMU_MEMCOUNT=1024
        [ -z "$EMU_CPUCOUNT" ] && EMU_CPUCOUNT=2
        [ -z "$EMU_DRIVETYPE" ] && EMU_DRIVETYPE="ata"
        [ -z "$EMU_USBTYPE" ] && EMU_USBTYPE="ohci"
        [ -z "$EMU_NETDEV" ] && EMU_NETDEV="pcnet"
        [ -z "$EMU_INPUTDEV" ] && EMU_INPUTDEV="ps2"
        [ -z "$EMU_DISPLAYTYPE" ] && EMU_DISPLAYTYPE="vga"
        [ -z "$EMU_SOUNDDEV" ] && EMU_SOUNDDEV="ac97"
        [ -z "$EMU_FLOPPYBOOT" ] &&  EMU_FLOPPYBOOT=0
        [ -z "$EMU_CDROM" ] && EMU_CDROM=0
        [ -z "$EMU_CDROMBOOT" ] && EMU_CDROMBOOT=0
        [ -z "$EMU_SMP" ] && EMU_SMP=1
        if [ "$NNARCH" = "x86_64" ]
        then
            VBoxManage modifyvm "NexNix" --ostype Other_64
        elif [ "$NNARCH" = "i386" ]
        then
            VBoxManage modifyvm "NexNix" --ostype Other
        fi
        VBoxManage modifyvm "NexNix" --apic on --pae on --largepages on \
                 --biosapic apic --acpi off --hpet off --x2apic off
    elif [ "$NNTARGETCONF" = "pnp" ]
    then
        [ -z "$EMU_MEMCOUNT" ] && EMU_MEMCOUNT=512
        [ -z "$EMU_CPUCOUNT" ] && EMU_CPUCOUNT=2
        [ -z "$EMU_DRIVETYPE" ] && EMU_DRIVETYPE="ata"
        [ -z "$EMU_USBTYPE" ] && EMU_USBTYPE="ohci"
        [ -z "$EMU_NETDEV" ] && EMU_NETDEV="pcnet"
        [ -z "$EMU_INPUTDEV" ] && EMU_INPUTDEV="ps2"
        [ -z "$EMU_DISPLAYTYPE" ] && EMU_DISPLAYTYPE="vga"
        [ -z "$EMU_SOUNDDEV" ] && EMU_SOUNDDEV="ac97"
        [ -z "$EMU_FLOPPYBOOT" ] &&  EMU_FLOPPYBOOT=0
        [ -z "$EMU_CDROM" ] && EMU_CDROM=0
        [ -z "$EMU_CDROMBOOT" ] && EMU_CDROMBOOT=0
        [ -z "$EMU_SMP" ] && EMU_SMP=1
        if [ "$NNARCH" = "x86_64" ]
        then
            VBoxManage modifyvm "NexNix" --ostype Other_64
        elif [ "$NNARCH" = "i386" ]
        then
            VBoxManage modifyvm "NexNix" --ostype Other
        fi
        VBoxManage modifyvm "NexNix" --apic on --pae on --largepages on \
                 --biosapic apic --acpi off --hpet off --x2apic off
    fi
    # Setup base global settings
    VBoxManage modifyvm "NexNix" --cpus $EMU_CPUCOUNT --memory ${EMU_MEMCOUNT}
    if [ "$NNARCH" = "i386" ] && [ "$EMU_FWTYPE" = "efi" ]
    then
        VBoxManage modifyvm "NexNix" --firmware efi32
    elif [ "$NNARCH" = "x86_64" ] && [ "$EMU_FWTYPE" = "efi" ]
    then
        VBoxManage modifyvm "NexNix" --firmware efi64
    else
        VBoxManage modifyvm "NexNix" --firmware bios
    fi

    # Set graphics controller
    if [ "$EMU_DISPLAYTYPE" = "vmware" ]
    then
        VBoxManage modifyvm "NexNix" --graphicscontroller vmsvga
    elif [ "$EMU_DISPLAYTYPE" = "vga" ]
    then
        VBoxManage modifyvm "NexNix" --graphicscontroller vboxvga
    else
        echo "$0: invalid display type $EMU_DISPLAYTYPE"
        exit 1
    fi

    # Setup networking
    VBoxManage modifyvm "NexNix" --nic1 nat
    if [ "$EMU_NETDEV" = "pcnet" ]
    then
        VBoxManage modifyvm "NexNix" --nictype1 Am79C973
    elif [ "$EMU_NETDEV" = "pro1000" ]
    then
        VBoxManage modifyvm "NexNix" --nictype1 82540EM
    elif [ "$EMU_NETDEV" = "virtio" ]
    then
        VBoxManage modifyvm "NexNix" --nictype1 virtio-net
    else
        echo "$0: invalid network adapter $EMU_NETDEV"
        exit 1
    fi

    # Setup USB HCI
    if [ "$EMU_USBTYPE" = "xhci" ]
    then
        VBoxManage modifyvm "NexNix" --usbxhci on
    elif [ "$EMU_USBTYPE" = "ehci" ]
    then
        VBoxManage modifyvm "NexNix" --usbehci on
    elif [ "$EMU_USBTYPE" = "ohci" ]
    then
        VBoxManage modifyvm "NexNix" --usbohci on
    else
        echo "$0: invalid USB HCI $EMU_USBTYPE"
        exit 1
    fi

    # Setup input device
    if [ "$EMU_INPUTDEV" = "usb" ]
    then
        VBoxManage modifyvm "NexNix" --mouse usb --keyboard usb
    elif [ "$EMU_INPUTDEV" = "ps2" ]
    then
        VBoxManage modifyvm "NexNix" --mouse ps2 --keyboard ps2
    fi

    # Setup audio device
    VBoxManage modifyvm "NexNix" --audio pulse --audiocontroller $EMU_SOUNDDEV --audioout on

    # Add serial port
    VBoxManage modifyvm "NexNix" --uart1 0x3F8 4 --uartmode1 file serial.txt

    # Get virtual disk name
    vboxdiskpath=$(echo "$diskpath" | sed 's/\.img/\.vdi/')

    # Delete old storage stuff
    if [ ! -z "$docreate" ] && [ ! -z "$diskpath" ]
    then
        VBoxManage closemedium disk $vboxdiskpath > /dev/null 2>&1
    fi
    VBoxManage storagectl "NexNix" --name "NexNix-storage" --remove

    # Setup storage device
    if [ "$EMU_DRIVETYPE" = "ata" ]
    then
        VBoxManage storagectl "NexNix" --name "NexNix-storage" --add ide \
                    --controller PIIX4 --portcount 2 --bootable on
    elif [ "$EMU_DRIVETYPE" = "sata" ] 
    then
        VBoxManage storagectl "NexNix" --name "NexNix-storage" --add sata \
                    --controller IntelAhci --portcount 2 --bootable on
    elif [ "$EMU_DRIVETYPE" = "nvme" ]
    then
        VBoxManage storagectl "NexNix" --name "NexNix-storage" --add pcie \
                    --controller NVMe --portcount 1 --bootable on
    elif [ "$EMU_DRIVETYPE" = "scsi" ]
    then
        VBoxManage storagectl "NexNix" --name "NexNix-storage" --add scsi \
                    --controller BusLogic --portcount 2 --bootable on
    elif [ "$EMU_DRIVETYPE" = "virtio" ]
    then
        VBoxManage storagectl "NexNix" --name "NexNix-storage" --add pcie \
                    --controller VirtIO --portcount 2 --bootable on
    fi
    # Add mediums
    if [ ! -z $diskpath ]
    then
        rm -f $vboxdiskpath
        VBoxManage convertfromraw $diskpath $vboxdiskpath --format VDI
        VBoxManage storageattach "NexNix" --storagectl "NexNix-storage" \
                    --medium $vboxdiskpath --type hdd --port 0 --device 0
    fi
    if [ "$EMU_CDROM" = "1" ]
    then
        VBoxManage storageattach "NexNix" --storagectl "NexNix-storage" \
                    --medium $cdrompath --type dvddrive --port 1 --device 0
    fi

    if [ "$EMU_FLOPPY" = "1" ]
    then
        if [ ! -z "$docreate" ]
        then
            VBoxManage closemedium $floppypath
            VBoxManage storagectl "NexNix" --name "NexNix-floppy" --remove
        fi
        VBoxManage storagectl "NexNix" --name "NexNix-floppy" --add floppy \
                    --controller I82078 --portcount 1 --bootable on
        VBoxManage storageattach "NexNix" --storagectl "NexNix-storage" \
                    --medium $floppypath --type fdd
    fi

    # Set boot order
    if [ "$EMU_CDROMBOOT" = "1" ]
    then
        VBoxManage modifyvm "NexNix" --boot1 dvd --boot2 disk
    elif [ "$EMU_FLOPPYBOOT" = "1" ]
    then
        VBoxManage modifyvm "NexNix" --boot1 floppy --boot2 disk
    else
        VBoxManage modifyvm "NexNix" --boot1 disk --boot2 dvd
    fi

    # Start it up
    VBoxManage startvm --putenv VBOX_DBG_GUI_ENABLED=true "NexNix"
elif [ "$emulator" = "simnow" ]
then
    if [ "$NNBOARD" != "pc" ]
    then
        panic "simnow not supported on architecture $NNARCH"
    fi
    if [ -z "$EMU_SIMNOWPATH" ]
    then
        panic "environment variable EMU_SIMNOWPATH must be set"
    fi
    panic "SimNow support incomplete"
fi

#!/bin/bash

readonly CONST_QEMU_BIN_I386="qemu-system-i386"
readonly CONST_QEMU_BIN_X86_64="qemu-system-x86_64"
readonly CONST_QEMU_MACHINE_PC="pc"
readonly CONST_QEMU_MACHINE_PC_KVM="pc,accel=kvm"
readonly CONST_QEMU_MIN_BIOS_CPU="486"
readonly CONST_QEMU_MIN_EFI_CPU="pentium2"
readonly CONST_QEMU_DEFAULT_RAM="64M"
readonly CONST_QEMU_BIOS_PC=""
readonly CONST_QEMU_BIOS_EFI="${OVMF:-/usr/share/ovmf/ia32/OVMF.fd}"
readonly CONST_QEMU_DEFAULT_BOOT_DEVICE="-drive driver=raw,node-name=disk,file.driver=file,file.filename=hhuOS.img"
readonly CONST_QEMU_ARGS="-vga std -monitor stdio"

QEMU_BIN="${CONST_QEMU_BIN_I386}"
QEMU_MACHINE="${CONST_QEMU_MACHINE_PC}"
QEMU_BIOS="${CONST_QEMU_BIOS_EFI}"
QEMU_RAM="${CONST_QEMU_DEFAULT_RAM}"
QEMU_CPU="${CONST_QEMU_MIN_EFI_CPU}"
QEMU_BOOT_DEVICE="${CONST_QEMU_DEFAULT_BOOT_DEVICE}"
QEMU_ARGS="${CONST_QEMU_ARGS}"

QEMU_GDB_PORT=""

check_file() {
  local file=$1

  if [ ! -f "$file" ]; then
    printf "File '%s' does not exist!\\n" "${file}"
    exit 1
  fi
}

parse_file() {
  local path=$1
  
  if [[ $path == *.iso ]]; then
    QEMU_BOOT_DEVICE="-cdrom ${path}"
  elif [[ $path == *.img ]]; then
    QEMU_BOOT_DEVICE="-drive driver=raw,node-name=disk,file.driver=file,file.filename=${path}"
  else
    printf "Invalid file '%s'!\\n" "${path}"
    exit 1
  fi
  
  check_file $path
}

parse_architecture() {
  local architecture=$1

  if [ "${architecture}" == "i386" ] || [ "${architecture}" == "x86" ]; then
    QEMU_BIN="${CONST_QEMU_BIN_I386}"
  elif [ "${architecture}" == "x86_64" ] || [ "${architecture}" == "x64" ]; then
    QEMU_BIN="${CONST_QEMU_BIN_X86_64}"
  else
    printf "Invalid architecture '%s'!\\n" "${architecture}"
    exit 1
  fi
}

parse_machine() {
  local machine=$1

  if [ "${machine}" == "pc" ]; then
    QEMU_MACHINE="${CONST_QEMU_MACHINE_PC}"
  elif [ "${machine}" == "pc-kvm" ]; then
    QEMU_MACHINE="${CONST_QEMU_MACHINE_PC_KVM}"
  else
    printf "Invalid machine '%s'!\\n" "${machine}"
    exit 1
  fi
}

parse_bios() {
  local bios=$1

  if [ "${bios}" == "bios" ]; then
    QEMU_BIOS="${CONST_QEMU_BIOS_PC}"
    QEMU_CPU="${CONST_QEMU_MIN_BIOS_CPU}"
  elif [ "${bios}" == "uefi" ]; then
    QEMU_BIOS="${CONST_QEMU_BIOS_EFI}"
    QEMU_CPU="${CONST_QEMU_MIN_EFI_CPU}"
  else
    printf "Invalid BIOS '%s'!\\n" "${machine}"
    exit 1
  fi
}

parse_ram() {
  local memory=$1

  QEMU_RAM="${memory}"
}

parse_debug() {
  local port=$1

  echo "set architecture i386
      set disassembly-flavor intel
      target remote 127.0.0.1:${port}" >/tmp/gdbcommands."$(id -u)"

  QEMU_GDB_PORT="${port}"
}

start_gdb() {
  gdb -x "/tmp/gdbcommands.$(id -u)" "loader/boot/hhuOS.bin"
  exit $?
}

print_usage() {
  printf "Usage: ./run.sh [OPTION...]
    Available options:
    -f, --file
        Set the .iso or .img file, which qemu should boot (Default: hhuOS.img)
    -a, --architecture
        Set the architecture, which qemu should emulate ([i386,x86] | [x86_64,x64]) (Default: i386)
    -m, --machine
        Set the machine profile, which qemu should emulate ([pc] | [pc-kvm]) (Defualt: pc)
    -b, --bios
        Set the BIOS, which qemu should use ([bios | uefi]) (Default: uefi)
    -r, --ram
        Set the amount of ram, which qemu should use (e.g. 256, 1G, ...) (Default: 64M)
    -d, --debug
        Set the port, on which qemu should listen for GDB clients (default: disabled)
    -h, --help
        Show this help message\\n"
}

parse_args() {
  while [ "${1}" != "" ]; do
    local arg=$1
    local val=$2

    case $arg in
    -f | --file)
      parse_file "$val"
      ;;
    -a | --architecture)
      parse_architecture "$val"
      ;;
    -m | --machine)
      parse_machine "$val"
      ;;
    -b | --bios)
      parse_bios "$val"
      ;;
    -r | --ram)
      parse_ram "$val"
      ;;
    -d | --debug)
      parse_debug "$val"
      ;;
    -g | --gdb)
      start_gdb
      ;;
    -h | --help)
      print_usage
      exit 0
      ;;
    *)
      printf "Unknown option '%s'\\n" "${arg}"
      print_usage
      exit 1
      ;;
    esac
    shift 2
  done
}

run_qemu() {
  local command="${QEMU_BIN}"

  if [ -n "${QEMU_MACHINE}" ]; then
    command="${command} -machine ${QEMU_MACHINE}"
  fi

  if [ -n "${QEMU_BIOS}" ]; then
    command="${command} -bios ${QEMU_BIOS}"
  fi

  command="${command} -m ${QEMU_RAM} -cpu ${QEMU_CPU} ${QEMU_BOOT_DEVICE} ${QEMU_ARGS}"
  
  printf "Running: %s\\n" "${command}"

  if [ -n "${QEMU_GDB_PORT}" ]; then
    $command -gdb tcp::"${QEMU_GDB_PORT}" -S &
  else
    $command
  fi
}

parse_args "$@"

QEMU_ARGS="${QEMU_ARGS}"

run_qemu

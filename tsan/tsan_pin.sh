#!/bin/bash

PIN_ROOT=${PIN_ROOT:-$HOME/pin}
TS_ROOT=${TS_ROOT:-.}
TS_VARIANT=-debug

UNAME_OS=`uname -o`
if [ "$UNAME_OS" == "GNU/Linux" ]; then
  PIN_BINARY=${PIN_BINARY:-pin}
  DLL=so
  OS=linux
elif [ "$UNAME_OS" == "Cygwin" ]; then
  PIN_BINARY=${PIN_BINARY:-pin.bat}
  DLL=dll
  OS=windows
fi

export MSM_THREAD_SANITIZER=1


FOLLOW=-follow_execv
PIN_FLAGS=${PIN_FLAGS:-""}

TS_FLAGS="-short_name"
PIN_FLAGS=""

VERBOZE=0

for arg in "$@"; do
  case $arg in
    --trace_children=yes) PIN_FLAGS="$PIN_FLAGS $FOLLOW";;
    --trace-children=yes) PIN_FLAGS="$PIN_FLAGS $FOLLOW";;
    --opt) TS_VARIANT="";;
    --dbg) TS_VARIANT="-debug";;
    --v=[1-9]) VERBOZE=1; TS_FLAGS="$TS_FLAGS $arg";;
    --) shift; break;;
    -*) TS_FLAGS="$TS_FLAGS $arg";;
    *) break;;
  esac
  shift
done


ulimit -c 0 # core make very little sense here

if [ $VERBOZE == "1" ] ; then
  printf "PIN_ROOT   : %s\n" "$PIN_ROOT"
  printf "PIN_BINARY : %s\n" "$PIN_BINARY"
  printf "PIN_FLAGS  : %s\n" "$PIN_FLAGS"
  printf "TS_ROOT    : %s\n" "$TS_ROOT"
  printf "TS_VARIANT : %s\n" "$TS_VARIANT"
  printf "TS_FLAGS   : %s\n" "$TS_FLAGS"
  printf "PARAMS     : %s\n" "$*"
fi

TS_PARAMS="$@"

run() {
  echo $@
  $@
}

run $PIN_ROOT/$PIN_BINARY $PIN_FLAGS \
  -t64 $TS_ROOT/bin/amd64-$OS${TS_VARIANT}-ts_pin.$DLL \
  -t   $TS_ROOT/bin/x86-$OS${TS_VARIANT}-ts_pin.$DLL \
 $TS_FLAGS -- $TS_PARAMS


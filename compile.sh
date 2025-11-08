#!/bin/bash --
set -e
cat <<'ZZZ'

This shell script is used for configuring and compiling Super ZZ Zero the
first time. It will try to determine some things about the system and ask
questions about your configuration, and then will compile Super ZZ Zero.

The compiled programs will be added in the "bin" subdirectory of your home
directory, as "superzz0", "sz0asm", and "sz0music".

ZZZ
read -p 'Push enter to continue or interrupt to cancel:'
echo
echo 'Figuring out configuration ...'
[ -d ~/bin -a -w ~/bin ] || mkdir ~/bin || (echo 'ERROR: Directory ~/bin does not exist or is not writable.'; exit 1)
(which gcc > /dev/null) || (echo 'ERROR: The C compiler does not seem to be installed correctly.'; exit 1)
(which sdl-config > /dev/null) || (echo 'ERROR: SDL1 does not seem to be installed correctly.'; exit 1)
echo 'Compiling maker.c ...'
gcc -s -O2 -o ./maker maker.c
echo 'Done'
echo
# TODO: check whether or not the X window system is available
# TODO: add an option to exclude the editor
declare -a optimization
optimization[0]='None (Debug)'
optimization[1]='Low'
optimization[2]='Mid'
optimization[3]='Hi'
declare -a yesno
yesno[0]='No'
yesno[1]='Yes'
xwindows=1
optim=0
proceed=0
native=0
echo 'Now you must select the configuration options, please.'
while [ $proceed = 0 ]; do
  echo
  echo '  <1> Optimization level:' ${optimization[$optim]}
  echo '  <2> Use X windows functions:' ${yesno[$xwindows]}
  echo '  <3> Generate instructions for local machine:' ${yesno[$native]}
  echo
  echo '  <0> Proceed'
  echo '  <H> Help'
  echo '  <Q> Cancel'
  echo
  read -p 'Push the appropriate number and then enter: '
  case $REPLY in
    0)
      proceed=1
      ;;
    1)
      optim="$(( (optim+1)&3 ))"
      ;;
    2)
      xwindows="$(( xwindows^1 ))"
      ;;
    3)
      native="$(( native^1 ))"
      ;;
    h|H)
      cat <<'ZZZ'
Optimization level:
  Set to "None (Debug)" for debugging; higher optimization will result in
  a smaller executable file and faster execution, but slower compiling.
  (It is possible that some bugs are only apparent at some optimization
  levels, so you might wish to use all of them if you are testing.)
Use X windows functions:
  Turn this off if you are not using the X window system. Also try turning
  it off if compiling display.c fails, or if linking fails due to the use
  of some functions whose name starts with "X" being unavailable.
Generate instructions for local machine:
  If selected, then it will try to optimize the compiled program to run on
  the computer that it is compiled on, and will enable features of the CPU
  that the computer that it is compiled on has. You should avoid using this
  option if you intend to redistribute the binaries, since the program
  might not run on computers other than your own, if you enable this.
ZZZ
      ;;
    q|Q)
      exit 1
      ;;
    *)
      echo 'That is not a valid selection; please try again.'
      ;;
  esac
done
case $optim in
  0)
    optim="-g -O0"
    ;;
  *)
    optim="-s -O$optim"
    ;;
esac
case $native in
  0)
    native=""
    ;;
  1)
    native="-march=native -mtune=native"
    ;;
esac
case $xwindows in
  0)
    xwindows="-DCONFIG_DISABLE_X11_FUNCTIONS"
    ;;
  1)
    xwindows=""
    ;;
esac
echo "$optim $xwindows $native" > cflags
cat <<'ZZZ'

The configuration has been set. If there are error messages, you might try
specifying different configuration; if it does not work, you should either
try to fix it by yourself or make a bug report. If you manage to fix it
then you should provide a patch that can be used by other people.

The value of cflags is now:
ZZZ
cat cflags
echo
read -p 'Push enter to continue or interrupt to cancel:'
echo
echo 'Compiling Super ZZ Zero ...'
echo 'sz0asm'
bash asm.c
echo 'superzz0'
./maker -a main.mak
echo 'sz0music'
bash music.c
echo 'Done.'
echo 'Use "./maker main.mak" to compile it again with the same configuration.'


# Configuration
VENDOR=2dc8
PRODUCT=310a

# Beautiful output
green_echo() {
  echo ""
  printf '\e[32m== %s ==\e[0m\n' "$*"
}

#green_echo "Checking requirements"
#if [ ! -e "/lib/modules/$(uname -r)/build" ]; then
#	echo "Error: Kernel headers not found."
#	echo "Please install build essentials and kernel headers."
#	echo ""
#	echo "For Ubuntu / Linux Mint"
#	echo "sudo apt install build-essential dkms"
#	echo "sudo apt install linux-headers-\$(uname -r)"
#	echo ""
#	echo "For DNF based systems"
#	echo "sudo dnf groupinstall \"Development Tools\""
#	echo "sudo dnf install kernel-devel kernel-headers"
#	echo ""
#	echo "For YUM based system"
#	echo "sudo yum groupinstall \"Development Tools\""
#	echo "sudo yum install kernel-devel kernel-headers"
#	echo ""
#	echo "Then run this script again :)"
#	exit 0
#fi
#echo "Done"

# sudo apt install build-essential dkms
# sudo apt install linux-headers-$(uname -r)

set -e

green_echo "Cleaning"
rm -fv 8bd-u2cw.ko
rm -fv .8bd-u2cw.ko.cmd
rm -fv 8bd-u2cw.mod
rm -fv 8bd-u2cw.mod.c
rm -fv .8bd-u2cw.mod.cmd
rm -fv 8bd-u2cw.mod.o
rm -fv .8bd-u2cw.mod.o.cmd
rm -fv 8bd-u2cw.o
rm -fv .8bd-u2cw.o.cmd

rm -fv Module.symvers
rm -fv modules.order
rm -fv .Module.symvers.cmd
rm -fv .modules.order.cmd
echo "Done"

# green_echo "Apply code macros"
# echo "#define BUILD_VERSION \""$(date +"%y%m%d-%H%M")"\"" > version.h
# echo "Done"

green_echo "Running make"
make
echo "Done"

green_echo "Unload old module from kernel"
set +e
sudo rmmod 8bd-u2cw.ko
set -e
echo "Done"

green_echo "Load module ff_memless"
sudo modprobe ff_memless
echo "Done"

green_echo "Load new module into kernel"
sudo insmod 8bd-u2cw.ko
echo "Done"


green_echo "Detecting gamepad"
set +e
BUS=$(lsusb -d $VENDOR:$PRODUCT | awk '{print $2}' | grep -Po '[0-9]+')
DEVICE=$(lsusb -d $VENDOR:$PRODUCT | awk '{print $4}' | grep -Po '[0-9]+')
NAME=$(lsusb -d $VENDOR:$PRODUCT | grep -Po ':.*' |grep -Pio '[a-z].*')
set -e

if [[ -z "$BUS" ]]; then
  echo "Warning: Device $VENDOR:$PRODUCT not found! Skipping USB connection reset."
  echo "Finished!"
  exit 0
fi

echo "Name      : $NAME"
echo "Vendor ID : $VENDOR"
echo "Product ID: $PRODUCT"
echo "Device    : $DEVICE"
echo "BUS       : $BUS"

PORT=$(basename $(udevadm info -q path -n /dev/bus/usb/$BUS/$DEVICE))
echo "Port      : $PORT"

#  lsusb -d 2dc8:310a | awk '{print "Bus: "$2", Device: "$4}'
#  udevadm info -q path -n /dev/bus/usb/003/006

green_echo "Unbind USB device"
echo $PORT | sudo tee /sys/bus/usb/drivers/usb/unbind
echo "Done"

# sleep 5

green_echo "Bind USB device"
echo $PORT | sudo tee /sys/bus/usb/drivers/usb/bind
echo "Done"

echo "Finished!"

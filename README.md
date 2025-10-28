# 8bd-u2cw Linux Driver

USB Driver for the **8BitDo Ultimate 2C** gamepad that just works.

**Key features**

- Xbox compatible layout - works out of the box on almost every game
- USB and 2.4G supported
- Force feedback enabled

**Known issues**

* Shoulder triggers LT and RT are detected as buttons rather than triggers

**Additional information**

* Experimental: L4 and R4 buttons require a macro, see [L4 and R4 Support](#l4-and-r4-support)
* Bluetooth is not covered by this driver

## Installation

### Step 1: Build

Get the code from the git repository.

```bash
git clone https://github.com/philipp-schwarz/8bd-u2cw
cd 8bd-u2cw
```

Install kernel headers and build essentials, if needed.

```bash
# Apt based distributions (Ubuntu, Mint, etc.)
sudo apt install build-essential dkms linux-headers-$(uname -r)
```

Compile the code with make.

```bash
make
```

### Step 2: Try the driver

This step is optional but recommended. Before installing, you should try if the driver works as expected. Driver problems can cause your system to crash.

```bash
# Load force feedback module (we need it)
sudo modprobe ff_memless
# Load the gamepad driver
sudo insmod 8bd-u2cw.ko
```

Try the gamepad in your favorite game or test tool. Reconnect your gamepad if it does not work on the first try.
The driver is now loaded until your next reboot. Proceed, if everything works fine.

### Step 3: Install

This installs the driver for your **current** kernel.

```bash
sudo make install
```

You are done - have fun!

**Important:** When you update your system, you may also get a newer kernel version and need to repeat the installation.

## L4 and R4 Support

**This feature is experimental.** The additional shoulder buttons L4 and R4 are not meant for regular use and therefore are not available using the original Windows drivers. However, many users wish to use them as regular buttons.

The intended use by the manufacturer is to program them. Hold the button (L4 or R4) and then also hold the button(s) you want to map to it. Confirm your mapping with the mapping button (square button) while still holding all buttons. You can map multiple buttons. Now, when you press L4 or R4, all mapped buttons will be pressed at the same time.

If you want to use L4 and R4 as standalone buttons, you can active the experimental support:

1. **Apply macro for L4**
    `L4` + `MINUS` button + `LEFT STICK` (press the stick) + `RIGHT STICK` (press the stick)
    and confirm with the `◼ SQUARE` button while holding all other buttons
2. **Apply macro for R4**
    `R4` + `PLUS` button + `LEFT STICK` (press the stick) + `RIGHT STICK` (press the stick)
    and confirm with the `◼ SQUARE` button while holding all other buttons

This needs to be done once for every gamepad.

There are limitations. You can never press L4 and R4 at the same time with stick buttons, PLUS or MINUS. Also these buttons are not mapped to a default gamepad layout (like the Xbox layout). Open your game options and configure your gamepad. See if the game recognizes the extra buttons and if you can map them. This will only work in combination with this Linux driver.

In short: It is experimental. Try it and be happy with whatever works.

/******************************************************************************
 * 8bd-u2cw Linux Driver
 *
 * USB Driver for the 8BitDo Ultimate 2C Gamepad that just works.
 *
 * Module:
 *  8bd-u2cw.ko
 *
 * Key features:
 *    - Xbox compatible layout - works out of the box on almost every game
 *    - USB and 2.4G supported
 *    - Force feedback enabled
 *
 * Known issues:
 *    - Shoulder triggers LT and RT work as buttons rather than triggers
 *
 * Additional information:
 *    - Experimental: L4 and R4 buttons require a macro
 *    - Bluetooth is not covered by this driver
 *
 * License: GPL
 * Source: https://github.com/philipp-schwarz/8bd-u2cw
 *
 * Version history:
 *  v0.1 - 2025-09-23 - Initial version
 *  v0.2 - 2025-10-09 - Force feedback added
 *  v0.3 - 2025-10-26 - Optimized cleanup
 *
 ******************************************************************************/

#define GAMEPAD_NAME "8BitDo Ultimate 2C"

#define DRIVER_NAME "8bd-u2cw"
#define DRIVER_VERSION "0.3.0"

#define PACKET_SIZE 32

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>

#include <linux/usb.h>
#include <linux/input.h>
#include <linux/usb/input.h>

// Some logging macros
#define log_info(fmt, ...) printk(KERN_INFO "" DRIVER_NAME ": " fmt, ##__VA_ARGS__)
#define log_err(fmt, ...) printk(KERN_ERR "" DRIVER_NAME ": " fmt, ##__VA_ARGS__)

// List all supported devices using vendor and product id
static const struct usb_device_id module_device_table[] = {
	// Vendor id: 0x2dc8 8bitdo
	{ USB_DEVICE(0x2dc8, 0x310a) }, // Product id: 0x310a Ultimate 2C
	{ }
};

// Button, trigger and axis states
struct gamepad_state {

	// Buttons
	bool button_x;
	bool button_y;
	bool button_b;
	bool button_a;

	bool button_plus;
	bool button_minus;
	bool button_menu;

	// Shoulder buttons
	bool button_lb;
	bool button_rb;
	bool button_l4; // Experimental, needs activation by macro
	bool button_r4; // Experimental, needs activation by macro

	// Stick buttons
	bool button_stick_left;
	bool button_stick_right;

	// Direction-Pad
	bool dpad_top;
	bool dpad_right;
	bool dpad_bottom;
	bool dpad_left;

	// Shoulder trigger
	uint8_t trigger_lt;
	uint8_t trigger_rt;
	bool trigger_lt_button;
	bool trigger_rt_button;

	// Axis
	int16_t stick_left_x;
	int16_t stick_left_y;
	int16_t stick_right_x;
	int16_t stick_right_y;
};

// Gamepad object
struct gamepad {

	bool active;

	// USB
	struct usb_device *usb_device;
	struct usb_interface *usb_interface;
	struct usb_endpoint_descriptor *usb_endpoint_in;
	struct usb_endpoint_descriptor *usb_endpoint_out;

	// Input device
	bool input_device_active;
	bool input_ff_active;
	struct input_dev *input_device;
	char input_path[64];

	// Data input
	uint8_t *usb_in_data;
	dma_addr_t usb_in_dma;
	spinlock_t usb_in_lock;
	struct urb *usb_in_urb;

	// Data output
	uint8_t *usb_out_data;
	dma_addr_t usb_out_dma;
	spinlock_t usb_out_lock;
	struct urb *usb_out_urb;
	struct usb_anchor usb_out_anchor;

	bool usb_out_sending;

	// Button, trigger and axis states
	struct gamepad_state state;

	// Queue
	bool rumble_off_pending;

	// Debugging
	bool heartbeat;

};


// Gamepad initialisation
static int gamepad_probe(struct usb_interface *interface, const struct usb_device_id *id);
static void gamepad_disconnect(struct usb_interface *interface);
static void gamepad_cleanup(struct gamepad *gamepad);

// Sending messages
static void gamepad_welcome_message(struct gamepad *gamepad);
static void gamepad_rumble_message(struct gamepad *gamepad, uint16_t weak, uint16_t strong);

// Callbacks for sending and receiving data
static void gamepad_in_cb(struct urb *urb);
static void gamepad_out_cb(struct urb *urb);

// Input system initialisation
static int gamepad_input_connect(struct gamepad *gamepad);
static void gamepad_input_process(struct gamepad *gamepad);
static void gamepad_input_disconnect(struct gamepad *gamepad);

// Callback for force feedback
static int gamepad_force_cb(struct input_dev *device, void *data, struct ff_effect *effect);

/******************************************************************************
 * Code starts here
 ******************************************************************************/

// The game wants the gamepad to rumble
static int gamepad_force_cb(struct input_dev *device, void *data, struct ff_effect *effect) {
	struct gamepad *gamepad = input_get_drvdata(device);

	uint16_t strong;
	uint16_t weak;

	// We only support rumble, not the kinky stuff
	if (effect->type != FF_RUMBLE)
		return 0;

	weak = effect->u.rumble.weak_magnitude;
	strong = effect->u.rumble.strong_magnitude;

	gamepad_rumble_message(gamepad, weak, strong);

	return 0;
}

// Initialize the gamepad as input device
static int gamepad_input_connect(struct gamepad *gamepad) {

	int error;
	struct input_dev *device;

	// Create input device
	device = input_allocate_device();
	if (!device)
		return -ENOMEM;
	gamepad->input_device = device;

	// Say my name!
	device->name = GAMEPAD_NAME;

	// Setup a path to identify the gamepad
	usb_make_path(gamepad->usb_device, gamepad->input_path, sizeof(gamepad->input_path));
	strlcat(gamepad->input_path, "/input0", sizeof(gamepad->input_path));
	device->phys = gamepad->input_path;

	// Tell input device and USB about each other
	usb_to_input_id(gamepad->usb_device, &device->id);

	// Map it to the correct point in sysfs tree
	device->dev.parent = &gamepad->usb_interface->dev;

	// Map the gamepad to the input device
	input_set_drvdata(device, gamepad);

	// Enable force feedback
	input_set_capability(device, EV_FF, FF_RUMBLE);
	error = input_ff_create_memless(device, NULL, gamepad_force_cb);
	if (error)
		return -ENOMEM;
	gamepad->input_ff_active = true;

	// Inform the input device about existing buttons, sticks, triggers
	// Buttons on the right side
	input_set_capability(device, EV_KEY, BTN_A);
	input_set_capability(device, EV_KEY, BTN_B);
	input_set_capability(device, EV_KEY, BTN_X);
	input_set_capability(device, EV_KEY, BTN_Y);

	// D-Pad
	// Assigned as axis rather than buttons on an Xbox layout
	input_set_abs_params(device, ABS_HAT0X, -1, 1, 0, 0);
	input_set_abs_params(device, ABS_HAT0Y, -1, 1, 0, 0);

	// Middle buttons
	input_set_capability(device, EV_KEY, BTN_START);
	input_set_capability(device, EV_KEY, BTN_SELECT);
	input_set_capability(device, EV_KEY, BTN_MODE);

	// Shoulder buttons
	input_set_capability(device, EV_KEY, BTN_TL);
	input_set_capability(device, EV_KEY, BTN_TR);

	input_set_capability(device, EV_KEY, BTN_TL2);
	input_set_capability(device, EV_KEY, BTN_TR2);

	/* LT and RT as trigger - does not work as expected
	input_set_abs_params(device, ABS_Z, 0, 255, 0, 0);
	input_set_abs_params(device, ABS_RZ, 0, 255, 0, 0);
	*/

	// Stick buttons
	input_set_capability(device, EV_KEY, BTN_THUMBL);
	input_set_capability(device, EV_KEY, BTN_THUMBR);

	// L4 and R4
	input_set_capability(device, EV_KEY, BTN_TRIGGER_HAPPY1);
	input_set_capability(device, EV_KEY, BTN_TRIGGER_HAPPY2);

	// Sticks with axes
	// Left
	input_set_abs_params(device, ABS_X, -32768, 32767, 16, 128);
	input_set_abs_params(device, ABS_Y, -32768, 32767, 16, 128);
	// Right
	input_set_abs_params(device, ABS_RX, -32768, 32767, 16, 128);
	input_set_abs_params(device, ABS_RY, -32768, 32767, 16, 128);

	// Register device
	error = input_register_device(device);
	if (error)
		return -ENOMEM;
	gamepad->input_device_active = true;

	return 0;
}

// Disconnect input device
static void gamepad_input_disconnect(struct gamepad *gamepad) {

	// Unregister the input device when active
	if (gamepad->input_device_active) {
		// Bye bye
		input_unregister_device(gamepad->input_device);
		gamepad->input_device_active = false;
	}

	// Something went wrong when initializing the input device.
	// We need some manual cleanup:
	else {
		// Only destroy FF when we do not call input_unregister_device
		if (gamepad->input_ff_active) {
			input_ff_destroy(gamepad->input_device);
			gamepad->input_ff_active = false;
		}

		// Free the input device
		if (gamepad->input_device) {
			input_free_device(gamepad->input_device);
			gamepad->input_device = 0;
		}
	}

}

// Process input from the gamepad
static void gamepad_input_process(struct gamepad *gamepad) {

	struct input_dev *device = gamepad->input_device;
	struct gamepad_state *state = &gamepad->state;

	if (gamepad->input_device_active) {

		// Simple debugging:
		// log_info("BUTTON A %d\n", gamepad->button_a);

		input_report_key(device, BTN_A, state->button_a);
		input_report_key(device, BTN_B, state->button_b);
		input_report_key(device, BTN_X, state->button_y); // X and Y
		input_report_key(device, BTN_Y, state->button_x); // need to be swapped

		input_report_abs(device, ABS_HAT0X, state->dpad_left*(-1) + state->dpad_right);
		input_report_abs(device, ABS_HAT0Y, state->dpad_top*(-1) + state->dpad_bottom);

		input_report_key(device, BTN_TL, state->button_lb);
		input_report_key(device, BTN_TR, state->button_rb);

		input_report_key(device, BTN_THUMBL, state->button_stick_left);
		input_report_key(device, BTN_THUMBR, state->button_stick_right);

		input_report_key(device, BTN_TRIGGER_HAPPY1, state->button_l4);
		input_report_key(device, BTN_TRIGGER_HAPPY2, state->button_r4);

		input_report_key(device, BTN_START, state->button_plus);
		input_report_key(device, BTN_SELECT, state->button_minus);
		input_report_key(device, BTN_MODE, state->button_menu);

		input_report_abs(device, ABS_X, state->stick_left_x);
		input_report_abs(device, ABS_Y, -state->stick_left_y); // Y axis is mirrored
		input_report_abs(device, ABS_RX, state->stick_right_x);
		input_report_abs(device, ABS_RY, -state->stick_right_y); // here too

		input_report_key(device, BTN_TL2, state->trigger_lt_button);
		input_report_key(device, BTN_TR2, state->trigger_rt_button);

		/* LT and RT as trigger - does not work as expected
		input_report_abs(device, ABS_Z, state->trigger_lt);
		input_report_abs(device, ABS_RZ, state->trigger_rt);
		*/

		input_sync(device);
	}

}

// Callback for incoming data
static void gamepad_in_cb(struct urb *urb) {
	struct gamepad *gamepad = urb->context;
	struct gamepad_state *state = &gamepad->state;

	int status = urb->status;
	bool macro_lr4 = false;

	uint8_t *data = gamepad->usb_in_data;


	// Simple debugging:
	// print_hex_dump(KERN_INFO, DRIVER_NAME ": ", DUMP_PREFIX_OFFSET, 32, 1, gamepad->usb_in_data, PACKET_SIZE, 0);

	if (data[0] == 0x00) {

		// Button mapping
		state->dpad_top           = data[2] & 1;
		state->dpad_bottom        = data[2] & 2;
		state->dpad_left          = data[2] & 4;
		state->dpad_right         = data[2] & 8;

		state->button_plus        = data[2] & 16;
		state->button_minus       = data[2] & 32;
		state->button_stick_left  = data[2] & 64;
		state->button_stick_right = data[2] & 128;

		state->button_lb          = data[3] & 1;
		state->button_rb          = data[3] & 2;
		state->button_menu        = data[3] & 4;

		state->button_a           = data[3] & 16;
		state->button_b           = data[3] & 32;
		state->button_x           = data[3] & 64;
		state->button_y           = data[3] & 128;

		// Trigger
		state->trigger_lt         = data[4];
		state->trigger_rt         = data[5];

		// Virtual buttons from triggers
		if (state->trigger_lt < 16)
			state->trigger_lt_button = false;
		else if (state->trigger_lt > 32)
			state->trigger_lt_button = true;
		if (state->trigger_rt < 16)
			state->trigger_rt_button = false;
		else if (state->trigger_rt > 32)
			state->trigger_rt_button = true;

		// Axis
		state->stick_left_x       = (data[7]<<8) + data[6];
		state->stick_left_y       = (data[9]<<8) + data[8];
		state->stick_right_x      = (data[11]<<8) + data[10];
		state->stick_right_y      = (data[13]<<8) + data[12];

		// Experimental: Shoulder buttons L4 and R4
		// L4
		state->button_l4 = false;
		if (
			state->button_stick_left
			&& state->button_stick_right
			&& state->button_minus
		) {
			macro_lr4 = true;
			state->button_l4 = true;
		}
		// R4
		state->button_r4 = false;
		if (
			state->button_stick_left
			&& state->button_stick_right
			&& state->button_plus
		) {
			macro_lr4 = true;
			state->button_r4 = true;
		}
		// Reset macro helper buttons
		if (macro_lr4) {
			state->button_stick_left = false;
			state->button_stick_right = false;
			state->button_plus = false;
			state->button_minus = false;
		}

		// Heartbeat to the kernel log
		if (
			state->button_plus
			&& state->button_minus
			&& state->button_lb
			&& state->button_rb
		) {
			if (!gamepad->heartbeat) {
				log_info("Heartbeat! (L + R + Plus + Minus)\n");
				gamepad->heartbeat = true;
			}
		}
		else
			gamepad->heartbeat = false;

		gamepad_input_process(gamepad);
	}

	// Success
	if (status == 0) {

		// Only send packets to active gamepads
		if (gamepad->active)
			usb_submit_urb(gamepad->usb_in_urb, GFP_KERNEL);
	}
}

// Callback for outgoing data
static void gamepad_out_cb(struct urb *urb) {
	struct gamepad *gamepad = urb->context;

	gamepad->usb_out_sending = false;

	// We skipped a rumble off message and need to resend it
	if (gamepad->rumble_off_pending) {
		gamepad_rumble_message(gamepad, 0, 0);
	}

}

// Send rumble message
static void gamepad_rumble_message(struct gamepad *gamepad, uint16_t weak, uint16_t strong) {
	unsigned long flags;

	uint8_t data[16];
	uint8_t len;

	int error;

	// Only send packets to active gamepads
	if (!gamepad->active)
		return;

	// Skip if already sending
	// Unlikely event. We might skip a rumble, but the gamepad motors cannot
	// handle two at the same time anyway.
	if (gamepad->usb_out_sending) {

		// However, we need to remember if the rumble should stop
		// or the gamepad will rumble endless.
		if (!weak && !strong)
			gamepad->rumble_off_pending = true;

		return;
	}

	// Rumble sequence
	data[0] = 0x00;
	data[1] = 0x08;
	data[2] = 0x00;

	// The left motor has the heavy weight. I opened the gamepad to verify this.
	// Byte 3 controls the left motor
	data[3] = strong / 256;
	// Byte 4 controls the right motor
	data[4] = weak / 256;

	data[5] = 0x00;
	data[6] = 0x00;
	data[7] = 0x00;
	len = 8;

	gamepad->usb_out_sending = true;

	if (!weak && !strong)
		gamepad->rumble_off_pending = false;

	spin_lock_irqsave(&gamepad->usb_out_lock, flags);

	// Copy data
	memcpy(gamepad->usb_out_data, data, len);
	gamepad->usb_out_urb->transfer_buffer_length = len;

	// Send and look for errors
	usb_anchor_urb(gamepad->usb_out_urb, &gamepad->usb_out_anchor);
	error = usb_submit_urb(gamepad->usb_out_urb, GFP_ATOMIC);
	if (error) {
		usb_unanchor_urb(gamepad->usb_out_urb);
		gamepad->usb_out_sending = false;
	}

	spin_unlock_irqrestore(&gamepad->usb_out_lock, flags);
}

// Send initialisation message
static void gamepad_welcome_message(struct gamepad *gamepad) {
	unsigned long flags;

	uint8_t data[16];
	uint8_t len;

	int error;

	// Only send packets to active gamepads
	if (!gamepad->active)
		return;

	// Skip if already sending
	if (gamepad->usb_out_sending)
		return;

	// Xbox Gamepad LED message
	// The Ultimate 2C Wireless gamepad doesn't even have a programmable LED,
	// it still requires this message in order to start working.
	// Don't ask me why. Wild guess: The gamepad needs a "heartbeat" or any
	// signal from the host to know it is there.

	// So let's be nice and say hello:
	data[0] = 0x01;
	data[1] = 0x03;
	data[2] = 0x00;
	len = 3;

	gamepad->usb_out_sending = true;

	spin_lock_irqsave(&gamepad->usb_out_lock, flags);

	memcpy(gamepad->usb_out_data, data, len);
	gamepad->usb_out_urb->transfer_buffer_length = len;

	usb_anchor_urb(gamepad->usb_out_urb, &gamepad->usb_out_anchor);
	error = usb_submit_urb(gamepad->usb_out_urb, GFP_ATOMIC);
	if (error) {
		usb_unanchor_urb(gamepad->usb_out_urb);
		gamepad->usb_out_sending = false;
	}

	spin_unlock_irqrestore(&gamepad->usb_out_lock, flags);
}

// Initialisation, setup everything we need
static int gamepad_probe(struct usb_interface *interface, const struct usb_device_id *id) {

	int i;
	int error;
	struct gamepad *gamepad;

	log_info("Initialize gamepad " GAMEPAD_NAME " (Driver " DRIVER_NAME " " DRIVER_VERSION ")\n");

	// Allocate gamepad object
	gamepad = kzalloc(sizeof(*gamepad), GFP_KERNEL);
	if (!gamepad)
		return -ENOMEM;

	gamepad->active = true;
	gamepad->usb_interface = interface;
	gamepad->usb_device = interface_to_usbdev(interface);

	// Bind gamepad to the USB interface
	usb_set_intfdata(interface, gamepad);

	// Allocate USB data
	gamepad->usb_in_data = usb_alloc_coherent(gamepad->usb_device, PACKET_SIZE, GFP_KERNEL, &gamepad->usb_in_dma);
	if (!gamepad->usb_in_data) {
		gamepad_cleanup(gamepad);
		return -ENOMEM;
	}
	gamepad->usb_out_data = usb_alloc_coherent(gamepad->usb_device, PACKET_SIZE, GFP_KERNEL, &gamepad->usb_out_dma);
	if (!gamepad->usb_out_data) {
		gamepad_cleanup(gamepad);
		return -ENOMEM;
	}

	// Allocate USB URBs
	gamepad->usb_in_urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!gamepad->usb_in_urb) {
		gamepad_cleanup(gamepad);
		return -ENOMEM;
	}
	gamepad->usb_out_urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!gamepad->usb_out_urb) {
		gamepad_cleanup(gamepad);
		return -ENOMEM;
	}

	// Anchor USB out
	init_usb_anchor(&gamepad->usb_out_anchor);

	// Init locks for later use
	spin_lock_init(&gamepad->usb_in_lock);
	spin_lock_init(&gamepad->usb_out_lock);

	// Find endpoints for in and output
	for (i=0; i<interface->cur_altsetting->desc.bNumEndpoints; i++) {
		struct usb_endpoint_descriptor *ep = &interface->cur_altsetting->endpoint[i].desc;
		if (usb_endpoint_xfer_int(ep)) {
			if (usb_endpoint_dir_in(ep))
				gamepad->usb_endpoint_in = ep;
			else
				gamepad->usb_endpoint_out = ep;
		}
	}
	if (!gamepad->usb_endpoint_in || !gamepad->usb_endpoint_out) {
		gamepad_cleanup(gamepad);
		return -ENODEV;
	}

	// Init USB input
	usb_fill_int_urb(gamepad->usb_in_urb, gamepad->usb_device,
			 usb_rcvintpipe(gamepad->usb_device, gamepad->usb_endpoint_in->bEndpointAddress),
			 gamepad->usb_in_data, PACKET_SIZE,
			 gamepad_in_cb, gamepad,
			 gamepad->usb_endpoint_in->bInterval);
	gamepad->usb_in_urb->transfer_dma = gamepad->usb_in_dma;
	gamepad->usb_in_urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

	// Init USB output
	usb_fill_int_urb(gamepad->usb_out_urb, gamepad->usb_device,
		usb_sndintpipe(gamepad->usb_device, gamepad->usb_endpoint_out->bEndpointAddress),
		gamepad->usb_out_data, PACKET_SIZE,
		gamepad_out_cb, gamepad,
		gamepad->usb_endpoint_out->bInterval);
	gamepad->usb_out_urb->transfer_dma = gamepad->usb_out_dma;
	gamepad->usb_out_urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

	// Say hello
	gamepad_welcome_message(gamepad);

	// Init input device
	error = gamepad_input_connect(gamepad);
	if (error) {
		gamepad_cleanup(gamepad);
		return error;
	}

	// Everything seems to be fine
	log_info("Gamepad connected successfuly\n");

	// Start input receiving
	usb_submit_urb(gamepad->usb_in_urb, GFP_KERNEL);

	return 0;
}

// Disconnect gamepad
static void gamepad_disconnect(struct usb_interface *interface) {

	struct gamepad *gamepad = usb_get_intfdata(interface);
	gamepad_cleanup(gamepad);

	log_info("Gamepad disconnected\n");
}

// Cleanup, free allocated memory before exiting
static void gamepad_cleanup(struct gamepad *gamepad) {

	gamepad->active = false;

	// Unregister input device
	gamepad_input_disconnect(gamepad);

	// Clear all anchored URBs
	if (!usb_wait_anchor_empty_timeout(&gamepad->usb_out_anchor, 200)) {
		// Every URB that is not processed within a few milliseconds
		// is most likely dead
		usb_kill_anchored_urbs(&gamepad->usb_out_anchor);
	}

	// Free USB URBs
	if (gamepad->usb_in_urb) {
		usb_free_urb(gamepad->usb_in_urb);
		gamepad->usb_in_urb = 0;
	}
	if (gamepad->usb_out_urb) {
		usb_free_urb(gamepad->usb_out_urb);
		gamepad->usb_out_urb = 0;
	}

	// Free USB data
	if (gamepad->usb_in_data) {
		usb_free_coherent(gamepad->usb_device, PACKET_SIZE, gamepad->usb_in_data, gamepad->usb_in_dma);
		gamepad->usb_in_data = 0;
	}
	if (gamepad->usb_out_data) {
		usb_free_coherent(gamepad->usb_device, PACKET_SIZE, gamepad->usb_out_data, gamepad->usb_out_dma);
		gamepad->usb_out_data = 0;
	}

	kfree(gamepad);

	return;
}


// Basic driver information and callback functions
static struct usb_driver module_driver = {
	.name = DRIVER_NAME,
	.probe = gamepad_probe,
	.disconnect = gamepad_disconnect,
	.id_table = module_device_table,
};

module_usb_driver(module_driver);

MODULE_AUTHOR("Philipp Schwarz <pschwarzmail@gmail.com>");
MODULE_DESCRIPTION("8BitDo Ultimate 2C Gamepad driver");
MODULE_LICENSE("GPL");

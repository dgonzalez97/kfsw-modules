#include <errno.h>
#include <stdbool.h>
#include <string.h>

#include <zephyr/shell/shell.h>
#include <zephyr/sys/util.h>

#include <kfsw/modules/button_test.h>

static int cmd_button_test_status(const struct shell *sh, size_t argc, char **argv)
{
	struct kfsw_button_test_status status;
	int result;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	result = kfsw_button_test_get_status(&status);
	if (result != 0) {
		shell_error(sh, "button_test status unavailable (%d)", result);
		return result;
	}

	shell_print(sh, "press_count: %u", status.press_count);
	shell_print(sh, "last_press_s: %u", status.last_press_s);
	shell_print(sh, "led_green: %s", status.led_green ? "on" : "off");
	shell_print(sh, "led_blue: %s", status.led_blue ? "on" : "off");
	shell_print(sh, "led_red: %s", status.led_red ? "on" : "off");
#if CONFIG_KFSW_BUTTON_TEST_GPIO
	shell_print(sh, "debounce_ms: %d", CONFIG_KFSW_BUTTON_TEST_DEBOUNCE_MS);
#endif
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(button_test_commands,
	SHELL_CMD_ARG(status, NULL, "Show debounced button status.", cmd_button_test_status, 1, 0),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(button_test, &button_test_commands, "Button example diagnostics.", NULL);

static int cmd_hw_test_led(const struct shell *sh, size_t argc, char **argv)
{
	enum kfsw_button_test_led led;
	bool on;
	int result;

	ARG_UNUSED(argc);

	if (strcmp(argv[1], "green") == 0) {
		led = KFSW_BUTTON_TEST_LED_GREEN;
	} else if (strcmp(argv[1], "blue") == 0) {
		led = KFSW_BUTTON_TEST_LED_BLUE;
	} else if (strcmp(argv[1], "red") == 0) {
		led = KFSW_BUTTON_TEST_LED_RED;
	} else {
		shell_error(sh, "unknown LED colour: %s", argv[1]);
		return -EINVAL;
	}

	if (strcmp(argv[2], "on") == 0) {
		on = true;
	} else if (strcmp(argv[2], "off") == 0) {
		on = false;
	} else {
		shell_error(sh, "LED state must be on or off");
		return -EINVAL;
	}

	result = kfsw_button_test_set_led(led, on);
	if (result != 0) {
		shell_error(sh, "%s LED control failed (%d)", argv[1], result);
		return result;
	}

	shell_print(sh, "%s: %s", argv[1], argv[2]);
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(hw_test_commands,
	SHELL_CMD_ARG(led, NULL, "Control LED: led <green|blue|red> <on|off>.",
		      cmd_hw_test_led, 3, 0),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(test, &hw_test_commands, "Developer hardware-test commands.", NULL);

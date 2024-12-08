import myconfig

import time
import traceback
import struct

import board
import analogio
import displayio
import supervisor

import adafruit_ssd1306, adafruit_hid
import busio

from rainbowio import colorwheel
import neopixel

import usb_hid

class Gamepad:
    """Emulate a generic gamepad controller with 16 buttons,
    numbered 1-16, and two joysticks, one controlling
    ``x` and ``y`` values, and the other controlling ``z`` and
    ``r_z`` (z rotation or ``Rz``) values.
    The joystick values could be interpreted
    differently by the receiving program: those are just the names used here.
    The joystick values are in the range -127 to 127."""

    def __init__(self, devices):
        """Create a Gamepad object that will send USB gamepad HID reports.
        Devices can be a list of devices that includes a gamepad device or a gamepad device
        itself. A device is any object that implements ``send_report()``, ``usage_page`` and
        ``usage``.
        """
        self._gamepad_device = adafruit_hid.find_device(devices, usage_page=0x1, usage=0x05)

        # Reuse this bytearray to send mouse reports.
        # Typically controllers start numbering buttons at 1 rather than 0.
        # report[0] buttons 1-8 (LSB is button 1)
        # report[1] buttons 9-16
        # report[2] joystick 0 x: -127 to 127
        # report[3] joystick 0 y: -127 to 127
        # report[4] joystick 1 x: -127 to 127
        # report[5] joystick 1 y: -127 to 127
        self._report = bytearray(6)

        # Remember the last report as well, so we can avoid sending
        # duplicate reports.
        self._last_report = bytearray(6)

        # Store settings separately before putting into report. Saves code
        # especially for buttons.
        self._buttons_state = 0
        self._joy_x = 0
        self._joy_y = 0
        self._joy_z = 0
        self._joy_r_z = 0

        # Send an initial report to test if HID device is ready.
        # If not, wait a bit and try once more.
        try:
            self.reset_all()
        except OSError:
            time.sleep(1)
            self.reset_all()

    def press_buttons(self, *buttons):
        """Press and hold the given buttons."""
        for button in buttons:
            self._buttons_state |= 1 << self._validate_button_number(button) - 1
        self._send()

    def release_buttons(self, *buttons):
        """Release the given buttons."""
        for button in buttons:
            self._buttons_state &= ~(1 << self._validate_button_number(button) - 1)
        self._send()

    def release_all_buttons(self):
        """Release all the buttons."""

        self._buttons_state = 0
        self._send()

    def click_buttons(self, *buttons):
        """Press and release the given buttons."""
        self.press_buttons(*buttons)
        self.release_buttons(*buttons)

    def move_joysticks(self, x=None, y=None, z=None, r_z=None):
        """Set and send the given joystick values.
        The joysticks will remain set with the given values until changed
        One joystick provides ``x`` and ``y`` values,
        and the other provides ``z`` and ``r_z`` (z rotation).
        Any values left as ``None`` will not be changed.
        All values must be in the range -127 to 127 inclusive.
        Examples::
            # Change x and y values only.
            gp.move_joysticks(x=100, y=-50)
            # Reset all joystick values to center position.
            gp.move_joysticks(0, 0, 0, 0)
        """
        if x is not None:
            self._joy_x = self._validate_joystick_value(x)
        if y is not None:
            self._joy_y = self._validate_joystick_value(y)
        if z is not None:
            self._joy_z = self._validate_joystick_value(z)
        if r_z is not None:
            self._joy_r_z = self._validate_joystick_value(r_z)
        self._send()

    def reset_all(self):
        """Release all buttons and set joysticks to zero."""
        self._buttons_state = 0
        self._joy_x = 0
        self._joy_y = 0
        self._joy_z = 0
        self._joy_r_z = 0
        self._send(always=True)

    def _send(self, always=False):
        """Send a report with all the existing settings.
        If ``always`` is ``False`` (the default), send only if there have been changes.
        """
        struct.pack_into(
            "<Hbbbb",
            self._report,
            0,
            self._buttons_state,
            self._joy_x,
            self._joy_y,
            self._joy_z,
            self._joy_r_z,
        )

        if always or self._last_report != self._report:
            self._gamepad_device.send_report(self._report)
            # Remember what we sent, without allocating new storage.
            self._last_report[:] = self._report

    @staticmethod
    def _validate_button_number(button):
        if not 1 <= button <= 16:
            raise ValueError("Button number must in range 1 to 16")
        return button

    @staticmethod
    def _validate_joystick_value(value):
        if not -127 <= value <= 127:
            raise ValueError("Joystick value must be in range -127 to 127")
        return value


def range_map(x, in_min, in_max, out_min, out_max):
    try:
        range = in_max - in_min
        if range == 0:
            range == 1
        return (x - in_min) * (out_max - out_min) / range + out_min
    except ZeroDivisionError:
        return 0


class AxisInput:
    def __init__(self, pin):
        self._analog_in = analogio.AnalogIn(pin)

    def _read_raw(self):
        return self._analog_in.value

    def get(self):
        return self._read_raw()


class AxisWithMovingAverage(AxisInput):
    def __init__(self, pin):
        super().__init__(pin)
        self.samples = [0] * 10

    def get(self):
        sum = 0
        SAMPLES = 25
        for i in range(SAMPLES):
            sum += self._read_raw()
            # time.sleep(100 / 1000000.0)
        if myconfig.DEBUG:
            print(myconfig.NAME, self._read_raw(), sum / SAMPLES)
        return sum / SAMPLES


class AxisWithKalman(AxisInput):
    def __init__(self, pin):
        super().__init__(pin)
        self.prev_now = time.monotonic_ns() / 1000000.0
        self.x = 0.0
        self.p = 10000.0
        self.v = 0.0
        self.p_v = 10000.0

    def get(self):
        raw = self._read_raw()
        now = time.monotonic_ns() / 1000000.0

        dt = now - self.prev_now + 1
        self.prev_now = now

        prev_x = self.x
        prev_p = self.p

        self.x, self.p = AxisWithKalman.predict(
            self.x, self.p, self.v, self.p_v, 0.05, dt
        )
        self.x, self.p, k = AxisWithKalman.update(self.x, self.p, 0.2, raw)

        self.v = (self.x - prev_x) / dt
        self.p_v = (self.p - prev_p) / dt

        print(raw, dt, "x", self.x, self.p, "v", self.v, self.p_v, "k", k)
        return self.x

    @staticmethod
    # x state, p estimate uncertainty,
    def predict(x, p, v, p_v, q, dt):
        # Prediction
        x = x + dt * v  # State Transition Equation (Dynamic Model or Prediction Model)
        p = p + (dt ** 2 * p_v) + q  # Predicted Covariance equation
        return x, p

    @staticmethod
    # x state, p estimate uncertainty, r measurement uncertainty, z measurement
    def update(x, p, r, z):
        k = p / (p + r)  # Kalman Gain, k=0.0 uncertain, k=1.0 certain
        x = x + k * (z - x)  # State Update
        p = (1 - k) * p  # Covariance Update
        return x, p, k


GREEN = (0, 255, 0)
RED = (255, 0, 0)
YELLOW = (255, 255, 0)
ORANGE = (255, 130, 0)
MILLIS = 1000.0


class ColorLed:
    def __init__(self):
        try:
            self.pixels = neopixel.NeoPixel(
                board.GP16, 1, brightness=0.3, auto_write=False
            )
            self.show(GREEN)
        except Exception as e:
            print("Error during init led", e)
            self.pixels = None

    def show(self, c):
        if self.pixels:
            self.pixels[0] = c
            self.pixels.show()
        else:
            print("LED: ", c)


class MiniDisplay:
    WIDTH = 70
    HEIGHT = 40

    def __init__(self):
        try:
            displayio.release_displays()

            i2c = busio.I2C(scl=board.GP3, sda=board.GP2)
            self.display = adafruit_ssd1306.SSD1306_I2C(
                MiniDisplay.WIDTH, MiniDisplay.HEIGHT, i2c
            )
            self.display.fill(0)
            self.display.show()
        except Exception as e:
            print("Error during init display", e)
            self.display = None

    def show(self):
        if not self.display:
            return
        self.display.show()
        self.display.fill(0)

    def draw_horizontal_bar(self, line):
        if not self.display:
            return
        BAR_HEIGHT = 4
        y_offset = int(MiniDisplay.HEIGHT / 2 - BAR_HEIGHT / 2)
        for i in range(line):
            for y in range(BAR_HEIGHT):
                self.display.pixel(i, y + y_offset, 1)

    def draw_vertical_bar(self, pos):
        if not self.display:
            return
        for y in range(MiniDisplay.HEIGHT):
            self.display.pixel(int(pos), y, 1)


def main(led, display):
    if myconfig.DEBUG:
        print("main")

    gp = Gamepad(usb_hid.devices)
    axis = AxisWithMovingAverage(board.A3)

    max_value = myconfig.MAX
    min_value = myconfig.MIN

    if myconfig.DEBUG:
        print("running")
    led.show(GREEN)
    while True:
        # Sample
        raw_value = axis.get()

        # Clamp
        clamp_max = False
        clamp_min = False
        if raw_value > max_value:
            raw_value = myconfig.MAX
            clamp_max = True
        if raw_value < min_value:
            raw_value = myconfig.MIN
            clamp_min = True

        # LED
        bright = range_map(raw_value, min_value, max_value, 0, 255)
        led.show((0, 0, bright))

        # Joystick
        joystick_value = range_map(raw_value, min_value, max_value, -127, 127)

        if myconfig.AXIS is "x":
            gp.move_joysticks(
                x=int(joystick_value),
            )
        else:
            gp.move_joysticks(
                y=int(joystick_value),
            )

        # Display
        range = max_value - min_value + 1
        line = (raw_value - min_value) / (range / MiniDisplay.WIDTH)
        display.draw_horizontal_bar(line)
        if clamp_min:
            display.draw_vertical_bar(0)
        if clamp_max:
            display.draw_vertical_bar(MiniDisplay.WIDTH - 1)
        half = int(MiniDisplay.WIDTH / 2)
        if abs(line - half) < 2:
            display.draw_vertical_bar(half)
        display.show()

        time.sleep(50 / MILLIS)


print("Starting USB Adapter", myconfig.NAME)
led = ColorLed()
display = MiniDisplay()
while True:
    try:
        main(led, display)
    except Exception as e:
        traceback.print_exception(None, e, None)
        led.show(ORANGE)
        time.sleep(1)
        led.show(RED)
        time.sleep(1)
        supervisor.reload()

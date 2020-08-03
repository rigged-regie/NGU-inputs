from collections import namedtuple
import ctypes
import platform
import struct
import time
import win32api
import win32con as wcon
import win32gui

class userset:
    SHORT_SLEEP = 0.04

class Window:
    id = 0
    x = 0
    y = 0

    @staticmethod
    def init():
        def enum_windows(hwnd, windows):
            """Add window title and ID to array."""
            windows.append((hwnd, win32gui.GetWindowText(hwnd)))

        windows = []
        win32gui.EnumWindows(enum_windows, windows)
        windows = [window[0] for window in windows if window[1] == 'NGU Idle']
        if len(windows) == 0:
            raise RuntimeError("Game window not found.")
        Window.id = windows[0]

class Com:
    """Com communicates with pipe server"""
    """eg. dll injected into the game process"""

    pipe = None

    @staticmethod
    def init() -> None:
        """Same as reconnect"""
        return Com.reconnect()

    @staticmethod
    def reconnect() -> None:
        """Recoonects with the communication pipe"""
        if Com.pipe is not None:
            Com.pipe.close()
            Com.pipe = None

        try:
            Com.pipe = open('\\\\.\\pipe\\ngu_cmd', 'wb', buffering=0)
        except FileNotFoundError as err:
            print('Are you sure you ran injector?')
            raise err

    @staticmethod
    def sync() -> None:
        """Wait for pipe server to complete last command"""
        Com.pipe.write(struct.pack('<b', 0xc))
    
    @staticmethod
    def set_cur_pos(x: int, y: int) -> None:
        """Fake position returned by user32.GetCursorPos"""
        """hook_get_cur_pos needed"""
        Com.pipe.write(struct.pack('<bhh', 0x0, x, y))
        Com.sync()
    
    @staticmethod
    def restore_cur() -> None:
        """Restore user32.GetCursorPos to its original state"""
        Com.pipe.write(struct.pack('<b', 0x1))
        Com.sync()
    
    @staticmethod
    def shortcut(keycode: int) -> None:
        """Fake keycode returned by Unity.GetKeyDownInt"""
        """hook_get_key_down needed"""
        Com.pipe.write(struct.pack('<bi', 0x2, keycode))
        Com.sync()
    
    @staticmethod
    def restore_shortcut() -> None:
        """Restore Unity.GetKeyDownInt to its original state"""
        Com.pipe.write(struct.pack('<b', 0x3))
        Com.sync()

    @staticmethod
    def special(keycode: int) -> None:
        """Fake keycode returned by Unity.GetKeyString"""
        """hook_get_key needed"""
        Com.pipe.write(struct.pack('<bb', 0x4, keycode))
        Com.sync()
    
    @staticmethod
    def restore_special() -> None:
        """Restore Unity.GetKeyString to its original state"""
        Com.pipe.write(struct.pack('<b', 0x5))
        Com.sync()

    @staticmethod
    def unhook() -> None:
        """Disable all hooks"""
        Com.pipe.write(struct.pack('<b', 0x6))
        Com.sync()

    @staticmethod
    def eject() -> None:
        """Close pipe server"""
        """Make sure that each hook is disabled"""
        Com.pipe.write(struct.pack('<b', 0x7))
        
    @staticmethod
    def hook_focus() -> None:
        """Hooks Unity.EventSystems.EventSystem.OnApplicationFocus."""
        """To take effect window must be focused and ufocused after"""
        """hook is created. This hook is necessary for script when game"""
        """is out of focus"""
        Com.pipe.write(struct.pack('<b', 0x8))
        Com.sync()

    @staticmethod
    def hook_get_cur_pos() -> None:
        """Hook user32.GetCursorPos"""
        Com.pipe.write(struct.pack('<b', 0x9))
        Com.sync()

    @staticmethod
    def hook_get_key_down() -> None:
        """Hook Unity.Input.GetKeyDownInt"""
        Com.pipe.write(struct.pack('<b', 0xa))
        Com.sync()

    @staticmethod
    def hook_get_key() -> None:
        """Hook Unity.Input.GetKeyString"""
        Com.pipe.write(struct.pack('<b', 0xb))
        Com.sync()

    @staticmethod
    def hook() -> None:
        """Enable all hooks"""
        Com.hook_focus()
        Com.hook_get_cur_pos()
        Com.hook_get_key_down()
        Com.hook_get_key()

class Inputs:
    """This class handles inputs."""

    Btn = namedtuple("Btn", ["btn", "down", "up"])
    
    btns = {
        "left": Btn(wcon.MK_LBUTTON, wcon.WM_LBUTTONDOWN, wcon.WM_LBUTTONUP), # left mouse button
        "right": Btn(wcon.MK_RBUTTON, wcon.WM_RBUTTONDOWN, wcon.WM_RBUTTONUP), # right mouse button
        "middle": Btn(wcon.MK_MBUTTON, wcon.WM_MBUTTONDOWN, wcon.WM_MBUTTONUP)  # middle mouse button
    }

    specialKeys = {
        "leftShift": 0, # left shift
        "rightShift": 1, # right shift
        "leftControl": 2, # left control
        "rightControl": 3  # right control
    }

    arrow = {
        "left": 276, # left arrow
        "right": 265, # right arrow
        "up": 273, # up arrow
        "down": 274  # down arrow
    }
    
    @staticmethod
    def special(special: str = "leftShift") -> None:
        """Simulate special button to be down"""
        # UnityEngine.Input.GetKeyString
        key = Inputs.specialKeys[special]
        Com.special(key)
    
    @staticmethod
    def restore_special() -> None:
        """Restore special button state"""
        Com.restore_special()

    @staticmethod
    def click(x: int, y: int, button: str = "left") -> None:
        """Click at pixel xy."""
        # No need for checking if special keys are pressed down.
        # When game is out of focus they are not sent :)
        button = Inputs.btns[button]
        Com.set_cur_pos(x + Window.x, y + Window.y)
        win32gui.SendMessage(Window.id, button.down, button.btn, 0)
        win32gui.SendMessage(Window.id, button.up  , button.btn, 0)
        time.sleep(userset.SHORT_SLEEP)
        Com.restore_cur()

    @staticmethod
    def click_drag(x: int, y: int, x2: int, y2: int, button: str = "left") -> None:
        """Simulate drag event from x, y to x2, y2"""
        button = Inputs.btns[button]
        Com.set_cur_pos(x + Window.x, y + Window.y)
        win32gui.SendMessage(Window.id, button.down, button.btn, 0)
        time.sleep(userset.SHORT_SLEEP)
        Com.set_cur_pos(x2 + Window.x, y2 + Window.y)
        win32gui.SendMessage(Window.id, wcon.WM_MOUSEMOVE, 0, 0)
        win32gui.SendMessage(Window.id, button.up, button.btn, 0)
        time.sleep(userset.SHORT_SLEEP)
        Com.restore_cur()

    @staticmethod
    def special_click(x: int, y: int, button: str = "left", special: str = "leftShift"):
        """Clicks at pixel x, y while simulating special button to be down."""
        Inputs.special(special)
        Inputs.click(x + Window.x, y + Window.y, button)
        Inputs.restore_special()

    @staticmethod
    def ctrl_click(x: int, y: int, button: str = "left") -> None:
        """Clicks at pixel x, y while simulating the CTRL button to be down."""
        Inputs.special_click(x + Window.x, y + Window.y, button, "leftControl")

    @staticmethod
    def send_string(s):
        """Send string to game"""
        for c in str(s):
            # UnityEngine.UI.InputField
            vkc = win32api.VkKeyScan(c)
            win32gui.PostMessage(Window.id, wcon.WM_KEYDOWN, vkc, 0)

            # UnityEngine.Input.GetKeyDownInt
            # https://github.com/jamesjlinden/unity-decompiled/blob/master/UnityEngine/UnityEngine/KeyCode.cs
            # Unity"s keycodes matches with ascii
            Com.shortcut(ord(c))
            time.sleep(userset.SHORT_SLEEP)

    @staticmethod
    def send_arrow_press(a: str = "left") -> None:
        """Sends either a left, right, up or down arrow key press"""
        key = Inputs.Arrow[a]
        Com.shortcut(key)
        time.sleep(userset.SHORT_SLEEP)

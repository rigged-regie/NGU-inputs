# About
This is standalone input module for [NGU-IDLE](https://store.steampowered.com/app/1147690/NGU_IDLE/) which allows control of application input. The main objective of this module is to allow easy creation of bots which works while game window is out of focus.

## Disclaimer
This input module breaks manual input. This problem arises from the way input module simulates mouse clicks. When a mouse click is simulated, winapi's GetCursorPos function is forced to return fake mouse position for a short period of time. From the perspective of the game, when the user manually presses the mouse during this time, mouse click occured on fake mouse position. If manual input is needed just stop the script and call ```api.Com.restore_cur()```

## Features
- Simulate mouse click at given coordinates
- Simulate mouse drag and drop
- Simulate keyboard input
- Exactly the same interface as Inputs module in [NGU-scripts](https://github.com/kujan/NGU-scripts)
- __Works when game window is not focused__

## Requirements
- 64bit Windows 7 or later (Window 8 untested)
- Visual Studio 2017 with Visual C++ installed (compilation only)
- Python 3 with pywin32 installed

## How to use it
First compile api-injector solution or use precompiled files in releases section. Then make sure injector.exe and api.dll are in the same directory. Next launch NGU-IDLE and injector in that order, a new window should appear with addinational informations. Use provided api python module to create scripts. For minimal script example, refer to [example.py](https://github.com/rigged-regie/NGU-inputs/blob/master/example.py)

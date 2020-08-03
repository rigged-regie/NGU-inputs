import api
import time

api.Com.init()
api.Com.hook()
api.Window.init()

x = 233
y = 50
offset = 27

for _ in range(10):
    for i in range(10):
        api.Inputs.click(x, y + i * offset)
        time.sleep(0.2)

api.Com.unhook()
api.Com.eject()

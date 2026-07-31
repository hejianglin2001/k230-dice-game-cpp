"""
骰子转盘电机控制 — 抽象接口
接入真实 GPIO 时只需改本文件
"""
from machine import Pin, PWM, FPIOA


class Motor:
    """骰子转盘电机（直流 + H 桥）

    真机接线示例（用到时取消注释）:
        # IN1 pin → GPIO
        # IN2 pin → GPIO
        # EN  pin → PWM（调速）
    """

    def __init__(self, in1_pin=None, in2_pin=None, en_pin=None):
        self._running = False
        self._in1 = None
        self._in2 = None
        self._pwm = None

        if in1_pin is not None and in2_pin is not None:
            self._init_hw(in1_pin, in2_pin, en_pin)

    def _init_hw(self, in1_pin, in2_pin, en_pin):
        fpioa = FPIOA()
        fpioa.set_function(in1_pin, FPIOA.GPIO0 + in1_pin)
        fpioa.set_function(in2_pin, FPIOA.GPIO0 + in2_pin)
        self._in1 = Pin(in1_pin, Pin.OUT, drive=7)
        self._in2 = Pin(in2_pin, Pin.OUT, drive=7)
        if en_pin is not None:
            self._pwm = PWM(en_pin, freq=1000, duty=80)

    def start(self):
        """开始转动"""
        self._running = True
        if self._in1 is not None:
            self._in1.value(1)
            self._in2.value(0)
        if self._pwm is not None:
            self._pwm.duty(80)

    def stop(self):
        """停止转动"""
        self._running = False
        if self._in1 is not None:
            self._in1.value(0)
            self._in2.value(0)
        if self._pwm is not None:
            self._pwm.duty(0)

    def is_running(self):
        return self._running

    def deinit(self):
        self.stop()
        if self._pwm is not None:
            self._pwm.deinit()

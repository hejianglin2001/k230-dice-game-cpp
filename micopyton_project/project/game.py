"""骰子游戏 — 状态机"""
import time
from project.config import *


class DiceGame:
    def __init__(self):
        self.reset()

    def reset(self):
        self.state = STATE_HOME
        self.round = 0
        self.wins  = 0
        self._state_enter = time.time()
        self._judge_buf = []
        self._last_dice = []

    def start_round(self):
        self.round += 1
        self._set_state(STATE_ROLLING)

    def tick(self, dice_values):
        now = time.time()
        if self.state == STATE_ROLLING:
            if now - self._state_enter >= ROLL_TIME:
                self._set_state(STATE_STOPPING)
        elif self.state == STATE_STOPPING:
            if now - self._state_enter >= SETTLE_TIME:
                self._judge_buf = []
                self._set_state(STATE_JUDGING)
        elif self.state == STATE_JUDGING:
            if len(dice_values) >= DICE_COUNT:
                vals = tuple(sorted(dice_values[:DICE_COUNT]))
                self._judge_buf.append(vals)
                if len(self._judge_buf) >= JUDGE_FRAMES and \
                   all(b == self._judge_buf[-1] for b in self._judge_buf[-JUDGE_FRAMES:]):
                    winner = sum(vals) > WIN_SUM
                    self._on_result(winner, list(vals))
            elif now - self._state_enter > 3:
                self.start_round()

    def _on_result(self, winner, dice_vals):
        self.wins = self.wins + 1 if winner else 0
        self._set_state(STATE_WIN if winner else STATE_LOSE)
        self._last_dice = dice_vals

    def next_round(self): self.start_round()
    def to_gift(self): self._set_state(STATE_GIFT)
    def to_home(self): self.reset()

    def gift_level(self):
        if self.wins >= 5: return 2
        if self.wins >= 3: return 1
        return 0

    def _set_state(self, s):
        self.state = s
        self._state_enter = time.time()

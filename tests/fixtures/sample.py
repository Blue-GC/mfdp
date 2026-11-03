import os
from decimal import Decimal as D
from .util import helper, other as o

TAX_RATE: float = 0.2
__all__ = ["calculate_total", "Calculator"]

def validate(x):
    return x > 0

class Calculator(Base):
    """Calc doc.

    More."""
    rate = TAX_RATE

    def __init__(self, items):
        self.items = items

    @staticmethod
    def sq(a):
        return a * a

    async def total(self, tax: float = 0.1, *args, key=None, **kw) -> float:
        validate(self.items)
        helper(tax)
        self.sq(2)
        return os.path.join("a") + len(self.items)

def calculate_total(items, tax=TAX_RATE):
    def inner(v):
        return validate(v)
    return sum(inner(i) for i in items)

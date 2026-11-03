from .util import helper
from .models import Invoice

def compute(invoice: Invoice, rate):
    return helper(invoice.amount) * rate

def unused():
    return 1

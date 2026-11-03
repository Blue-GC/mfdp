def apply_discount(price: float, discount: float) -> float:
    """Return price reduced by a fractional discount."""
    if not 0.0 <= discount < 1.0:
        raise ValueError("discount must be in [0, 1)")
    return price * (1.0 - discount)

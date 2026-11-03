import json
import logging
from dataclasses import dataclass, field
from typing import Dict, List, Optional

from .pricing import apply_discount

LOW_STOCK_THRESHOLD = 5
log = logging.getLogger(__name__)


@dataclass
class Item:
    sku: str
    name: str
    quantity: int = 0
    price: float = 0.0
    tags: List[str] = field(default_factory=list)


class Inventory:
    """Tracks items and stock levels."""

    def __init__(self, items: Optional[List[Item]] = None):
        self._items: Dict[str, Item] = {}
        for item in items or []:
            self.add(item)

    def add(self, item: Item) -> None:
        """Add or replace an item."""
        if item.sku in self._items:
            log.warning("replacing %s", item.sku)
        self._items[item.sku] = item

    def remove(self, sku: str) -> Item:
        return self._items.pop(sku)

    def low_stock(self, threshold: int = LOW_STOCK_THRESHOLD) -> List[Item]:
        result = []
        for item in self._items.values():
            if item.quantity <= threshold:
                result.append(item)
        result.sort(key=lambda i: i.quantity)
        return result

    def total_value(self, discount: float = 0.0) -> float:
        total = 0.0
        for item in self._items.values():
            total += apply_discount(item.price, discount) * item.quantity
        return round(total, 2)

    def to_json(self) -> str:
        payload = [
            {"sku": i.sku, "name": i.name, "quantity": i.quantity, "price": i.price, "tags": i.tags}
            for i in self._items.values()
        ]
        return json.dumps(payload, indent=2, sort_keys=True)


def load_inventory(path: str) -> Inventory:
    with open(path, "r", encoding="utf-8") as handle:
        raw = json.load(handle)
    items = [Item(**entry) for entry in raw]
    log.info("loaded %d items", len(items))
    return Inventory(items)

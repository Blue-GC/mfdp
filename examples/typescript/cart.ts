import { formatMoney, TAX_RATE } from './money';

export interface CartLine {
  sku: string;
  quantity: number;
  unitPrice: number;
}

export type CartId = string;

export class Cart {
  private lines: CartLine[] = [];

  constructor(public readonly id: CartId) {}

  add(line: CartLine): void {
    const existing = this.lines.find((l) => l.sku === line.sku);
    if (existing) {
      existing.quantity += line.quantity;
    } else {
      this.lines.push({ ...line });
    }
  }

  subtotal(): number {
    return this.lines.reduce((sum, l) => sum + l.quantity * l.unitPrice, 0);
  }

  total(): number {
    return this.subtotal() * (1 + TAX_RATE);
  }

  describe(): string {
    return `${this.id}: ${formatMoney(this.total())}`;
  }
}

export function mergeCarts(a: Cart, b: Cart, id: CartId): Cart {
  const merged = new Cart(id);
  for (const cart of [a, b]) {
    (cart as any).lines.forEach((line: CartLine) => merged.add(line));
  }
  return merged;
}

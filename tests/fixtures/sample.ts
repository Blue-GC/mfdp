import { helper, Other as O } from './util';
import * as path from 'path';
import Def from "./def";

export interface Item { id: number; price: number; label?: string; total(tax: number): number }
export type ID = string | number;
export enum Color { Red, Green }
const TAX = 0.2;

/** Calc docs. */
export class Calculator<T extends Item> extends Base implements Item {
  private rate = TAX;
  constructor(private items: T[]) { super(); }
  total(tax: number = 0.1): number {
    helper(tax);
    this.sq(2);
    return path.join('a').length + Color.Red;
  }
  sq = (a: number): number => a * a;
}

export function calculateTotal(items: Item[], tax = TAX): number {
  function inner(v: Item) { return helper(v); }
  return items.map(inner).reduce((a, b) => a + b, 0);
}

export const arrow = async (x: ID): Promise<void> => { console.log(x, Def); };
namespace NS { export function f() {} }

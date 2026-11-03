import { Shape, area } from './b';
import * as lib from './b';

export function describe(s: Shape): number {
  return area(s) + lib.perimeter(s);
}

export interface Shape { w: number; h: number }
export function area(s: Shape): number { return s.w * s.h; }
export function perimeter(s: Shape): number { return 2 * (s.w + s.h); }

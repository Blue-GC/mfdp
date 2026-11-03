export interface WriteParams {
  src?: string[] | string;
  memory?: { filename: string; content: string }[];
  kwrds?: string;
  inc?: boolean;
  include?: boolean;
  inctyps?: boolean;
  include_types?: boolean;
  import_mode?: 'all' | 'relevant' | 'none';
  legacy_inctyps?: boolean;
  budget?: number;
  lang?: string;
  hash?: boolean;
  mode?: 'auto' | 'symbols' | 'full';
  deps?: boolean;
  dep_depth?: number;
  lines?: boolean;
  combined?: boolean;
  output?: string;
  output_dir?: string;
  overwrite?: boolean;
}

export interface Warning {
  code: string;
  message: string;
}

export interface WriteResult {
  output: string;
  outputs: string[];
  summaries: {
    output: string;
    storedBytes: number;
    rawBytes: number;
    files: number;
    symbolsStored: number;
    symbolsParsed: number;
  }[];
  warnings: Warning[];
}

export interface ExtractParams {
  kwrds?: string;
  budget?: number;
  expand?: 'auto' | 'always' | 'never';
  deps?: boolean;
  imports?: boolean;
}

export interface ExtractResult {
  text: string;
  tokens: number;
  tokensAreApproximate: boolean;
  budget: number;
  symbolsStored: number;
  symbolsPresented: number;
  fragmentsAvailable: number;
  fragmentsIncluded: number;
  fragmentsOmitted: number;
  omittedTokens: number;
  warnings: Warning[];
}

export interface InspectResult {
  versionMajor: number;
  versionMinor: number;
  flags: number;
  compression: string;
  storedBytes: number;
  rawBytes: number;
  requestedMode: string;
  resolvedMode: string;
  importMode: string;
  combined: boolean;
  budget: number;
  depDepth: number;
  kwrds: string;
  files: {
    path: string;
    filename: string;
    language: string;
    sourceSize: number;
    hasHash: boolean;
    sha256: string;
    totalSymbols: number;
    storedSymbols: number;
    selectedSymbols: number;
    totalImports: number;
    storedImports: number;
    dependencies: number;
    fragments: number;
  }[];
  warnings: Warning[];
}

export interface VerifyResult {
  ok: boolean;
  entries: {
    recordPath: string;
    originalPath: string;
    status: 'match' | 'stale' | 'no-hash' | 'missing';
    expected: string;
    actual: string;
  }[];
  unmatchedOriginals: string[];
}

export interface MFDPOptions {
  root?: string;
  secret?: string;
  bin?: string;
  timeoutMs?: number;
}

export class MFDPError extends Error {
  code: string;
  details: { file?: string; line?: number; column?: number };
}

export class AuthError extends MFDPError {}

export class MFDP {
  constructor(options?: MFDPOptions);
  readonly root: string;
  readonly env: Record<string, string>;
  rpc<T = unknown>(method: string, params?: object): Promise<T>;
  write(params: WriteParams): Promise<WriteResult>;
  read(artifact: string): Promise<{ valid: boolean; versionMajor: number; versionMinor: number; files: number; symbols: number }>;
  inspect(artifact: string): Promise<InspectResult>;
  extract(artifact: string, params?: ExtractParams): Promise<ExtractResult>;
  verify(artifact: string, src?: string[], originals?: { path: string; content: string }[]): Promise<VerifyResult>;
  info(): Promise<{ displayName: string; slug: string; name: string; author: string; version: string; description: string; license: string; formatVersion: string }>;
}

export function findRoot(start?: string): string;
export function loadEnv(root: string): Record<string, string>;

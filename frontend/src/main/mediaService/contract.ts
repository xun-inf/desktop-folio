import {
  boolValue,
  contract,
  event,
  isRecord,
  method,
  noArgs,
  singleString,
  uint32,
  voidResult,
  type Validator,
} from '../../ipc/contract.js';
export interface MediaStatus {
  connected: boolean;
  message: string;
  phase?: string;
  pid?: number;
  session?: string;
}
export interface NativeWindowRect {
  x: number;
  y: number;
  width: number;
  height: number;
  dpr: number;
}
export interface StreamPreviewFrameMeta {
  id: number;
  token: bigint;
  width: number;
  height: number;
  timestampUs: number;
}
export const isMediaStatus = (value: unknown): value is MediaStatus =>
  isRecord(value) &&
  typeof value.connected === 'boolean' &&
  typeof value.message === 'string' &&
  (value.phase === undefined || typeof value.phase === 'string') &&
  (value.pid === undefined || Number.isInteger(value.pid)) &&
  (value.session === undefined || typeof value.session === 'string');
export const isNativeWindowRect = (value: unknown): value is NativeWindowRect =>
  isRecord(value) &&
  [value.x, value.y, value.width, value.height, value.dpr].every(
    (item) => typeof item === 'number' && Number.isFinite(item),
  ) &&
  Number(value.width) >= 0 &&
  Number(value.height) >= 0 &&
  Number(value.width) <= 16384 &&
  Number(value.height) <= 16384 &&
  Math.abs(Number(value.x)) <= 1048576 &&
  Math.abs(Number(value.y)) <= 1048576 &&
  Number(value.dpr) > 0 &&
  Number(value.dpr) <= 16;
const keyArgs: Validator<[string]> = (value): value is [string] =>
  singleString(value) && /^[a-zA-Z0-9_.-]{1,128}$/.test(value[0]);
const rectArgs: Validator<[string, NativeWindowRect]> = (
  value,
): value is [string, NativeWindowRect] =>
  Array.isArray(value) && value.length === 2 && keyArgs([value[0]]) && isNativeWindowRect(value[1]);
const visibleArgs: Validator<[string, boolean]> = (value): value is [string, boolean] =>
  Array.isArray(value) &&
  value.length === 2 &&
  keyArgs([value[0]]) &&
  typeof value[1] === 'boolean';
const idArgs: Validator<[number]> = (value): value is [number] =>
  Array.isArray(value) && value.length === 1 && uint32(value[0]);
const tokenArgs: Validator<[number, bigint]> = (value): value is [number, bigint] =>
  Array.isArray(value) &&
  value.length === 2 &&
  uint32(value[0]) &&
  typeof value[1] === 'bigint' &&
  value[1] >= 0n &&
  value[1] <= 0xffffffffffffffffn;
export const mediaContract = contract({
  id: 'media',
  methods: {
    getStatus: method(noArgs, isMediaStatus),
    initialize: method(noArgs, boolValue, 'main'),
    stop: method(noArgs, voidResult, 'main'),
  },
  events: { status: event(isMediaStatus) },
});
export const nativeWindowContract = contract({
  id: 'nativeWindowService',
  methods: {
    create: method(keyArgs, voidResult, 'main'),
    setRect: method(rectArgs, voidResult, 'main'),
    setVisible: method(visibleArgs, voidResult, 'main'),
    release: method(keyArgs, voidResult, 'main'),
    releaseAll: method(noArgs, voidResult, 'main'),
    refresh: method(noArgs, voidResult, 'main'),
  },
  events: {},
});
export const streamPreviewContract = contract({
  id: 'streamPreviewService',
  methods: {
    bind: method(idArgs, voidResult),
    unbind: method(idArgs, voidResult),
    releaseVideoFrame: method(tokenArgs, voidResult),
  },
  events: {},
});

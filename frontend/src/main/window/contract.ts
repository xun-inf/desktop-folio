import {
  contract,
  event,
  isRecord,
  method,
  noArgs,
  singleString,
  stringValue,
  voidResult,
  type Validator,
} from '../../ipc/contract.js';
export type WindowPage = 'Main' | 'Settings' | 'SecondScreen';

export interface WindowConfig {
  pageUrls: Record<WindowPage, string>;
  preload: string;
}

export interface WindowSize {
  width: number;
  height: number;
  minWidth?: number;
  minHeight?: number;
}

export interface WindowOptions {
  page: WindowPage;
  id?: string;
  reuse?: boolean;
  center?: boolean;
  title?: string;
  queryParams?: Record<string, string | number | boolean>;
  size?: WindowSize;
}

export interface ResolvedWindowOptions {
  page: WindowPage;
  id?: string;
  reuse?: boolean;
  center?: boolean;
  title?: string;
  queryParams?: Record<string, string | number | boolean>;
  size: WindowSize;
}

export type WindowControl = 'minimize' | 'maximize' | 'unmaximize' | 'toggleMaximize' | 'close';

export interface BrowserViewRect {
  x: number;
  y: number;
  width: number;
  height: number;
  visible: boolean;
}

export interface BrowserViewEvent {
  id: string;
  type: 'loaded' | 'navigated' | 'error';
  url?: string;
  message?: string;
}

export interface WindowState {
  maximized: boolean;
}
export const isWindowState = (value: unknown): value is WindowState =>
  isRecord(value) && typeof value.maximized === 'boolean';
const controlArgs: Validator<[WindowControl]> = (value): value is [WindowControl] =>
  singleString(value) &&
  ['minimize', 'maximize', 'unmaximize', 'toggleMaximize', 'close'].includes(value[0]);
const optionsArgs: Validator<[WindowOptions]> = (value): value is [WindowOptions] => {
  if (!Array.isArray(value) || value.length !== 1 || !isRecord(value[0])) return false;
  const options = value[0];
  if (
    !['Settings', 'SecondScreen'].includes(String(options.page)) ||
    (options.id !== undefined &&
      (typeof options.id !== 'string' ||
        !options.id ||
        options.id.length > 256 ||
        ['main', 'child'].includes(options.id))) ||
    (options.reuse !== undefined && typeof options.reuse !== 'boolean') ||
    (options.center !== undefined && typeof options.center !== 'boolean') ||
    (options.title !== undefined &&
      (typeof options.title !== 'string' || options.title.length > 256))
  )
    return false;
  if (
    options.queryParams !== undefined &&
    (!isRecord(options.queryParams) ||
      Object.entries(options.queryParams).some(
        ([key, value]) =>
          key !== 'streamId' ||
          !['string', 'number', 'boolean'].includes(typeof value) ||
          String(value).length > 64,
      ))
  )
    return false;
  if (options.size !== undefined) {
    if (
      !isRecord(options.size) ||
      !Number.isInteger(options.size.width) ||
      !Number.isInteger(options.size.height)
    )
      return false;
    for (const key of ['width', 'height', 'minWidth', 'minHeight']) {
      const size = options.size[key];
      if (
        size !== undefined &&
        (!Number.isInteger(size) || Number(size) <= 0 || Number(size) > 16384)
      )
        return false;
    }
  }
  return true;
};
const viewId = (value: unknown): value is string =>
  typeof value === 'string' && value.length > 0 && value.length <= 128;
const viewIdArgs: Validator<[string]> = (value): value is [string] =>
  Array.isArray(value) && value.length === 1 && viewId(value[0]);
const viewUrlArgs: Validator<[string, string]> = (value): value is [string, string] => {
  if (
    !Array.isArray(value) ||
    value.length !== 2 ||
    !viewId(value[0]) ||
    typeof value[1] !== 'string' ||
    value[1].length > 1048576
  )
    return false;
  try {
    const url = new URL(value[1]);
    return ['http:', 'https:'].includes(url.protocol) || value[1].startsWith('data:text/html');
  } catch {
    return false;
  }
};
const rectArgs: Validator<[string, BrowserViewRect]> = (
  value,
): value is [string, BrowserViewRect] => {
  if (!Array.isArray(value) || value.length !== 2 || !viewId(value[0]) || !isRecord(value[1]))
    return false;
  const rect = value[1];
  return (
    [rect.x, rect.y, rect.width, rect.height].every(
      (item) => typeof item === 'number' && Number.isFinite(item),
    ) &&
    Number(rect.width) >= 0 &&
    Number(rect.height) >= 0 &&
    Number(rect.width) <= 16384 &&
    Number(rect.height) <= 16384 &&
    Math.abs(Number(rect.x)) <= 1048576 &&
    Math.abs(Number(rect.y)) <= 1048576 &&
    typeof rect.visible === 'boolean'
  );
};
const isViewEvent = (value: unknown): value is BrowserViewEvent =>
  isRecord(value) &&
  viewId(value.id) &&
  ['loaded', 'navigated', 'error'].includes(String(value.type)) &&
  (value.url === undefined || typeof value.url === 'string') &&
  (value.message === undefined || typeof value.message === 'string');
export const windowsContract = contract({
  id: 'windows',
  methods: {
    control: method(controlArgs, isWindowState),
    ready: method(noArgs, voidResult),
    getState: method(noArgs, isWindowState),
    showWindow: method(optionsArgs, stringValue),
    closeWindow: method(singleString, voidResult),
    closeAllWindow: method(noArgs, voidResult),
  },
  events: { state: event(isWindowState) },
});
export const browserViewContract = contract({
  id: 'browserView',
  methods: {
    create: method(viewUrlArgs, voidResult, 'main'),
    load: method(viewUrlArgs, voidResult, 'main'),
    setRect: method(rectArgs, voidResult, 'main'),
    release: method(viewIdArgs, voidResult, 'main'),
  },
  events: { event: event(isViewEvent, 'main') },
});

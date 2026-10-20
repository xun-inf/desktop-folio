// Browser-safe contracts. Descriptors are explicit data, never reflected implementation objects.
export type Validator<T> = (value: unknown) => value is T;
export type Access = 'page' | 'main';
export interface Method<A extends unknown[] = any[], R = any> {
  readonly args: Validator<A>;
  readonly result: Validator<R>;
  readonly access: Access;
}
export interface Event<T = any> {
  readonly payload: Validator<T>;
  readonly access: Access;
}
export interface Contract {
  readonly id: string;
  readonly methods: Readonly<Record<string, Method>>;
  readonly events: Readonly<Record<string, Event>>;
}
export type Args<M> = M extends Method<infer A, any> ? A : never;
export type Result<M> = M extends Method<any, infer R> ? R : never;
export type Payload<E> = E extends Event<infer T> ? T : never;
export type Client<C extends Contract> = {
  readonly [K in keyof C['methods']]: (
    ...args: Args<C['methods'][K]>
  ) => Promise<Result<C['methods'][K]>>;
};
export type Subscription = (() => void) & { readonly ready: Promise<void> };
export const IPC_CHANNELS = {
  invoke: 'ipc:invoke',
  subscribe: 'ipc:subscribe',
  unsubscribe: 'ipc:unsubscribe',
  event: 'ipc:event',
} as const;
export type Reply<T = unknown> = { ok: true; value: T } | { ok: false; error: string };
export function ownValue(object: unknown, name: string): unknown {
  if (!object || typeof object !== 'object') return undefined;
  return Object.getOwnPropertyDescriptor(object, name)?.value;
}
export function isRecord(value: unknown): value is Record<string, unknown> {
  return (
    !!value &&
    typeof value === 'object' &&
    !Array.isArray(value) &&
    [Object.prototype, null].includes(Object.getPrototypeOf(value)) &&
    Object.values(Object.getOwnPropertyDescriptors(value)).every((property) => 'value' in property)
  );
}
export const identifier = (value: unknown): value is string =>
  typeof value === 'string' && /^[a-zA-Z][a-zA-Z0-9]{0,63}$/.test(value);
export function method<A extends unknown[], R>(
  args: Validator<A>,
  result: Validator<R>,
  access: Access = 'page',
): Method<A, R> {
  return Object.freeze({ args, result, access });
}
export function event<T>(payload: Validator<T>, access: Access = 'page'): Event<T> {
  return Object.freeze({ payload, access });
}
export function contract<const C extends Contract>(definition: C): C {
  if (!identifier(definition.id)) throw new Error('Invalid capability ID');
  for (const table of [definition.methods, definition.events]) {
    if (!isRecord(table)) throw new Error('Contract entries must be own data properties');
    for (const key of Object.keys(table)) {
      if (!identifier(key) || ['constructor', 'toString', '__proto__'].includes(key))
        throw new Error('Invalid contract entry');
      Object.freeze(table[key]);
    }
    Object.freeze(table);
  }
  return Object.freeze(definition);
}
export const noArgs: Validator<[]> = (value): value is [] =>
  Array.isArray(value) && value.length === 0;
export const voidResult: Validator<void> = (value): value is void => value === undefined;
export const stringValue: Validator<string> = (value): value is string => typeof value === 'string';
export const boolValue: Validator<boolean> = (value): value is boolean =>
  typeof value === 'boolean';
export const singleString: Validator<[string]> = (value): value is [string] =>
  Array.isArray(value) && value.length === 1 && stringValue(value[0]);
export const uint32 = (value: unknown): value is number =>
  Number.isInteger(value) && Number(value) >= 0 && Number(value) <= 0xffffffff;

import {
  IPC_CHANNELS,
  isRecord,
  ownValue,
  type Client,
  type Contract,
  type Payload,
  type Reply,
  type Subscription,
} from './contract.js';
interface Transport {
  invoke(channel: string, request: unknown): Promise<unknown>;
  on(channel: string, callback: (_event: unknown, message: unknown) => void): unknown;
  removeListener(channel: string, callback: (_event: unknown, message: unknown) => void): unknown;
}
interface Entry {
  capability: string;
  event: string;
  token: string;
  callbacks: Set<{ callback(value: any): void }>;
  ready: Promise<void>;
  validate(value: unknown): boolean;
}
export class IpcClient {
  private readonly subscriptions = new Map<string, Entry>();
  private sequence = 0;
  private closed = false;
  private readonly listener = (_event: unknown, value: unknown): void => {
    if (this.closed || !isRecord(value)) return;
    const entry = this.subscriptions.get(value.capability + ':' + value.event);
    if (!entry || value.token !== entry.token || !entry.validate(value.payload)) return;
    for (const registration of [...entry.callbacks]) {
      if (!entry.callbacks.has(registration)) continue;
      try {
        registration.callback(value.payload);
      } catch (error) {
        this.report(error);
      }
    }
  };
  constructor(
    private readonly transport: Transport,
    private readonly report: (error: unknown) => void = console.error,
  ) {
    transport.on(IPC_CHANNELS.event, this.listener);
  }
  bind<C extends Contract>(definition: C): Client<C> {
    const client: Record<string, (...args: unknown[]) => Promise<unknown>> = {};
    for (const name of Object.keys(definition.methods)) {
      const descriptor = definition.methods[name];
      client[name] = async (...args) => {
        if (this.closed) throw new Error('Page document retired: ' + definition.id + '.' + name);
        if (!descriptor.args(args)) throw new TypeError('Invalid IPC arguments');
        const value = await this.request(IPC_CHANNELS.invoke, {
          capability: definition.id,
          method: name,
          args,
        });
        if (!descriptor.result(value)) throw new Error('Invalid IPC result');
        return value;
      };
    }
    return Object.freeze(client) as Client<C>;
  }
  subscribe<C extends Contract, K extends keyof C['events'] & string>(
    definition: C,
    event: K,
    callback: (payload: Payload<C['events'][K]>) => void,
  ): Subscription {
    if (this.closed) throw new Error('Page document retired');
    const descriptor = ownValue(definition.events, event) as C['events'][K] | undefined;
    if (!descriptor) throw new Error('Unknown capability event');
    if (typeof callback !== 'function') throw new TypeError('IPC callback must be a function');
    const key = definition.id + ':' + event;
    let entry = this.subscriptions.get(key);
    const first = !entry;
    if (!entry) {
      entry = {
        capability: definition.id,
        event,
        token: String(++this.sequence),
        callbacks: new Set(),
        ready: Promise.resolve(),
        validate: descriptor.payload,
      };
      this.subscriptions.set(key, entry);
    }
    const registration = { callback },
      captured = entry;
    entry.callbacks.add(registration);
    if (first) {
      captured.ready = this.request(IPC_CHANNELS.subscribe, {
        capability: captured.capability,
        event,
        token: captured.token,
      }).then(() => {});
      // ready stays rejectable for callers; attach a handler immediately to avoid unhandled rejection.
      void captured.ready.catch((error) => {
        if (this.subscriptions.get(key) === captured) this.subscriptions.delete(key);
        captured.callbacks.clear();
        this.report(error);
      });
    }
    let disposed = false;
    const dispose = (() => {
      if (disposed) return;
      disposed = true;
      captured.callbacks.delete(registration);
      if (captured.callbacks.size !== 0 || this.subscriptions.get(key) !== captured) return;
      this.subscriptions.delete(key);
      this.cancel(captured);
    }) as Subscription;
    Object.defineProperty(dispose, 'ready', { value: captured.ready, enumerable: true });
    return dispose;
  }
  close(): void {
    if (this.closed) return;
    this.closed = true;
    this.transport.removeListener(IPC_CHANNELS.event, this.listener);
    // Main owns navigation/destruction/global cleanup. The outgoing document must not
    // schedule IPC after its server has already been stopped by application shutdown.
    for (const entry of this.subscriptions.values()) entry.callbacks.clear();
    this.subscriptions.clear();
  }
  private cancel(entry: Entry): void {
    void entry.ready
      .then(() => {
        if (!this.closed)
          return this.request(IPC_CHANNELS.unsubscribe, {
            capability: entry.capability,
            event: entry.event,
            token: entry.token,
          });
      })
      .catch((error) => {
        if (!this.closed) this.report(error);
      });
  }
  private async request(channel: string, request: unknown): Promise<unknown> {
    const reply = (await this.transport.invoke(channel, request)) as Reply;
    if (!isRecord(reply) || typeof reply.ok !== 'boolean') throw new Error('Invalid IPC response');
    if (!reply.ok)
      throw new Error(typeof reply.error === 'string' ? reply.error : 'IPC operation failed');
    return reply.value;
  }
}

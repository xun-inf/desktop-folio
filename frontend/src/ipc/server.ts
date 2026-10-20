import type { IpcMain, IpcMainInvokeEvent, WebContents, WebFrameMain } from 'electron';
import {
  IPC_CHANNELS,
  identifier,
  isRecord,
  ownValue,
  type Access,
  type Args,
  type Contract,
  type Payload,
  type Reply,
  type Result,
} from './contract.js';

export interface Caller {
  readonly id: string;
  readonly sender: WebContents;
  readonly frame: WebFrameMain;
}
export type Handlers<C extends Contract> = {
  [K in keyof C['methods']]: (
    caller: Caller,
    ...args: Args<C['methods'][K]>
  ) => Result<C['methods'][K]> | Promise<Result<C['methods'][K]>>;
};
export type EventSources<C extends Contract> = {
  [K in keyof C['events']]?: (
    caller: Caller,
    emit: (payload: Payload<C['events'][K]>) => void,
  ) => () => void;
};
interface Binding {
  contract: Contract;
  handlers: object;
  events: object;
}
interface Document {
  sender: WebContents;
  frame: WebFrameMain;
  token: string;
  process: number;
  routing: number;
  url: string;
  navigating: boolean;
  retired: boolean;
  cleanup(): void;
}
interface Registration {
  document: Document;
  capability: string;
  event: string;
  token: string;
  dispose(): void;
}
export interface ServerOptions {
  ipcMain: Pick<IpcMain, 'handle' | 'removeHandler'>;
  resolvePage(event: IpcMainInvokeEvent): { id: string; pageUrl: string };
  report(error: unknown): void;
}
export class IpcServer {
  private readonly bindings = new Map<string, Binding>();
  private readonly documents = new Map<number, Document>();
  private readonly subscriptions = new Map<string, Registration>();
  private stopped = false;
  constructor(private readonly options: ServerOptions) {
    options.ipcMain.handle(IPC_CHANNELS.invoke, (event, request) =>
      this.boundary(() => this.invoke(event, request)),
    );
    options.ipcMain.handle(IPC_CHANNELS.subscribe, (event, request) =>
      this.boundary(() => this.subscribe(event, request)),
    );
    options.ipcMain.handle(IPC_CHANNELS.unsubscribe, (event, request) =>
      this.boundary(() => this.unsubscribe(event, request)),
    );
  }
  register<C extends Contract>(
    definition: C,
    handlers: Handlers<C>,
    events: EventSources<C> = {},
  ): void {
    if (this.stopped || this.bindings.has(definition.id))
      throw new Error('Duplicate or closed capability: ' + definition.id);
    if (!isRecord(handlers) || !isRecord(events))
      throw new Error('Handlers must be own data properties');
    for (const key of Object.keys(definition.methods)) {
      if (typeof ownValue(handlers, key) !== 'function')
        throw new Error('Missing IPC handler: ' + key);
    }
    if (
      Object.keys(handlers).some((key) => !ownValue(definition.methods, key)) ||
      Object.keys(events).some((key) => !ownValue(definition.events, key))
    )
      throw new Error('Undeclared IPC binding');
    this.bindings.set(definition.id, { contract: definition, handlers, events });
  }
  emit<C extends Contract, K extends keyof C['events'] & string>(
    definition: C,
    name: K,
    sender: WebContents,
    payload: Payload<C['events'][K]>,
  ): void {
    const descriptor = ownValue(definition.events, name) as C['events'][K] | undefined;
    if (!descriptor?.payload(payload))
      throw new Error('Invalid event payload: ' + definition.id + '.' + name);
    for (const [key, registration] of [...this.subscriptions]) {
      if (
        registration.document.sender === sender &&
        registration.capability === definition.id &&
        registration.event === name
      )
        this.deliver(key, registration, payload);
    }
  }
  get subscriptionCount(): number {
    return this.subscriptions.size;
  }
  dispose(): void {
    if (this.stopped) return;
    this.stopped = true;
    for (const key of [...this.subscriptions.keys()]) this.clear(key);
    for (const document of this.documents.values()) document.cleanup();
    this.documents.clear();
    for (const channel of [IPC_CHANNELS.invoke, IPC_CHANNELS.subscribe, IPC_CHANNELS.unsubscribe])
      this.options.ipcMain.removeHandler(channel);
    this.bindings.clear();
  }
  private async boundary(action: () => unknown): Promise<Reply> {
    try {
      return { ok: true, value: await action() };
    } catch (error) {
      return { ok: false, error: error instanceof Error ? error.message : 'IPC operation failed' };
    }
  }
  private caller(
    event: IpcMainInvokeEvent,
    allowPaused = false,
  ): { caller: Caller; document: Document } {
    if (this.stopped) throw new Error('IPC server is closed');
    const page = this.options.resolvePage(event),
      sender = event.sender,
      frame = event.senderFrame;
    const normalize = (value: string): string => {
      const url = new URL(value);
      url.search = '';
      url.hash = '';
      return url.href;
    };
    if (
      sender.isDestroyed() ||
      !frame ||
      frame.detached ||
      frame !== sender.mainFrame ||
      normalize(frame.url) !== normalize(page.pageUrl)
    )
      throw new Error('Untrusted page renderer');
    let document = this.documents.get(sender.id);
    if (document?.retired) throw new Error('Page document retired');
    if (document && !this.current(document)) {
      this.retire(document);
      document.cleanup();
      this.documents.delete(sender.id);
      document = undefined;
    }
    if (!document) {
      document = {
        sender,
        frame,
        token: frame.frameToken,
        process: frame.processId,
        routing: frame.routingId,
        url: normalize(frame.url),
        navigating: false,
        retired: false,
        cleanup() {},
      };
      const tracked = document;
      const retire = (): void => this.retire(tracked);
      const navigation = (details: { isMainFrame: boolean; isSameDocument: boolean }): void => {
        if (details.isMainFrame && !details.isSameDocument) tracked.navigating = true;
      };
      const settled = (): void => {
        // A cancelled navigation can keep the original document. Resume only after loading
        // settles and the complete captured identity/URL still belongs to that document.
        if (
          tracked.navigating &&
          this.current(tracked) &&
          !sender.isLoadingMainFrame() &&
          normalize(tracked.frame.url) === tracked.url
        )
          tracked.navigating = false;
      };
      const committed = (
        _event: unknown,
        _url: string,
        _code: number,
        _status: string,
        isMainFrame: boolean,
      ): void => {
        if (isMainFrame) {
          retire();
          tracked.cleanup();
          this.documents.delete(sender.id);
        }
      };
      const destroyed = (): void => {
        retire();
        tracked.cleanup();
        this.documents.delete(sender.id);
      };
      sender.on('did-start-navigation', navigation);
      sender.on('did-stop-loading', settled);
      sender.on('did-frame-navigate', committed);
      sender.on('render-process-gone', retire);
      sender.once('destroyed', destroyed);
      tracked.cleanup = () => {
        sender.removeListener('did-start-navigation', navigation);
        sender.removeListener('did-frame-navigate', committed);
        sender.removeListener('did-stop-loading', settled);
        sender.removeListener('render-process-gone', retire);
        sender.removeListener('destroyed', destroyed);
      };
      this.documents.set(sender.id, tracked);
    }
    if (document.navigating && !allowPaused) throw new Error('Page navigation pending');
    return { caller: { id: page.id, sender, frame }, document };
  }
  private request(
    value: unknown,
    operation: 'method' | 'event',
  ): { capability: string; name: string; binding: Binding } {
    if (
      !isRecord(value) ||
      !identifier(ownValue(value, 'capability')) ||
      !identifier(ownValue(value, operation))
    )
      throw new Error('Invalid IPC request');
    const capability = value.capability as string,
      name = value[operation] as string;
    const binding = this.bindings.get(capability);
    if (
      !binding ||
      !ownValue(operation === 'method' ? binding.contract.methods : binding.contract.events, name)
    )
      throw new Error('Unknown IPC ' + operation);
    return { capability, name, binding };
  }
  private async invoke(event: IpcMainInvokeEvent, value: unknown): Promise<unknown> {
    const { caller } = this.caller(event),
      { name, binding } = this.request(value, 'method');
    const descriptor = binding.contract.methods[name];
    this.authorize(descriptor.access, caller);
    const args = ownValue(value, 'args');
    if (!descriptor.args(args)) throw new TypeError('Invalid IPC arguments');
    const handler = ownValue(binding.handlers, name) as (
      caller: Caller,
      ...args: unknown[]
    ) => unknown;
    const result = await handler(caller, ...args);
    if (!descriptor.result(result)) throw new TypeError('Invalid IPC result');
    return result;
  }
  private subscribe(event: IpcMainInvokeEvent, value: unknown): void {
    const { caller, document } = this.caller(event),
      { capability, name, binding } = this.request(value, 'event');
    const descriptor = binding.contract.events[name],
      token = ownValue(value, 'token');
    this.authorize(descriptor.access, caller);
    if (typeof token !== 'string' || !/^[0-9]{1,16}$/.test(token))
      throw new Error('Invalid subscription token');
    const key = caller.sender.id + ':' + token;
    if (this.subscriptions.has(key)) throw new Error('Duplicate subscription token');
    if (
      [...this.subscriptions.values()].filter((value) => value.document === document).length >= 256
    )
      throw new Error('Too many subscriptions');
    const registration: Registration = { document, capability, event: name, token, dispose() {} };
    this.subscriptions.set(key, registration); // Register before source: synchronous initial events must arrive.
    try {
      const source = ownValue(binding.events, name) as
        | ((caller: Caller, emit: (payload: unknown) => void) => () => void)
        | undefined;
      if (source) {
        const dispose = source(caller, (payload) => {
          if (!descriptor.payload(payload)) {
            this.options.report(new Error('Invalid event payload'));
            return;
          }
          this.deliver(key, registration, payload);
        });
        if (typeof dispose !== 'function') throw new Error('Invalid event disposer');
        registration.dispose = dispose;
        if (this.subscriptions.get(key) !== registration) dispose();
      }
    } catch (error) {
      this.clear(key);
      throw error;
    }
  }
  private unsubscribe(event: IpcMainInvokeEvent, value: unknown): void {
    const { caller } = this.caller(event, true),
      { capability, name } = this.request(value, 'event');
    const token = ownValue(value, 'token');
    if (typeof token !== 'string') throw new Error('Invalid subscription token');
    const key = caller.sender.id + ':' + token,
      registration = this.subscriptions.get(key);
    if (registration && registration.capability === capability && registration.event === name)
      this.clear(key);
  }
  private current(document: Document): boolean {
    try {
      return (
        !document.retired &&
        !document.sender.isDestroyed() &&
        !document.frame.detached &&
        document.sender.mainFrame === document.frame &&
        document.frame.frameToken === document.token &&
        document.frame.processId === document.process &&
        document.frame.routingId === document.routing
      );
    } catch {
      return false;
    }
  }
  private authorize(access: Access, caller: Caller): void {
    if (access === 'main' && caller.id !== 'main') throw new Error('Main page required');
  }
  private deliver(key: string, registration: Registration, payload: unknown): void {
    if (this.subscriptions.get(key) !== registration) return;
    if (!this.current(registration.document)) {
      this.clear(key);
      return;
    }
    if (registration.document.navigating) return;
    try {
      registration.document.frame.send(IPC_CHANNELS.event, {
        capability: registration.capability,
        event: registration.event,
        token: registration.token,
        payload,
      });
    } catch (error) {
      this.clear(key);
      this.options.report(error);
    }
  }
  private retire(document: Document): void {
    document.retired = true;
    for (const [key, registration] of [...this.subscriptions])
      if (registration.document === document) this.clear(key);
  }
  private clear(key: string): void {
    const registration = this.subscriptions.get(key);
    this.subscriptions.delete(key);
    try {
      registration?.dispose();
    } catch (error) {
      this.options.report(error);
    }
  }
}

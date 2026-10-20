import type { BizModulesApi, BizModulesStatus, TestModuleApi } from './addon.js';
import { createModuleRegistry, ownMethod } from './moduleRegistry.js';

type ContainerState = 'created' | 'initializing' | 'ready' | 'closing' | 'closed';

export class BizModulesContainer {
  private native: BizModulesApi | null = null;
  private modules: { testModule: TestModuleApi } | null = null;
  private state: ContainerState = 'created';
  private initializePromise: Promise<void> | null = null;
  private shutdownPromise: Promise<void> | null = null;
  private readonly subscriptions = new Set<() => void>();

  install(native: BizModulesApi): void {
    if (this.native || this.state !== 'created')
      throw new Error('BizModulesContainer cannot be installed');
    if (!native || typeof native !== 'object') throw new Error('Invalid node-bizmodules host');
    for (const method of ['initialize', 'shutdown', 'getStatus']) ownMethod(native, method);
    const modules = createModuleRegistry(native);
    this.native = native;
    this.modules = modules;
  }

  initialize(): Promise<void> {
    if (this.state === 'ready') return Promise.resolve();
    if (this.state === 'initializing') return this.initializePromise!;
    if (this.state !== 'created') return Promise.reject(new Error('BizModulesContainer is closed'));
    if (!this.native)
      return Promise.reject(new Error('BizModulesContainer must be installed before use'));
    const native = this.native;
    this.state = 'initializing';
    this.initializePromise = Promise.resolve()
      .then(() => {
        if (this.state !== 'initializing') throw new Error('BizModulesContainer is closed');
        return native.initialize();
      })
      .then(
        () => {
          if (this.state !== 'initializing') throw new Error('BizModulesContainer is closed');
          this.state = 'ready';
        },
        (error) => {
          if (this.state === 'initializing') this.state = 'created';
          throw error;
        },
      );
    return this.initializePromise;
  }

  getStatus(): BizModulesStatus {
    if (!this.native) throw new Error('BizModulesContainer must be installed before use');
    return this.native.getStatus();
  }

  get testModule(): {
    echo(value: string): Promise<string>;
    publish(value: string): Promise<string>;
    onChanged(callback: (value: string) => void): () => void;
  } {
    const requireModule = (): TestModuleApi => {
      if (this.state !== 'ready' || !this.modules)
        throw new Error('BizModulesContainer is not ready');
      return this.modules.testModule;
    };
    return {
      echo: (value) => {
        if (typeof value !== 'string') throw new TypeError('Invalid module arguments');
        return requireModule().echo(value);
      },
      publish: (value) => {
        if (typeof value !== 'string') throw new TypeError('Invalid module arguments');
        return requireModule().publish(value);
      },
      onChanged: (handler) => {
        if (typeof handler !== 'function')
          throw new TypeError('Module callback must be a function');
        let disposed = false;
        const subscription = requireModule().subscribe('changed', (payload) => {
          if (!disposed && this.state === 'ready') handler(payload);
        });
        const dispose = (): void => {
          if (disposed) return;
          disposed = true;
          this.subscriptions.delete(dispose);
          subscription.dispose();
        };
        this.subscriptions.add(dispose);
        return dispose;
      },
    };
  }

  shutdown(): Promise<void> {
    if (this.shutdownPromise) return this.shutdownPromise;
    this.state = 'closing';
    this.shutdownPromise = Promise.resolve().then(async () => {
      const failures: unknown[] = [];
      for (const dispose of [...this.subscriptions]) {
        try {
          dispose();
        } catch (error) {
          failures.push(error);
        }
      }
      this.subscriptions.clear();
      await this.initializePromise?.catch(() => {});
      try {
        await this.native?.shutdown();
      } catch (error) {
        failures.push(error);
      } finally {
        this.state = 'closed';
      }
      if (failures.length === 1) throw failures[0];
      if (failures.length > 1)
        throw new AggregateError(failures, 'BizModulesContainer cleanup failed');
    });
    return this.shutdownPromise;
  }
}

export const bizModules = new BizModulesContainer();

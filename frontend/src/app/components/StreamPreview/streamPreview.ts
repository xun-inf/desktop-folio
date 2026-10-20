import type { StreamPreviewFrameMeta, StreamPreviewSubscribe } from '../../types';

// 组件内部的绘制控制器，负责最新帧调度和纹理资源释放。
// Canvas backing size 跟随输入帧，页面布局由调用方通过 CSS 设置。
export class StreamPreviewController {
  private pending: { frame: VideoFrame; meta: StreamPreviewFrameMeta } | null = null;
  private animation = 0;
  private unsubscribe: (() => Promise<void>) | null = null;
  private disposed = false;
  private readonly context: WebGLRenderingContext;
  private program: WebGLProgram | null = null;
  private vertices: WebGLBuffer | null = null;
  private texture: WebGLTexture | null = null;
  private disposal: Promise<void> | null = null;

  private constructor(
    private readonly canvas: HTMLCanvasElement,
    private readonly onError: (error: unknown) => void,
  ) {
    const context = canvas.getContext('webgl', {
      alpha: false,
      antialias: false,
      depth: false,
      stencil: false,
    });
    if (!context) throw new Error('WebGL unavailable');
    this.context = context;
    this.initialize();
    canvas.addEventListener('webglcontextlost', this.onContextLost);
  }

  static async attach(
    canvas: HTMLCanvasElement,
    id: number,
    subscribe: StreamPreviewSubscribe,
    onError: (error: unknown) => void = console.error,
  ): Promise<StreamPreviewController> {
    const presenter = new StreamPreviewController(canvas, onError);
    try {
      presenter.unsubscribe = await subscribe(id, (frame, meta) => presenter.offer(frame, meta));
      window.addEventListener('pagehide', presenter.onPageHide);
      document.addEventListener('visibilitychange', presenter.onVisibilityChanged);
      return presenter;
    } catch (error) {
      await presenter.dispose();
      throw error;
    }
  }

  dispose(): Promise<void> {
    if (this.disposal) return this.disposal;
    this.disposed = true;
    this.clearPending();
    window.removeEventListener('pagehide', this.onPageHide);
    document.removeEventListener('visibilitychange', this.onVisibilityChanged);
    this.canvas.removeEventListener('webglcontextlost', this.onContextLost);
    this.releaseResources();
    const unsubscribe = this.unsubscribe;
    this.unsubscribe = null;
    this.disposal = Promise.resolve().then(() => unsubscribe?.());
    return this.disposal;
  }

  private readonly onContextLost = (event: Event): void => {
    event.preventDefault();
    this.clearPending();
    // 恢复后的下一帧重新创建 GPU 资源。
    this.program = null;
    this.vertices = null;
    this.texture = null;
  };

  private initialize(): void {
    const gl = this.context;
    const shaders: WebGLShader[] = [];
    try {
      this.program = gl.createProgram();
      this.vertices = gl.createBuffer();
      this.texture = gl.createTexture();
      if (!this.program || !this.vertices || !this.texture)
        throw new Error('WebGL resource allocation failed');
      const sources: Array<[number, string]> = [
        [
          gl.VERTEX_SHADER,
          `attribute vec2 position;
          varying vec2 uv;
          void main() {
            gl_Position = vec4(position, 0.0, 1.0);
            uv = vec2((position.x + 1.0) * 0.5, (1.0 - position.y) * 0.5);
          }`,
        ],
        [
          gl.FRAGMENT_SHADER,
          `precision mediump float;
          varying vec2 uv;
          uniform sampler2D frameTexture;
          void main() { gl_FragColor = texture2D(frameTexture, uv); }`,
        ],
      ];
      for (const [type, source] of sources) {
        const shader = gl.createShader(type);
        if (!shader) throw new Error('WebGL shader allocation failed');
        shaders.push(shader);
        gl.shaderSource(shader, source);
        gl.compileShader(shader);
        if (!gl.getShaderParameter(shader, gl.COMPILE_STATUS))
          throw new Error(gl.getShaderInfoLog(shader) || 'WebGL shader compilation failed');
        gl.attachShader(this.program, shader);
      }
      gl.linkProgram(this.program);
      if (!gl.getProgramParameter(this.program, gl.LINK_STATUS))
        throw new Error(gl.getProgramInfoLog(this.program) || 'WebGL program link failed');
      gl.useProgram(this.program);
      gl.bindBuffer(gl.ARRAY_BUFFER, this.vertices);
      gl.bufferData(
        gl.ARRAY_BUFFER,
        new Float32Array([-1, -1, 1, -1, -1, 1, 1, 1]),
        gl.STATIC_DRAW,
      );
      const position = gl.getAttribLocation(this.program, 'position');
      gl.enableVertexAttribArray(position);
      gl.vertexAttribPointer(position, 2, gl.FLOAT, false, 0, 0);
      gl.activeTexture(gl.TEXTURE0);
      gl.bindTexture(gl.TEXTURE_2D, this.texture);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
      gl.uniform1i(gl.getUniformLocation(this.program, 'frameTexture'), 0);
    } catch (error) {
      this.releaseResources();
      throw error;
    } finally {
      for (const shader of shaders) {
        if (this.program) gl.detachShader(this.program, shader);
        gl.deleteShader(shader);
      }
    }
  }

  private releaseResources(): void {
    // 当前绑定的 program 会延迟删除；先解除绑定，组件卸载时即可释放引用。
    this.context.useProgram(null);
    this.context.bindBuffer(this.context.ARRAY_BUFFER, null);
    this.context.bindTexture(this.context.TEXTURE_2D, null);
    this.context.deleteTexture(this.texture);
    this.context.deleteBuffer(this.vertices);
    this.context.deleteProgram(this.program);
    this.texture = null;
    this.vertices = null;
    this.program = null;
  }

  private readonly onPageHide = (): void => {
    void this.dispose().catch(() => {});
  };
  private readonly onVisibilityChanged = (): void => {
    if (document.hidden) this.clearPending();
  };

  private clearPending(): void {
    cancelAnimationFrame(this.animation);
    this.animation = 0;
    const pending = this.pending;
    this.pending = null;
    if (!pending) return;
    pending.frame.close();
    this.release(pending.meta);
  }

  private offer(frame: VideoFrame, meta: StreamPreviewFrameMeta): void {
    if (this.disposed || document.hidden || this.context.isContextLost()) {
      frame.close();
      this.release(meta);
      return;
    }
    if (this.pending) {
      this.pending.frame.close();
      this.release(this.pending.meta);
    }
    this.pending = { frame, meta };
    if (!this.animation) this.animation = requestAnimationFrame(() => this.draw());
  }

  private draw(): void {
    this.animation = 0;
    const pending = this.pending;
    this.pending = null;
    if (!pending) return;
    const frame = pending.frame;
    try {
      const gl = this.context;
      if (gl.isContextLost()) return;
      if (this.canvas.width !== frame.displayWidth) this.canvas.width = frame.displayWidth;
      if (this.canvas.height !== frame.displayHeight) this.canvas.height = frame.displayHeight;
      if (!this.program) this.initialize();
      gl.viewport(0, 0, this.canvas.width, this.canvas.height);
      // VideoFrame 直接作为纹理源；不通过 Canvas 2D 或 CPU 像素回读中转。
      gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, gl.RGBA, gl.UNSIGNED_BYTE, frame);
      gl.drawArrays(gl.TRIANGLE_STRIP, 0, 4);
    } catch (error) {
      this.onError(error);
    } finally {
      frame.close();
      this.release(pending.meta);
    }
  }

  private release(meta: StreamPreviewFrameMeta): void {
    void window.folioMediaService.streamPreviewService
      .releaseVideoFrame(meta.id, meta.token)
      .catch(() => {});
  }
}

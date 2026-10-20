// Internal document context for preview ownership; transport dispatch lives in ipc/.
import type { WebContents, WebFrameMain } from 'electron';
export interface InvokeContext {
  sender: WebContents;
  frame: WebFrameMain | null;
}

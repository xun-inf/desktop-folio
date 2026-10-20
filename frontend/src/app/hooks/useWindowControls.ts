import { useEffect, useState } from 'react';
import type { WindowControl } from '../../common/page-contract';
const control = (action: WindowControl) => {
  void window.folioMain.windows.control(action).catch(console.error);
};
const minimize = () => control('minimize'),
  maximize = () => control('maximize');
const unmaximize = () => control('unmaximize'),
  toggleMaximize = () => control('toggleMaximize'),
  close = () => control('close');
export function useWindowControls() {
  const [maximized, setMaximized] = useState(false);
  useEffect(() => {
    let disposed = false;
    const update = (state: { maximized: boolean }) => {
      if (!disposed) setMaximized(state.maximized);
    };
    const unsubscribe = window.folioMain.windows.onState(update);
    void window.folioMain.windows.getState().then(update).catch(console.error);
    return () => {
      disposed = true;
      unsubscribe();
    };
  }, []);
  return { maximized, minimize, maximize, unmaximize, toggleMaximize, close };
}

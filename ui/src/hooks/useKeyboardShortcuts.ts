import { useEffect } from 'react';
import { useSimStore } from '../store';

export function useKeyboardShortcuts() {
  useEffect(() => {
    function handleKeyDown(e: KeyboardEvent) {
      if (
        e.target instanceof HTMLInputElement ||
        e.target instanceof HTMLTextAreaElement ||
        e.target instanceof HTMLSelectElement
      ) return;

      const speed = useSimStore.getState().speed;
      const setSpeed = useSimStore.getState().setSpeed;

      switch (e.code) {
        case 'Space':
          e.preventDefault();
          setSpeed(speed === 'paused' ? 'play' : 'paused');
          break;
        case 'ArrowRight':
          e.preventDefault();
          setSpeed('step');
          break;
        case 'ArrowUp':
          e.preventDefault();
          if (speed === 'paused') setSpeed('play');
          else if (speed === 'play') setSpeed('fast');
          break;
        case 'ArrowDown':
          e.preventDefault();
          if (speed === 'fast') setSpeed('play');
          else if (speed === 'play') setSpeed('paused');
          break;
      }
    }

    window.addEventListener('keydown', handleKeyDown);
    return () => window.removeEventListener('keydown', handleKeyDown);
  }, []);
}

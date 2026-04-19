import { useCallback, useEffect, useRef, useState } from 'react';
import { useSimStore } from './store';
import { TopBar } from './components/TopBar';
import { SceneCardPanel } from './components/SceneCardPanel';
import { CalendarPanel } from './components/CalendarPanel';
import { ProvincePanel } from './components/ProvincePanel';
import { MetricsDashboard } from './components/MetricsDashboard';
import { BusinessPanel } from './components/BusinessPanel';
import { ActionHistoryPanel } from './components/ActionHistoryPanel';
import { ToastContainer } from './components/Toast';
import { useKeyboardShortcuts } from './hooks/useKeyboardShortcuts';

interface ToastItem {
  id: number;
  message: string;
}

export function App() {
  const connected = useSimStore((s) => s.connected);
  const state = useSimStore((s) => s.state);
  const connect = useSimStore((s) => s.connect);
  const [showLog, setShowLog] = useState(false);
  const [toasts, setToasts] = useState<ToastItem[]>([]);
  const prevCardIdsRef = useRef<Set<number>>(new Set());

  useKeyboardShortcuts();

  useEffect(() => {
    connect();
  }, [connect]);

  useEffect(() => {
    if (!state) return;
    const activeCards = state.pending_scene_cards.filter(c => c.chosen_choice_id === 0);
    const currentIds = new Set(activeCards.map(c => c.id));
    const newCards = activeCards.filter(c => !prevCardIdsRef.current.has(c.id));
    if (newCards.length > 0) {
      setToasts(prev => [
        ...prev,
        ...newCards.map(c => ({
          id: c.id,
          message: `New ${c.type.replace(/_/g, ' ')}: ${c.npc_name}`,
        })),
      ]);
    }
    prevCardIdsRef.current = currentIds;
  }, [state]);

  const dismissToast = useCallback((id: number) => {
    setToasts(prev => prev.filter(t => t.id !== id));
  }, []);

  if (!connected || !state) {
    return (
      <div className="app-loading">
        <div className="loading-spinner" />
        <p>Connecting to simulation...</p>
      </div>
    );
  }

  const hasSceneCards = state.pending_scene_cards.filter(c => c.chosen_choice_id === 0).length > 0;

  return (
    <div className="app">
      <TopBar showLog={showLog} onToggleLog={() => setShowLog(v => !v)} />
      <ToastContainer toasts={toasts} onDismiss={dismissToast} />
      <div className="app-body">
        {showLog && <ActionHistoryPanel onClose={() => setShowLog(false)} />}
        <main className="main-panel">
          {hasSceneCards ? (
            <SceneCardPanel />
          ) : (
            <MetricsDashboard />
          )}
          <BusinessPanel />
        </main>
        <aside className="side-panel">
          <CalendarPanel />
          <ProvincePanel />
        </aside>
      </div>
    </div>
  );
}
